//===-- IA64ISelLowering.cpp - IA64 DAG Lowering Implementation -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the IA64TargetLowering class.
//
// Scope note: LowerFormalArguments / LowerReturn (Stage 1) and LowerCall
// (Stage C) are implemented for the integer, direct-call ABI that fib needs:
// args in r32-r39 (incoming) / out0-out7 (outgoing), return in r8, gp/sp/rp
// saved around calls; indirect calls go through the function descriptor
// (entry point into b6, callee gp into r1). TLS remains deferred.
//
//===----------------------------------------------------------------------===//

#include "IA64ISelLowering.h"
#include "IA64MachineFunctionInfo.h"
#include "IA64RegisterInfo.h"
#include "MCTargetDesc/IA64MCAsmInfo.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

// A floating-point scalar that is not long double (f64; f32 was promoted to
// f64 earlier) occupies exactly one parameter slot. IA-64's parameter model is
// positional: every argument, integer or FP, consumes a slot in one shared
// sequence -- the first eight slots map to r32-r39 (incoming) / out0-out7
// (outgoing), the rest to 8-byte stack slots. A *fixed* FP value travels in the
// next floating-point register F8-F15, but it must still RESERVE its general
// parameter slot so a following integer argument keeps its positional slot.
//
// CCAssignToRegWithShadow cannot express this: it shadows the GR at the *FP
// register's* index, so the first FP arg always shadows r32 no matter how many
// integers preceded it, never reserving the slot the FP arg actually occupies.
// A trailing integer then reused that slot's register -- e.g. the long long in
// _testfunc_q_bhilfdq(b,h,i,l,f,d,q) landed in the float's slot and read back
// the float's bit pattern instead of q.
//
// A *variadic* FP arg ('...' match) is passed in a general register in memory
// format: a prototyped variadic callee reads its variable arguments out of the
// integer parameter slots / register save area, never F8-F15 (psABI 8.5.4). It
// is bit-cast to its i64 IEEE pattern (getf.d, the BCvt in LowerCall) and put
// in the next slot register. SlotRegs is r32-r39 (incoming) / out0-out7 (call).
static bool CC_IA64_FP_Common(unsigned ValNo, MVT ValVT, MVT LocVT,
                              ISD::ArgFlagsTy ArgFlags, CCState &State,
                              ArrayRef<MCPhysReg> SlotRegs) {
  static const MCPhysReg FPRegs[] = {IA64::F8,  IA64::F9,  IA64::F10, IA64::F11,
                                     IA64::F12, IA64::F13, IA64::F14, IA64::F15};
  if (ArgFlags.isVarArg()) {
    if (unsigned Reg = State.AllocateReg(SlotRegs))
      State.addLoc(
          CCValAssign::getReg(ValNo, ValVT, Reg, MVT::i64, CCValAssign::BCvt));
    else
      State.addLoc(CCValAssign::getMem(ValNo, ValVT,
                                       State.AllocateStack(8, Align(8)),
                                       MVT::i64, CCValAssign::BCvt));
    return true;
  }
  // Fixed FP arg: reserve the positional GR slot; within the first eight slots
  // the value rides in the parallel FP register. Slots and FP registers are
  // consumed only by FP args here (and the f80 hook), so the FP register is
  // always available when a slot was, and they run out together; once the eight
  // slots are gone the value goes on the stack.
  if (State.AllocateReg(SlotRegs)) {
    unsigned FReg = State.AllocateReg(FPRegs);
    State.addLoc(
        CCValAssign::getReg(ValNo, ValVT, FReg, LocVT, CCValAssign::Full));
    return true;
  }
  State.addLoc(CCValAssign::getMem(
      ValNo, ValVT, State.AllocateStack(8, Align(8)), LocVT, CCValAssign::Full));
  return true;
}

// Incoming f64/f32: the parameter slots are the incoming stacked GP registers.
static bool CC_IA64_FP(unsigned ValNo, MVT ValVT, MVT LocVT,
                       CCValAssign::LocInfo /*LocInfo*/,
                       ISD::ArgFlagsTy ArgFlags, CCState &State) {
  static const MCPhysReg SlotRegs[] = {IA64::r32, IA64::r33, IA64::r34,
                                       IA64::r35, IA64::r36, IA64::r37,
                                       IA64::r38, IA64::r39};
  return CC_IA64_FP_Common(ValNo, ValVT, LocVT, ArgFlags, State, SlotRegs);
}

// Outgoing f64/f32: the parameter slots are the output registers out0-out7.
static bool CC_IA64_Call_FP(unsigned ValNo, MVT ValVT, MVT LocVT,
                            CCValAssign::LocInfo /*LocInfo*/,
                            ISD::ArgFlagsTy ArgFlags, CCState &State) {
  static const MCPhysReg SlotRegs[] = {IA64::out0, IA64::out1, IA64::out2,
                                       IA64::out3, IA64::out4, IA64::out5,
                                       IA64::out6, IA64::out7};
  return CC_IA64_FP_Common(ValNo, ValVT, LocVT, ArgFlags, State, SlotRegs);
}

// A named (prototyped) f80 'long double' argument is passed in one FP register
// in register format, but -- being 16 bytes -- it occupies TWO 16-byte-aligned
// (Next-Even) parameter slots, so it shadows two general registers (psABI
// 8.5.1). A variadic long double is passed in the general registers in memory
// format (two slots). ShadowRegs is r32-r39 (incoming) or out0-out7 (outgoing).
static bool CC_IA64_F80_Common(unsigned ValNo, MVT ValVT, MVT LocVT,
                               ISD::ArgFlagsTy ArgFlags, CCState &State,
                               ArrayRef<MCPhysReg> ShadowRegs) {
  static const MCPhysReg FPRegs[] = {IA64::F8,  IA64::F9,  IA64::F10, IA64::F11,
                                     IA64::F12, IA64::F13, IA64::F14, IA64::F15};
  // A long double (double-extended) uses the "Next Even" slot policy (psABI
  // 8.5.1, Table 8-3): it occupies two parameter slots and must START on an
  // even-numbered slot. The slot index equals the shadow-GR index for the
  // first eight slots, so if the next free shadow GR is odd, burn it as a
  // padding slot (it is not reused for any later parameter). Beyond the eight
  // register slots the same alignment is enforced on the stack via Align(16).
  unsigned NextSlot = State.getFirstUnallocated(ShadowRegs);
  if (NextSlot < ShadowRegs.size() && (NextSlot & 1))
    State.AllocateReg(ShadowRegs);

  if (ArgFlags.isVarArg()) {
    // A variadic long double is passed in the *general* registers in memory
    // format (psABI 8.5), occupying two parameter slots; spill into the stack
    // image if the registers are exhausted. Emit two i64 part-locations (this
    // value gets two CCValAssigns); LowerCall splits the f80 into the two
    // memory-format halves via an stfe/ld8 temporary. The first stack part is
    // 16-byte aligned to keep the Next-Even policy on the stack.
    for (int Part = 0; Part < 2; ++Part) {
      if (unsigned Reg = State.AllocateReg(ShadowRegs))
        State.addLoc(CCValAssign::getReg(ValNo, MVT::i64, Reg, MVT::i64,
                                         CCValAssign::Full));
      else
        State.addLoc(CCValAssign::getMem(
            ValNo, MVT::i64,
            State.AllocateStack(8, Align(Part == 0 ? 16 : 8)), MVT::i64,
            CCValAssign::Full));
    }
    return true;
  }
  if (unsigned FReg = State.AllocateReg(FPRegs)) {
    // Consume the two (now even-aligned) shadow GR parameter slots this 16-byte
    // value occupies so following arguments keep their positional slots.
    State.AllocateReg(ShadowRegs);
    State.AllocateReg(ShadowRegs);
    State.addLoc(
        CCValAssign::getReg(ValNo, ValVT, FReg, LocVT, CCValAssign::Full));
    return true;
  }
  // All FP argument registers used (reachable only via HFAs): pass the 16-byte
  // value on the stack.
  unsigned Off = State.AllocateStack(16, Align(16));
  State.addLoc(CCValAssign::getMem(ValNo, ValVT, Off, LocVT, CCValAssign::Full));
  return true;
}

// Incoming f80: shadow the incoming stacked GP registers r32-r39.
static bool CC_IA64_F80(unsigned ValNo, MVT ValVT, MVT LocVT,
                        CCValAssign::LocInfo /*LocInfo*/,
                        ISD::ArgFlagsTy ArgFlags, CCState &State) {
  static const MCPhysReg ShadowRegs[] = {IA64::r32, IA64::r33, IA64::r34,
                                         IA64::r35, IA64::r36, IA64::r37,
                                         IA64::r38, IA64::r39};
  return CC_IA64_F80_Common(ValNo, ValVT, LocVT, ArgFlags, State, ShadowRegs);
}

// Outgoing f80: shadow the output registers out0-out7.
static bool CC_IA64_Call_F80(unsigned ValNo, MVT ValVT, MVT LocVT,
                             CCValAssign::LocInfo /*LocInfo*/,
                             ISD::ArgFlagsTy ArgFlags, CCState &State) {
  static const MCPhysReg ShadowRegs[] = {IA64::out0, IA64::out1, IA64::out2,
                                         IA64::out3, IA64::out4, IA64::out5,
                                         IA64::out6, IA64::out7};
  return CC_IA64_F80_Common(ValNo, ValVT, LocVT, ArgFlags, State, ShadowRegs);
}

#include "IA64GenCallingConv.inc"

IA64TargetLowering::IA64TargetLowering(const TargetMachine &TM,
                                       const TargetSubtargetInfo &STI)
    : TargetLowering(TM, STI) {
  // Register classes: general (i64), floating-point (f32/f64/f80 = long double)
  // and predicate (i1). f80 is the 80-bit double-extended C 'long double',
  // held natively in the 82-bit FP registers (memory format via ldfe/stfe).
  addRegisterClass(MVT::i64, &IA64::GRRegClass);
  addRegisterClass(MVT::f32, &IA64::FPRegClass);
  addRegisterClass(MVT::f64, &IA64::FPRegClass);
  addRegisterClass(MVT::f80, &IA64::FPRegClass);
  addRegisterClass(MVT::i1, &IA64::PRRegClass);

  // IA-64 uses SELECT, not SELECT_CC, and has no native BR_CC / jump tables.
  setOperationAction(ISD::BRIND, MVT::Other, Legal);
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);

  // BR_CC / SELECT_CC must be keyed by the *compare operand* value type, not
  // MVT::Other. The DAGCombiner folds brcond(setcc) -> br_cc whenever BR_CC is
  // legal-or-custom for that operand type (DAGCombiner::visitBRCOND), and the
  // legalizer likewise queries getOperationAction by the operand type. The
  // pre-removal backend used MVT::Other, which was right for the LLVM 2.6
  // legalizer but is now a dead no-op -- it left BR_CC/i64 at its Legal default,
  // so brcond(setcc) got folded into an unselectable br_cc. Marking i64 Expand
  // keeps brcond(setcc) intact, which is exactly what our setcc (CMP*) patterns
  // and the hand-selected BRCOND consume. (Sparc keys these by operand type
  // too; it only differs in Custom-lowering them, having native cc-branches.)
  setOperationAction(ISD::BR_CC, MVT::i64, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::i64, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::f32, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::f64, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::f80, Expand);

  // FP compares: keep brcond(setcc f64) from folding into an unselectable
  // br_cc, so the legalizer hands us setcc + brcond. setcc f64 selects to the
  // fcmp relations (FCMP* in IA64InstrInfo.td), which cover every clang FP
  // condition except SETONE/SETUEQ; expand those into a pair joined by the i1
  // and/or patterns.
  setOperationAction(ISD::BR_CC, MVT::f64, Expand);
  setCondCodeAction(ISD::SETONE, MVT::f64, Expand);
  setCondCodeAction(ISD::SETUEQ, MVT::f64, Expand);
  // f80 ('long double') compares select to the same fcmp relations.
  setOperationAction(ISD::BR_CC, MVT::f80, Expand);
  setCondCodeAction(ISD::SETONE, MVT::f80, Expand);
  setCondCodeAction(ISD::SETUEQ, MVT::f80, Expand);
  // ...and so do f32 compares (fcmp looks at the full register-format value,
  // so single precision needs no separate compare path).
  setOperationAction(ISD::BR_CC, MVT::f32, Expand);
  setCondCodeAction(ISD::SETONE, MVT::f32, Expand);
  setCondCodeAction(ISD::SETUEQ, MVT::f32, Expand);

  // Comparing two predicates (i1): keep br_cc/select_cc as setcc + brcond/select,
  // and custom-lower the i1 setcc to predicate logic (eq/ne -> xnor/xor).
  setOperationAction(ISD::BR_CC, MVT::i1, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::i1, Expand);
  setOperationAction(ISD::SETCC, MVT::i1, Custom);
  // ...but mark the i1 eq/ne conditions Expand so the combiner's rebuildSetCC
  // does not turn our lowered xor back into an i1 setcc (an infinite loop, since
  // that setcc is Custom-lowered to the same xor again).
  setCondCodeAction(ISD::SETEQ, MVT::i1, Expand);
  setCondCodeAction(ISD::SETNE, MVT::i1, Expand);

  setOperationAction(ISD::SINT_TO_FP, MVT::i1, Promote);
  setOperationAction(ISD::UINT_TO_FP, MVT::i1, Promote);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);

  setOperationAction(ISD::FREM, MVT::f32, Expand);
  setOperationAction(ISD::FREM, MVT::f64, Expand);
  setOperationAction(ISD::FDIV, MVT::f32, Expand);
  setOperationAction(ISD::FDIV, MVT::f64, Expand);
  // f80 ('long double') has no inline divide/remainder; use the libcall
  // (__divxf3 / fmodl). fadd/fsub/fmpy/fma are native (FADD etc.).
  setOperationAction(ISD::FREM, MVT::f80, Expand);
  setOperationAction(ISD::FDIV, MVT::f80, Expand);

  // FP truncating stores must round first. stfs/stf8 emit fp_fr_to_mem_format,
  // which *assumes the FR was already rounded* to the destination precision --
  // they do not round themselves. So storing an unrounded wider value as a
  // narrower one would just slice its bits and corrupt the result. Expanding
  // these turns a truncstore into an explicit fpround (FNORMS/FNORMD) followed
  // by a same-size store of the now-rounded value, and also stops DAGCombiner
  // from re-merging store(fpround x) back into a single truncating store.
  // (The load direction needs no dual: ldfs/ldf8 always widen correctly.)
  setTruncStoreAction(MVT::f64, MVT::f32, Expand);
  setTruncStoreAction(MVT::f80, MVT::f32, Expand);
  setTruncStoreAction(MVT::f80, MVT::f64, Expand);

  // IA-64 has no native half (f16). Convert to/from f16 via the soft-float
  // libcalls (__truncsfhf2/__extendhfsf2 etc.) and never load/store f16 as an
  // extended/truncated FP value -- it is handled as i16 bits. Mirrors SPARC.
  // (f128 needs no such setup: with no f128 register class it is soft-floated
  // to the default libgcc __*tf3 libcalls.)
  for (MVT VT : {MVT::f32, MVT::f64, MVT::f80, MVT::f128}) {
    setOperationAction(ISD::FP_TO_FP16, VT, Expand);
    setOperationAction(ISD::FP16_TO_FP, VT, Expand);
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::f16, Expand);
    setTruncStoreAction(VT, MVT::f16, Expand);
  }

  // We don't support sin/cos/sqrt/pow (expand to libcalls: sinl/cosl/sqrtl/...).
  for (MVT VT : {MVT::f32, MVT::f64, MVT::f80}) {
    setOperationAction(ISD::FSIN, VT, Expand);
    setOperationAction(ISD::FCOS, VT, Expand);
    setOperationAction(ISD::FSQRT, VT, Expand);
    setOperationAction(ISD::FPOW, VT, Expand);
    // FIXME: IA64 supports fcopysign natively.
    setOperationAction(ISD::FCOPYSIGN, VT, Expand);
  }

  // IA-64 has a native population count (popcnt); select ctpop directly.
  setOperationAction(ISD::CTPOP, MVT::i64, Legal);
  // ctlz/cttz have no direct instruction; let the legalizer expand them (now
  // cheaply, in terms of the legal ctpop above).
  setOperationAction(ISD::CTLZ, MVT::i64, Expand);
  setOperationAction(ISD::CTTZ, MVT::i64, Expand);
  setOperationAction(ISD::ROTL, MVT::i64, Expand);
  setOperationAction(ISD::ROTR, MVT::i64, Expand);
  // FIXME: IA64 has this (mux @rev), but it is not implemented.
  setOperationAction(ISD::BSWAP, MVT::i64, Expand);

  // Use toolchain built-in for integer division
  for (unsigned Op : {ISD::SDIV, ISD::UDIV, ISD::SREM, ISD::UREM, ISD::UDIVREM,
                      ISD::SDIVREM})
    setOperationAction(Op, MVT::i64, Expand);

  // No single instruction yields both halves of a 64x64 product; expand into a
  // separate low MUL and a high MULHU/MULHS (both of which we select).
  setOperationAction(ISD::UMUL_LOHI, MVT::i64, Expand);
  setOperationAction(ISD::SMUL_LOHI, MVT::i64, Expand);

  // 128-bit shifts (i128, e.g. `core`'s checked_shl) legalize to a *_PARTS node
  // over an i64 register pair. We have no instruction for that; mark them Expand
  // so the integer legalizer emits the libgcc libcall (__ashlti3/__ashrti3/
  // __lshrti3) instead, matching how we already handle 128-bit divide/modulo.
  setOperationAction(ISD::SHL_PARTS, MVT::i64, Expand);
  setOperationAction(ISD::SRA_PARTS, MVT::i64, Expand);
  setOperationAction(ISD::SRL_PARTS, MVT::i64, Expand);

  // va_start points the va_list at the register save area (custom); va_arg,
  // va_copy and va_end use the generic load/increment/store expansion. The
  // va_list is a plain pointer, so the default va_copy/va_end suffice.
  // Thread-local addresses are lowered per TLS model (see LowerGlobalTLSAddress);
  // there is no generic expansion, so it must be Custom.
  setOperationAction(ISD::GlobalTLSAddress, MVT::i64, Custom);

  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::VAARG, MVT::Other, Expand);
  setOperationAction(ISD::VACOPY, MVT::Other, Expand);
  setOperationAction(ISD::VAEND, MVT::Other, Expand);
  setOperationAction(ISD::STACKSAVE, MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Expand);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i64, Expand);

  // Atomics. An aligned <=8-byte ld/st is atomic on the hardware, but LLVM
  // represents an atomic access as a distinct node (ISD::ATOMIC_LOAD/STORE)
  // that the selector won't turn into ld8/st8 on its own. Custom-lower them to
  // a plain load/store carrying the same (atomic) memory operand; see
  // LowerOperation. Ordering is handled separately: shouldInsertFencesForAtomic
  // asks AtomicExpand to wrap stronger orderings with fences and demote the
  // access to monotonic, so the only atomic load/store we ever lower here is
  // monotonic. The fences become ISD::ATOMIC_FENCE, selected to 'mf'.
  //
  // Only the legal integer type i64 is marked Custom: a narrow (i8/i16/i32)
  // atomic load/store has an illegal type and is first widened by the *type*
  // legalizer (PromoteIntRes_Atomic0 / PromoteIntOp_ATOMIC_STORE) to an i64
  // access carrying the narrow memory VT, which then reaches LowerOperation as
  // an i64 Custom op. Marking the narrow types Custom instead would divert type
  // legalization into ReplaceNodeResults (which we do not implement) and abort.
  setMaxAtomicSizeInBitsSupported(64);
  setOperationAction(ISD::ATOMIC_LOAD, MVT::i64, Custom);
  setOperationAction(ISD::ATOMIC_STORE, MVT::i64, Custom);
  setOperationAction(ISD::ATOMIC_FENCE, MVT::Other, Legal);

  setStackPointerRegisterToSaveRestore(IA64::r12);

  // The pre-removal backend reported a Log2 function alignment of 5, i.e. a
  // 32-byte alignment ('.align 32' in the reference output).
  setMinFunctionAlignment(Align(32));

  computeRegisterProperties(STI.getRegisterInfo());

  // Note: the pre-removal backend called addLegalFPImmediate(0/±1) here; that
  // API was removed (FP-immediate legality is now an isFPImmLegal override).
  // plus.ll uses no FP immediates, so this is left for a later stage.
}

const char *IA64TargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch (Opcode) {
  default:
    return nullptr;
  case IA64ISD::GETFD:
    return "IA64ISD::GETFD";
  case IA64ISD::BRCALL:
    return "IA64ISD::BRCALL";
  case IA64ISD::RET_FLAG:
    return "IA64ISD::RET_FLAG";
  case IA64ISD::TLS_TPREL:
    return "IA64ISD::TLS_TPREL";
  case IA64ISD::TLS_GOTLOAD:
    return "IA64ISD::TLS_GOTLOAD";
  }
}

EVT IA64TargetLowering::getSetCCResultType(const DataLayout & /*DL*/,
                                           LLVMContext & /*Context*/,
                                           EVT /*VT*/) const {
  // SETCC produces a predicate register value.
  return MVT::i1;
}

bool IA64TargetLowering::isFMAFasterThanFMulAndFAdd(const MachineFunction & /*MF*/,
                                                    EVT VT) const {
  // fma/fms/fnma fuse a*b+c into one single-rounding F-unit op. f32 (fma.s),
  // f64 (fma.d) and f80 (fma) each have a hardware FMA pattern, so contracting
  // fmul+fadd is profitable for them. This stays an explicit whitelist (not
  // `true`): f16 and f128 are soft-floated, and contracting those would form an
  // fma node of a width with no hardware pattern (unselectable / soft-float).
  return VT == MVT::f32 || VT == MVT::f64 || VT == MVT::f80;
}

bool IA64TargetLowering::isFPImmLegal(const APFloat & /*Imm*/, EVT VT,
                                      bool /*ForCodeSize*/) const {
  // Keep f32/f64 constants out of the constant pool: we materialise them from
  // their integer bit pattern (movl + setf.d) -- see the fpimm patterns in
  // IA64InstrInfo.td. f80 ('long double') is 80 bits and cannot be built from a
  // single 64-bit movl, so its literals go to the constant pool (loaded by ldfe;
  // see the ISD::ConstantPool selection in IA64ISelDAGToDAG).
  return VT == MVT::f32 || VT == MVT::f64;
}

SDValue IA64TargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CC_IA64);

  for (CCValAssign &VA : ArgLocs) {
    if (VA.isRegLoc()) {
      // The argument arrives in a register.
      MVT RegVT = VA.getLocVT();
      const TargetRegisterClass *RC;
      if (RegVT == MVT::i64)
        RC = &IA64::GRRegClass;
      else if (RegVT == MVT::f32 || RegVT == MVT::f64 || RegVT == MVT::f80)
        RC = &IA64::FPRegClass;
      else
        report_fatal_error("IA64: unhandled formal-argument register type");

      Register VReg = RegInfo.createVirtualRegister(RC);
      RegInfo.addLiveIn(VA.getLocReg(), VReg);
      SDValue ArgValue = DAG.getCopyFromReg(Chain, dl, VReg, RegVT);

      // If the argument was widened to fill the register, narrow it back to
      // its declared type.
      if (RegVT != VA.getValVT()) {
        if (RegVT.isInteger())
          ArgValue = DAG.getNode(ISD::TRUNCATE, dl, VA.getValVT(), ArgValue);
        else
          ArgValue = DAG.getNode(ISD::FP_ROUND, dl, VA.getValVT(), ArgValue,
                                 DAG.getIntPtrConstant(0, dl, /*isTarget=*/true));
      }

      InVals.push_back(ArgValue);
    } else {
      // The argument arrives on the stack. Per the psABI (§8.5.3) parameter
      // slot 8 is at sp+16, slot 9 at sp+24, and so on (the 16-byte scratch
      // area sits below at [sp, sp+16)). This holds whether or not the function
      // is variadic -- the variadic register-home spill area is carved out of
      // *this* frame and the scratch area, not reserved by the caller (see the
      // spill loop below).
      assert(VA.isMemLoc() && "unexpected argument location");
      int FI = MF.getFrameInfo().CreateFixedObject(
          8, 16 + VA.getLocMemOffset(), /*IsImmutable=*/true);
      SDValue FIN = DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
      InVals.push_back(
          DAG.getLoad(VA.getValVT(), dl, Chain, FIN, MachinePointerInfo()));
    }
  }

  // Variadic functions: spill the unnamed incoming GP registers to their
  // parameter-slot memory homes so va_start/va_arg can walk the variadic
  // arguments as a single contiguous in-memory image. Per the psABI (§8.5.4)
  // the callee spills in6/in7 into the 16-byte scratch area at [sp, sp+16) and
  // in0-in5 into up to 48 bytes at the base of its own frame, just below sp.
  // This places parameter slot i at offset 8*i - 48 from the incoming sp:
  // slot6 -> sp+0, slot7 -> sp+8, slot8 (first stack arg) -> sp+16, slot9 ->
  // sp+24, ... -- one contiguous ascending block running from the frame base up
  // into the caller's memory arguments. A va_list is just an ascending pointer,
  // so it walks out of the register homes straight into the stack arguments.
  // (CreateFixedObject offsets are relative to the incoming sp; negative
  // offsets land in this frame, which PrologEpilogInserter sizes to cover.
  // Storing the registers also marks them used, so frame lowering's 'alloc'
  // keeps all eight incoming GP registers live as locals.)
  if (isVarArg) {
    static const MCPhysReg ArgGPRs[] = {IA64::r32, IA64::r33, IA64::r34,
                                        IA64::r35, IA64::r36, IA64::r37,
                                        IA64::r38, IA64::r39};
    unsigned FirstVar = CCInfo.getFirstUnallocated(ArgGPRs);
    MachineFrameInfo &MFI = MF.getFrameInfo();
    int VAFI = 0;
    SmallVector<SDValue, 8> Stores;
    for (unsigned i = FirstVar; i < 8; ++i) {
      int FI = MFI.CreateFixedObject(8, 8 * (int)i - 48, /*IsImmutable=*/false);
      if (i == FirstVar)
        VAFI = FI; // va_start points at the first unnamed slot's home
      Register VReg = RegInfo.createVirtualRegister(&IA64::GRRegClass);
      RegInfo.addLiveIn(ArgGPRs[i], VReg);
      SDValue Val = DAG.getCopyFromReg(Chain, dl, VReg, MVT::i64);
      SDValue Addr = DAG.getFrameIndex(FI, MVT::i64);
      Stores.push_back(DAG.getStore(Val.getValue(1), dl, Val, Addr,
                                    MachinePointerInfo::getFixedStack(MF, FI)));
    }
    // All eight GP slots named: no register varargs, so va_start points at the
    // first unnamed stack slot. That is slot 8 (sp+16) only when there are no
    // *named* stack arguments; if the prototype has named parameters beyond the
    // eight register slots (e.g. Links' input_field: 8 register params + 4 named
    // stack args + ...), the unnamed args begin after them, at
    // sp + 16 + <bytes of named stack args>. CCInfo.getStackSize() is exactly
    // those bytes (the formals were just analyzed above).
    if (FirstVar == 8)
      VAFI = MFI.CreateFixedObject(8, 16 + CCInfo.getStackSize(),
                                   /*IsImmutable=*/true);
    MF.getInfo<IA64FunctionInfo>()->setVarArgsFrameIndex(VAFI);
    if (!Stores.empty())
      Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Stores);
  }

  // 'alloc' (which captures the caller's ar.pfs) and its restore are now emitted
  // entirely by frame lowering into a reserved stacked local, so there is
  // nothing to materialise here. See IA64FrameLowering::emitPrologue.
  return Chain;
}

SDValue IA64TargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                      SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc &dl = CLI.DL;
  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals = CLI.OutVals;
  SmallVectorImpl<ISD::InputArg> &Ins = CLI.Ins;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  CallingConv::ID CallConv = CLI.CallConv;
  bool isVarArg = CLI.IsVarArg;
  MachineFunction &MF = DAG.getMachineFunction();

  // No tail calls yet.
  CLI.IsTailCall = false;

  // Assign the outgoing arguments to out0-out7 / F8-F15 (caller convention).
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeCallOperands(Outs, CC_IA64_Call);

  // A 16-byte scratch area sits at the bottom of the outgoing frame; keep the
  // whole thing 16-byte aligned. Stack-passed arguments begin at sp+16 (psABI
  // §8.5.3), variadic or not: the variadic register-home spill area is built by
  // the callee out of its own frame and the scratch area, not reserved here
  // (see LowerFormalArguments).
  unsigned NumBytes = (CCInfo.getStackSize() + 16 + 15) & ~15u;

  // Record how many output registers this call needs; the prologue 'alloc'
  // sizes its output region from the max over all of the function's calls.
  // Count the actually-allocated out registers rather than the argument count:
  // an FP argument shadows (consumes) its parameter slot(s) without occupying an
  // out register for the value, while a long double (f80) shadows *two* out
  // slots -- so a trailing integer arg can land in a higher out register than
  // the plain argument count would suggest.
  static const MCPhysReg OutRegs[] = {IA64::out0, IA64::out1, IA64::out2,
                                      IA64::out3, IA64::out4, IA64::out5,
                                      IA64::out6, IA64::out7};
  unsigned NumOutRegs = 0;
  for (unsigned i = 0; i < 8; ++i)
    if (CCInfo.isAllocated(OutRegs[i]))
      NumOutRegs = i + 1;
  IA64FunctionInfo *FInfo = MF.getInfo<IA64FunctionInfo>();
  FInfo->OutRegsUsed = std::max(FInfo->OutRegsUsed, NumOutRegs);

  Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, dl);

  // An indirect callee is a function pointer: not a GlobalAddress/ExternalSymbol
  // but an ordinary i64 value pointing at a function descriptor { entry, gp }.
  // Read the descriptor here, while Chain is still a plain (unglued) chain and
  // before the gp save below latches the caller's r1; the entry point and the
  // callee's gp are installed into b6 / r1 just before the call further down.
  bool IsIndirect = !isa<GlobalAddressSDNode>(Callee) &&
                    !isa<ExternalSymbolSDNode>(Callee);
  SDValue EntryPoint, NewGp;
  if (IsIndirect) {
    EntryPoint = DAG.getLoad(MVT::i64, dl, Chain, Callee, MachinePointerInfo());
    Chain = EntryPoint.getValue(1);
    SDValue GpAddr = DAG.getNode(ISD::ADD, dl, MVT::i64, Callee,
                                 DAG.getIntPtrConstant(8, dl));
    NewGp = DAG.getLoad(MVT::i64, dl, Chain, GpAddr, MachinePointerInfo());
    Chain = NewGp.getValue(1);
  }

  // Collect the (out-register, value) pairs to copy in just before the call,
  // and the stores for any arguments that overflow onto the outgoing stack.
  SmallVector<std::pair<unsigned, SDValue>, 8> RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;
  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];
    // Index by ValNo, not i: a variadic long double maps one argument value to
    // two consecutive parameter-slot locations (see below), after which i and
    // the argument number diverge.
    SDValue Arg = OutVals[VA.getValNo()];

    // By-value aggregate argument. The psABI passes aggregates by value; the
    // frontend models this as a `byval` pointer to the caller's object and
    // expects the callee to receive a pointer to a *private copy*. We currently
    // realize that copy here (the callee then dereferences the pointer as usual)
    // rather than flattening the aggregate into parameter slots/GRs -- that full
    // ABI is still TODO (see struct-value-abi.md). The copy is mandatory: without
    // it the argument aliases caller memory, and a callee that mutates or frees
    // that memory corrupts the caller. Concretely, glibc regex's re_dfa_add_node
    // takes an re_token_t by value and `realloc`s the very dfa->nodes array a
    // by-value `dfa->nodes[org_idx]` argument points into -- so the un-copied
    // pointer dangled into the freed block and read back garbage.
    ISD::ArgFlagsTy Flags = Outs[VA.getValNo()].Flags;
    if (Flags.isByVal()) {
      unsigned Size = Flags.getByValSize();
      if (Size != 0) {
        Align ByValAlign = Flags.getNonZeroByValAlign();
        int FI = MF.getFrameInfo().CreateStackObject(Size, ByValAlign, false);
        SDValue Copy = DAG.getFrameIndex(FI, MVT::i64);
        SDValue MemcpyChain = DAG.getMemcpy(
            Chain, dl, Copy, Arg, DAG.getIntPtrConstant(Size, dl), ByValAlign,
            /*isVol=*/false, /*AlwaysInline=*/false, /*CI=*/nullptr,
            /*OverrideTailCall=*/std::nullopt,
            MachinePointerInfo::getFixedStack(MF, FI), MachinePointerInfo());
        // Order the copy before the call (alongside the other arg stores).
        MemOpChains.push_back(MemcpyChain);
        Arg = Copy; // pass the private copy's address per VA below
      }
    }

    // Variadic long double (f80): the CC gave it two consecutive i64 slots --
    // this location and the next, both tagged with the same ValNo. It is passed
    // in memory format (psABI 8.5).
    if (i + 1 < e && ArgLocs[i + 1].getValNo() == VA.getValNo()) {
      CCValAssign &VAHi = ArgLocs[i + 1];

      // Both halves land on the outgoing stack: store the long double straight
      // to its parameter slot with stfe (memory format) -- no register
      // round-trip. (The two slots are adjacent, so one 10-byte stfe covers the
      // significant bytes; the callee's va_arg reads it back with ldfe.) The
      // spill-and-reload path below would only DAGCombine down to this if the
      // combiner forwarded an f80 store into i64 loads, which it does not.
      if (VA.isMemLoc() && VAHi.isMemLoc()) {
        unsigned Off = 16 + VA.getLocMemOffset(); // psABI: slot 8 at sp+16
        SDValue Addr = DAG.getNode(ISD::ADD, dl, MVT::i64,
                                   DAG.getRegister(IA64::r12, MVT::i64),
                                   DAG.getIntPtrConstant(Off, dl));
        MemOpChains.push_back(DAG.getStore(Chain, dl, Arg, Addr,
                                           MachinePointerInfo::getStack(MF, Off)));
        ++i; // consumed both part-locations
        continue;
      }

      // At least one half goes in a general register: spill to a 16-byte
      // temporary with stfe and reload the two 8-byte memory-format halves
      // (ld8) into the assigned slots -- the in-memory image the callee's
      // va_arg reconstructs with ldfe. (There is no register instruction to
      // extract the 80-bit *memory* format into GRs, so the spill is required.)
      int FI = MF.getFrameInfo().CreateStackObject(16, Align(16), false);
      SDValue Tmp = DAG.getFrameIndex(FI, MVT::i64);
      SDValue St = DAG.getStore(Chain, dl, Arg, Tmp,
                                MachinePointerInfo::getFixedStack(MF, FI));
      SDValue HiAddr = DAG.getNode(ISD::ADD, dl, MVT::i64, Tmp,
                                   DAG.getIntPtrConstant(8, dl));
      SDValue Half[2] = {
          DAG.getLoad(MVT::i64, dl, St, Tmp,
                      MachinePointerInfo::getFixedStack(MF, FI)),
          DAG.getLoad(MVT::i64, dl, St, HiAddr,
                      MachinePointerInfo::getFixedStack(MF, FI, 8))};
      // Order the spill/reload before the call.
      MemOpChains.push_back(Half[0].getValue(1));
      MemOpChains.push_back(Half[1].getValue(1));
      for (unsigned Part = 0; Part < 2; ++Part) {
        CCValAssign &PVA = ArgLocs[i + Part];
        if (PVA.isRegLoc()) {
          RegsToPass.push_back(std::make_pair(PVA.getLocReg(), Half[Part]));
        } else {
          unsigned Off = 16 + PVA.getLocMemOffset(); // psABI: slot 8 at sp+16
          SDValue Addr = DAG.getNode(ISD::ADD, dl, MVT::i64,
                                     DAG.getRegister(IA64::r12, MVT::i64),
                                     DAG.getIntPtrConstant(Off, dl));
          MemOpChains.push_back(DAG.getStore(
              Chain, dl, Half[Part], Addr, MachinePointerInfo::getStack(MF, Off)));
        }
      }
      ++i; // consumed both part-locations
      continue;
    }

    switch (VA.getLocInfo()) {
    case CCValAssign::Full:
      break;
    case CCValAssign::BCvt:
      // A variadic FP arg routed into a GR slot: reinterpret the f64 as its
      // i64 IEEE bit pattern (selects to getf.d). See CC_IA64_FP_Common.
      Arg = DAG.getNode(ISD::BITCAST, dl, VA.getLocVT(), Arg);
      break;
    case CCValAssign::SExt:
      Arg = DAG.getNode(ISD::SIGN_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    case CCValAssign::ZExt:
      Arg = DAG.getNode(ISD::ZERO_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    case CCValAssign::AExt:
      Arg = DAG.getNode(ISD::ANY_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    case CCValAssign::FPExt:
      Arg = DAG.getNode(ISD::FP_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    default:
      report_fatal_error("IA64: unhandled argument CCValAssign");
    }

    if (VA.isRegLoc()) {
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), Arg));
    } else {
      // Arguments beyond out0-out7 are passed on the outgoing stack, just above
      // the 16-byte scratch area: parameter slot 8 at sp+16, slot 9 at sp+24,
      // ... (psABI §8.5.3) -- the same layout LowerFormalArguments reads
      // incoming stack arguments from. The store is sp-relative: with a reserved
      // call frame (no variable-sized objects) sp is constant here; otherwise
      // the call-frame pseudos adjust it around the call.
      assert(VA.isMemLoc() && "argument neither in register nor on the stack");
      unsigned Off = 16 + VA.getLocMemOffset();
      SDValue StackPtr = DAG.getRegister(IA64::r12, MVT::i64);
      SDValue Addr = DAG.getNode(ISD::ADD, dl, MVT::i64, StackPtr,
                                 DAG.getIntPtrConstant(Off, dl));
      MemOpChains.push_back(DAG.getStore(
          Chain, dl, Arg, Addr, MachinePointerInfo::getStack(MF, Off)));
    }
  }

  // Sequence all the outgoing-argument stores before the call.
  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOpChains);

  // Save gp/sp around the call. br.call may transfer into another load module
  // (so the callee's gp must be reinstalled afterwards) and sp is restored
  // conservatively. These reads must precede the call and the restores must
  // follow it, so the whole save -> args -> call -> restore sequence is tied
  // together with glue. Use the glue-carrying getCopyFromReg overload even for
  // the first save (with a null input glue): it still gives the node a glue
  // *result* to start the chain. The plain 4-operand form has no glue result,
  // so reading getValue(2) off it would be out of range.
  //
  // We deliberately do NOT save/restore rp (b0) per call here. br.call does
  // overwrite b0, but frame lowering already parks the incoming rp once in a
  // stacked local for the whole function (IA64FrameLowering::emitPrologue) and
  // the epilogue restores b0 from it, so our br.ret returns correctly no matter
  // how many calls clobber rp in between -- the per-call save was redundant.
  // Worse, it was actively wrong: rp is a member of the GR class (so that
  // 'mov rN = rp' works), the save value was live across the call and coalesced
  // into the physical rp, and the spiller then spilled it with a plain
  // 'st8 [slot] = rp' / 'ld8 rp = [slot]'. That is illegal -- st8/ld8 require a
  // general register, not the branch register b0 -- and gas rejects it
  // ("Operand N of st8/ld8 should be a general register"). The only place rp is
  // still read around a call is the returns_twice path below, where it is parked
  // into the CSR r7 *before* the call and so is never live across it as b0.
  SDValue InGlue;
  SDValue GPSave = DAG.getCopyFromReg(Chain, dl, IA64::r1, MVT::i64, InGlue);
  Chain = GPSave.getValue(1);
  InGlue = GPSave.getValue(2);
  SDValue SPSave = DAG.getCopyFromReg(Chain, dl, IA64::r12, MVT::i64, InGlue);
  Chain = SPSave.getValue(1);
  InGlue = SPSave.getValue(2);

  // In a function that calls setjmp (and so may be re-entered by longjmp), the
  // save vregs above cannot be allowed to land in stacked locals: longjmp brings
  // the stacked frame back only to its last-written values, and the register
  // allocator reuses the save register right after the (singly-modeled) restore
  // -- which sits before the setjmp-result branch, i.e. exactly the longjmp
  // re-entry point -- so the restored value is garbage (observed: gp = 0, then a
  // stale slot address). Park gp/sp/rp instead in the static callee-saved
  // registers r4/r6/r7, which glibc's setjmp/longjmp save and restore through the
  // jmpbuf: on a longjmp re-entry they come back holding the setjmp-time
  // gp/sp/rp, and any reuse after the restore is harmless because longjmp
  // overwrites it. Because they are true CSRs (getCalleeSavedRegs), a nested
  // setjmp call saves and restores them, so it cannot clobber an outer frame's
  // parked values. The restore below reads them back out of r4/r6/r7. Reading rp
  // here is safe (it is parked into r7, a GR, before the call -- never spilled as
  // b0 across the call).
  bool ReturnsTwice = MF.exposesReturnsTwice();
  SDValue RPSave;
  if (ReturnsTwice) {
    RPSave = DAG.getCopyFromReg(Chain, dl, IA64::rp, MVT::i64, InGlue);
    Chain = RPSave.getValue(1);
    InGlue = RPSave.getValue(2);
    Chain = DAG.getCopyToReg(Chain, dl, IA64::r4, GPSave, InGlue);
    InGlue = Chain.getValue(1);
    Chain = DAG.getCopyToReg(Chain, dl, IA64::r6, SPSave, InGlue);
    InGlue = Chain.getValue(1);
    Chain = DAG.getCopyToReg(Chain, dl, IA64::r7, RPSave, InGlue);
    InGlue = Chain.getValue(1);
  }

  // Copy the outgoing arguments into their out registers, glued before the call.
  for (auto &R : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, dl, R.first, R.second, InGlue);
    InGlue = Chain.getValue(1);
  }

  // Set up the br.call target. For an indirect call, install the callee's gp
  // (r1) and the entry point (b6) read from the descriptor above, glued in just
  // after the argument copies; BRCALL then branches to b6. For a direct call,
  // make the callee a target node so the generic selector leaves it alone and
  // the IA64ISD::BRCALL selection consumes it as the br.call target.
  if (IsIndirect) {
    Chain = DAG.getCopyToReg(Chain, dl, IA64::r1, NewGp, InGlue);
    InGlue = Chain.getValue(1);
    Chain = DAG.getCopyToReg(Chain, dl, IA64::B6, EntryPoint, InGlue);
    InGlue = Chain.getValue(1);
    Callee = DAG.getRegister(IA64::B6, MVT::i64);
  } else if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee))
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), dl, MVT::i64);
  else if (ExternalSymbolSDNode *E = dyn_cast<ExternalSymbolSDNode>(Callee))
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(), MVT::i64);

  // Emit the call.
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  SmallVector<SDValue, 12> Ops = {Chain, Callee};
  for (auto &R : RegsToPass)
    Ops.push_back(DAG.getRegister(R.first, R.second.getValueType()));
  if (InGlue.getNode())
    Ops.push_back(InGlue);
  Chain = DAG.getNode(IA64ISD::BRCALL, dl, NodeTys, Ops);
  InGlue = Chain.getValue(1);

  // Restore gp/sp after the call. For a returns_twice function read gp/sp/rp back
  // out of r4/r6/r7 (longjmp-safe, see the save above) and reinstate rp from r7
  // (a plain GR->GR copy, never spilled as b0); otherwise restore gp/sp from the
  // save vregs directly. The common path needs no rp restore -- frame lowering
  // owns the function's return pointer (see the save block above).
  if (ReturnsTwice) {
    GPSave = DAG.getCopyFromReg(Chain, dl, IA64::r4, MVT::i64, InGlue);
    Chain = GPSave.getValue(1);
    InGlue = GPSave.getValue(2);
    SPSave = DAG.getCopyFromReg(Chain, dl, IA64::r6, MVT::i64, InGlue);
    Chain = SPSave.getValue(1);
    InGlue = SPSave.getValue(2);
    RPSave = DAG.getCopyFromReg(Chain, dl, IA64::r7, MVT::i64, InGlue);
    Chain = RPSave.getValue(1);
    InGlue = RPSave.getValue(2);
  }
  Chain = DAG.getCopyToReg(Chain, dl, IA64::r1, GPSave, InGlue);
  InGlue = Chain.getValue(1);
  Chain = DAG.getCopyToReg(Chain, dl, IA64::r12, SPSave, InGlue);
  InGlue = Chain.getValue(1);
  // rp last (only for returns_twice), preserving the gp -> sp -> rp restore order.
  if (ReturnsTwice) {
    Chain = DAG.getCopyToReg(Chain, dl, IA64::rp, RPSave, InGlue);
    InGlue = Chain.getValue(1);
  }

  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, InGlue, dl);
  InGlue = Chain.getValue(1);

  // Read the return value(s) out of r8 / F8.
  SmallVector<CCValAssign, 16> RVLocs;
  CCState RVInfo(CallConv, isVarArg, MF, RVLocs, *DAG.getContext());
  RVInfo.AnalyzeCallResult(Ins, RetCC_IA64);
  for (unsigned i = 0, e = RVLocs.size(); i != e; ++i) {
    CCValAssign &VA = RVLocs[i];
    SDValue Val =
        DAG.getCopyFromReg(Chain, dl, VA.getLocReg(), VA.getLocVT(), InGlue);
    Chain = Val.getValue(1);
    InGlue = Val.getValue(2);

    if (VA.getLocVT() != VA.getValVT()) {
      if (VA.getLocVT().isInteger())
        Val = DAG.getNode(ISD::TRUNCATE, dl, VA.getValVT(), Val);
      else
        Val = DAG.getNode(ISD::FP_ROUND, dl, VA.getValVT(), Val,
                          DAG.getIntPtrConstant(0, dl, /*isTarget=*/true));
    }
    InVals.push_back(Val);
  }

  return Chain;
}

SDValue IA64TargetLowering::LowerOperation(SDValue Op,
                                           SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  default:
    report_fatal_error("IA64: unimplemented custom operation lowering");
  case ISD::FRAMEADDR: {
    // __builtin_frame_address(0): the address of the current frame, which we
    // take to be the frame register (the frame pointer r5 if one is forced,
    // else the stack pointer r12).
    if (Op.getConstantOperandVal(0) != 0)
      report_fatal_error("IA64: __builtin_frame_address with nonzero depth is "
                         "not supported");
    MachineFunction &MF = DAG.getMachineFunction();
    MF.getFrameInfo().setFrameAddressIsTaken(true);
    Register FrameReg = MF.getSubtarget().getRegisterInfo()->getFrameRegister(MF);
    return DAG.getCopyFromReg(DAG.getEntryNode(), SDLoc(Op), FrameReg,
                              Op.getValueType());
  }
  case ISD::GlobalTLSAddress:
    return LowerGlobalTLSAddress(Op, DAG);
  case ISD::SETCC: {
    // i1 (predicate) comparison: a != b is xor, a == b is its complement
    // (xor then invert via xor with 1). Booleans only ever use eq/ne.
    SDLoc dl(Op);
    ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(2))->get();
    SDValue Xor = DAG.getNode(ISD::XOR, dl, MVT::i1, Op.getOperand(0),
                              Op.getOperand(1));
    if (CC == ISD::SETNE)
      return Xor;
    if (CC == ISD::SETEQ)
      return DAG.getNode(ISD::XOR, dl, MVT::i1, Xor,
                         DAG.getConstant(1, dl, MVT::i1));
    report_fatal_error("IA64: unhandled i1 SETCC condition (expected eq/ne)");
  }
  case ISD::ATOMIC_LOAD: {
    // Lower a monotonic atomic load (AtomicExpand has already split off any
    // stronger ordering into fences) to a plain load with the same atomic
    // memory operand. The existing ISD::LOAD selector picks ld1/ld2/ld4/ld8 by
    // the memory type and applies the zero/sign extension carried here.
    AtomicSDNode *AN = cast<AtomicSDNode>(Op);
    SDLoc dl(Op);
    return DAG.getExtLoad(AN->getExtensionType(), dl, Op.getValueType(),
                          AN->getChain(), AN->getBasePtr(), AN->getMemoryVT(),
                          AN->getMemOperand());
  }
  case ISD::ATOMIC_STORE: {
    // Mirror of ATOMIC_LOAD: a monotonic atomic store becomes a plain (possibly
    // truncating) store. The value is promoted to i64, so a narrow access is a
    // truncating store keyed on the memory type, which the store selector
    // handles.
    AtomicSDNode *AN = cast<AtomicSDNode>(Op);
    SDLoc dl(Op);
    SDValue Val = AN->getVal();
    EVT MemVT = AN->getMemoryVT();
    if (MemVT == Val.getValueType())
      return DAG.getStore(AN->getChain(), dl, Val, AN->getBasePtr(),
                          AN->getMemOperand());
    return DAG.getTruncStore(AN->getChain(), dl, Val, AN->getBasePtr(), MemVT,
                             AN->getMemOperand());
  }
  case ISD::VASTART: {
    // va_start stores the address of the register save area (the first variadic
    // argument slot, filled in by LowerFormalArguments) into the va_list.
    MachineFunction &MF = DAG.getMachineFunction();
    SDLoc dl(Op);
    SDValue FR = DAG.getFrameIndex(
        MF.getInfo<IA64FunctionInfo>()->getVarArgsFrameIndex(),
        getPointerTy(DAG.getDataLayout()));
    const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
    return DAG.getStore(Op.getOperand(0), dl, FR, Op.getOperand(1),
                        MachinePointerInfo(SV));
  }
  }
}

SDValue IA64TargetLowering::LowerGlobalTLSAddress(SDValue Op,
                                                  SelectionDAG &DAG) const {
  GlobalAddressSDNode *GA = cast<GlobalAddressSDNode>(Op);
  const GlobalValue *GV = GA->getGlobal();
  SDLoc dl(Op);
  EVT PtrVT = getPointerTy(DAG.getDataLayout());

  // -femulated-tls is handled generically; otherwise emit native ELF TLS.
  if (DAG.getTarget().useEmulatedTLS())
    return LowerToTLSEmulatedModel(GA, DAG);

  // Read the thread pointer (tp / r13). It is reserved, so a CopyFromReg of the
  // physreg observes its live value; the per-model offset below is added to it.
  auto ThreadPointer = [&]() {
    return DAG.getCopyFromReg(DAG.getEntryNode(), dl, IA64::r13, PtrVT);
  };

  switch (getTargetMachine().getTLSModel(GV)) {
  case TLSModel::LocalExec: {
    // The offset is a static-link-time constant materialised directly (no GOT):
    //   movl rX = @tprel(sym) ;; add rX = rX, tp
    SDValue Sym =
        DAG.getTargetGlobalAddress(GV, dl, PtrVT, /*offset=*/0, IA64::S_TPREL);
    SDValue Off = DAG.getNode(IA64ISD::TLS_TPREL, dl, PtrVT, Sym);
    return DAG.getNode(ISD::ADD, dl, PtrVT, ThreadPointer(), Off);
  }
  case TLSModel::InitialExec: {
    // The offset is resolved by the dynamic linker into a GOT slot:
    //   addl rX = @ltoff(@tprel(sym)), gp ;; ld8 rX = [rX] ;; add rX = rX, tp
    SDValue Sym = DAG.getTargetGlobalAddress(GV, dl, PtrVT, /*offset=*/0,
                                             IA64::S_LTOFF_TPREL);
    SDValue Off = DAG.getNode(IA64ISD::TLS_GOTLOAD, dl, PtrVT, Sym);
    return DAG.getNode(ISD::ADD, dl, PtrVT, ThreadPointer(), Off);
  }
  case TLSModel::GeneralDynamic:
  case TLSModel::LocalDynamic: {
    // Call __tls_get_addr(module, offset): the two arguments are loaded from the
    // @ltoff(@dtpmod)/@ltoff(@dtprel) GOT slots, and the call returns the
    // variable's address. (Local-dynamic is lowered identically to
    // general-dynamic -- one call per access using the variable's own
    // dtpmod/dtprel -- which is correct, just without the LDM module-base
    // sharing optimization.) IA-64's __tls_get_addr takes the two scalars
    // directly (out0/out1), not a pointer to a tls_index struct.
    SDValue ModSym = DAG.getTargetGlobalAddress(GV, dl, PtrVT, /*offset=*/0,
                                                IA64::S_LTOFF_DTPMOD);
    SDValue OffSym = DAG.getTargetGlobalAddress(GV, dl, PtrVT, /*offset=*/0,
                                                IA64::S_LTOFF_DTPREL);
    SDValue Module = DAG.getNode(IA64ISD::TLS_GOTLOAD, dl, PtrVT, ModSym);
    SDValue Offset = DAG.getNode(IA64ISD::TLS_GOTLOAD, dl, PtrVT, OffSym);

    Type *I64Ty = Type::getInt64Ty(*DAG.getContext());
    ArgListTy Args;
    Args.push_back(ArgListEntry(Module, I64Ty));
    Args.push_back(ArgListEntry(Offset, I64Ty));

    // __tls_get_addr is an external symbol, so LowerCall emits a direct br.call
    // and (via AdjustInstrPostInstrSelection) models the gp clobber -> the gp
    // save/restore around the call survives, as GCC emits.
    TargetLowering::CallLoweringInfo CLI(DAG);
    CLI.setDebugLoc(dl)
        .setChain(DAG.getEntryNode())
        .setLibCallee(CallingConv::C, PointerType::getUnqual(*DAG.getContext()),
                      DAG.getExternalSymbol("__tls_get_addr", PtrVT),
                      std::move(Args));
    return LowerCallTo(CLI).first;
  }
  }
  llvm_unreachable("Unknown TLS model");
}

SDValue IA64TargetLowering::LowerReturn(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals, const SDLoc &dl,
    SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();

  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, isVarArg, MF, RVLocs, *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs, RetCC_IA64);

  SDValue Glue;
  SmallVector<SDValue, 4> RetOps(1, Chain); // RetOps[0] is patched below.

  // Copy the return values into their assigned registers (r8 / F8).
  for (unsigned i = 0, e = RVLocs.size(); i != e; ++i) {
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "return value must be in a register");
    SDValue Val = OutVals[i];

    if (VA.getLocVT() != VA.getValVT()) {
      if (VA.getLocVT().isInteger())
        Val = DAG.getNode(ISD::ANY_EXTEND, dl, VA.getLocVT(), Val);
      else
        Val = DAG.getNode(ISD::FP_EXTEND, dl, VA.getLocVT(), Val);
    }

    Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(), Val, Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  RetOps[0] = Chain;
  if (Glue.getNode())
    RetOps.push_back(Glue);

  return DAG.getNode(IA64ISD::RET_FLAG, dl, MVT::Other, RetOps);
}

void IA64TargetLowering::AdjustInstrPostInstrSelection(MachineInstr &MI,
                                                       SDNode * /*Node*/) const {
  unsigned Opc = MI.getOpcode();
  if (Opc != IA64::BRCALL_IPREL_GA && Opc != IA64::BRCALL_IPREL_ES)
    return;

  // gp (r1) is caller-saved at any call that is *not* provably local to this
  // load module: such a call may be resolved through an import stub that loads
  // the callee's own gp, and whether that happens is a static-vs-dynamic
  // linking decision we cannot see at compile time -- so we must conservatively
  // assume it does. Marking the call as defining r1 keeps the gp save/restore
  // LowerCall emits from being coalesced away (the same mechanism as rp/b0).
  //
  // A dso_local callee (e.g. a recursive self-call) keeps gp, so we leave it
  // alone and the redundant save/restore folds away -- no per-call gp churn.
  // (LTO could later prove more callees local and drop the clobber.)
  //
  // The call's only explicit operand (0) is the target: a GlobalAddress (direct
  // call to a known function) or an ExternalSymbol (always external).
  const MachineOperand &Target = MI.getOperand(0);
  bool IsLocal = Target.isGlobal() && Target.getGlobal()->isDSOLocal();
  if (!IsLocal)
    MI.addOperand(
        MachineOperand::CreateReg(IA64::r1, /*isDef=*/true, /*isImp=*/true));

  // A returns_twice callee on IA-64 cannot preserve the caller's stacked
  // register frame (r32-r127). The two cases that matter -- setjmp/longjmp and
  // vfork -- both leave the caller's stacked registers holding something other
  // than their call-time values: vfork in particular runs the child in the
  // parent's address space while the parent is blocked, so the child's use of
  // the shared register backing store overwrites the parent's stacked locals
  // (observed: an 'interp' argument parked in r32 reads back as 0 -- the value
  // the vfork child stored there -- in Tcl's TclpCreateProcess, freeing a
  // non-heap pointer). The static callee-saved registers r4-r7 are not in the
  // backing store and survive (the kernel restores them from the parent's saved
  // context); only the RSE-backed stacked registers are unsafe.
  //
  // The fixed BRCALL clobber list deliberately omits r32-r127 because an
  // ordinary call *does* preserve the caller's frame via the RSE. For a
  // returns_twice call we must additionally mark every stacked register clobbered
  // so the allocator keeps nothing live across the call there -- such values are
  // forced into r4-r7 or spilled to memory (which the child does not touch),
  // exactly as GCC's 'calls_setjmp' handling requires. This complements the
  // gp/sp/rp parking LowerCall already does for returns_twice functions.
  //
  // Express it as a regmask rather than 96 implicit-defs: implicit-def reg
  // operands make MachineRegisterInfo::isPhysRegUsed report every stacked
  // register as used, which IA64FrameLowering would then size the 'alloc' frame
  // around (ballooning it to the 96-register maximum). A regmask is tested
  // separately and is skipped by the frame-sizing scan (isPhysRegUsed's
  // SkipRegMaskTest), so it constrains the allocator without inflating the frame.
  const Function *Callee =
      Target.isGlobal() ? dyn_cast<Function>(Target.getGlobal()) : nullptr;
  if (Callee && Callee->hasFnAttribute(Attribute::ReturnsTwice)) {
    MachineFunction &MF = *MI.getMF();
    unsigned NumRegs = MF.getSubtarget().getRegisterInfo()->getNumRegs();
    uint32_t *Mask = MF.allocateRegMask();
    // A set bit means "preserved"; allocateRegMask zero-inits (clobber all), so
    // mark everything preserved and then clear just the stacked GPRs. The fixed
    // Defs above keep clobbering the caller-saved set on top of this mask.
    for (unsigned I = 0, E = MachineOperand::getRegMaskSize(NumRegs); I != E; ++I)
      Mask[I] = ~0u;
    // Register 0 is NoRegister, not a physical register: it must stay clobbered
    // (bit clear), or regmask consumers that expand preserved bits to reg units
    // (e.g. MachineCopyPropagation) assert iterating reg-units of reg 0.
    Mask[0] &= ~1u;
    for (unsigned I = 0; I != IA64NumStackedGPRs; ++I) {
      MCRegister R = getIA64StackedGPR(I);
      Mask[R.id() / 32] &= ~(1u << (R.id() % 32));
    }
    MI.addOperand(MachineOperand::CreateRegMask(Mask));
  }
}

//===----------------------------------------------------------------------===//
//                         Inline Assembly Support
//===----------------------------------------------------------------------===//

TargetLowering::ConstraintType
IA64TargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    default:
      break;
    case 'r': // general register
    case 'f': // floating-point register
      return C_RegisterClass;
    }
  }
  return TargetLowering::getConstraintType(Constraint);
}

std::pair<unsigned, const TargetRegisterClass *>
IA64TargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                                 StringRef Constraint,
                                                 MVT VT) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'r':
      // Any integer value (including the i1 a Rust bool / black_box produces)
      // lives in a general register. The GR class only carries i64, so the
      // generic exact-type search fails for the narrower types; map them here.
      if (VT.isInteger() || VT == MVT::Other)
        return std::make_pair(0U, &IA64::GRRegClass);
      break;
    case 'f':
      // f80 ('long double') is wider than the FP class's representative type
      // (f64), which makes the generic inline-asm register-tiling code assert.
      // Hand it the f80-only class so its register type is f80; f32/f64 are no
      // wider than the representative type and use the multi-typed FP class.
      if (VT == MVT::f80)
        return std::make_pair(0U, &IA64::FP80RegClass);
      if (VT == MVT::f32 || VT == MVT::f64)
        return std::make_pair(0U, &IA64::FPRegClass);
      break;
    }
  }
  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}
