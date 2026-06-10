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
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

// Variadic floating-point arguments are passed in the *general* registers, not
// F8-F15: a prototyped variadic callee (e.g. printf) reads its variable
// arguments out of the integer parameter slots / register save area, never the
// FP registers (IA-64 SysV psABI 8.5.4). So an FP value matching the '...' is
// bit-cast to its i64 IEEE representation (getf.d, the BCvt below) and assigned
// to the next out register by *slot* via AllocateReg -- which also sidesteps the
// fixed-arg FP-index shadow mapping. Fixed FP args fall through (return true) to
// the normal F8-F15+shadow rule. Hooked from CC_IA64_Call (CCCustom).
static bool CC_IA64_Call_VarArgFP(unsigned ValNo, MVT ValVT, MVT LocVT,
                                  CCValAssign::LocInfo LocInfo,
                                  ISD::ArgFlagsTy ArgFlags, CCState &State) {
  // The CCCustom wrapper stops at this rule when we return true ("handled") and
  // falls through to the next rule when we return false.
  if (!ArgFlags.isVarArg())
    return false; // a fixed FP arg: let the F8-F15 + GR-shadow rule handle it
  static const MCPhysReg OutRegs[] = {IA64::out0, IA64::out1, IA64::out2,
                                      IA64::out3, IA64::out4, IA64::out5,
                                      IA64::out6, IA64::out7};
  if (unsigned Reg = State.AllocateReg(OutRegs))
    State.addLoc(
        CCValAssign::getReg(ValNo, ValVT, Reg, MVT::i64, CCValAssign::BCvt));
  else
    State.addLoc(CCValAssign::getMem(ValNo, ValVT, State.AllocateStack(8, Align(8)),
                                     MVT::i64, CCValAssign::BCvt));
  return true; // handled
}

#define GET_CALLING_CONV_IMPL

#include "IA64GenCallingConv.inc"

IA64TargetLowering::IA64TargetLowering(const TargetMachine &TM,
                                       const TargetSubtargetInfo &STI)
    : TargetLowering(TM, STI) {
  // Register classes: general (i64), floating-point (f32/f64) and predicate
  // (i1).
  addRegisterClass(MVT::i64, &IA64::GRRegClass);
  addRegisterClass(MVT::f32, &IA64::FPRegClass);
  addRegisterClass(MVT::f64, &IA64::FPRegClass);
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
  // FP (f64) compares/branches stay deferred -- no FCMP patterns yet.
  setOperationAction(ISD::BR_CC, MVT::i64, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::i64, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::f64, Expand);

  // FP compares: keep brcond(setcc f64) from folding into an unselectable
  // br_cc, so the legalizer hands us setcc + brcond. setcc f64 selects to the
  // fcmp relations (FCMP* in IA64InstrInfo.td), which cover every clang FP
  // condition except SETONE/SETUEQ; expand those into a pair joined by the i1
  // and/or patterns.
  setOperationAction(ISD::BR_CC, MVT::f64, Expand);
  setCondCodeAction(ISD::SETONE, MVT::f64, Expand);
  setCondCodeAction(ISD::SETUEQ, MVT::f64, Expand);

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
  setOperationAction(ISD::FDIV, MVT::f64, Expand);

  // f32 is a hardware type (held in the FP registers), but we model no separate
  // single-precision arithmetic path: promote f32 arithmetic to f64 and round
  // the result with fnorm.s (FP_ROUND).
  for (unsigned Op : {ISD::FADD, ISD::FSUB, ISD::FMUL, ISD::FDIV})
    setOperationAction(Op, MVT::f32, Promote);

  // We don't support sin/cos/sqrt/pow.
  for (MVT VT : {MVT::f32, MVT::f64}) {
    setOperationAction(ISD::FSIN, VT, Expand);
    setOperationAction(ISD::FCOS, VT, Expand);
    setOperationAction(ISD::FSQRT, VT, Expand);
    setOperationAction(ISD::FPOW, VT, Expand);
    // FIXME: IA64 supports fcopysign natively.
    setOperationAction(ISD::FCOPYSIGN, VT, Expand);
  }

  // The legalizer expansion of ctlz/cttz in terms of ctpop is large; expand.
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

  // va_start points the va_list at the register save area (custom); va_arg,
  // va_copy and va_end use the generic load/increment/store expansion. The
  // va_list is a plain pointer, so the default va_copy/va_end suffice.
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::VAARG, MVT::Other, Expand);
  setOperationAction(ISD::VACOPY, MVT::Other, Expand);
  setOperationAction(ISD::VAEND, MVT::Other, Expand);
  setOperationAction(ISD::STACKSAVE, MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Expand);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i64, Expand);

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
  // fma/fms/fnma fuse a*b+c into one single-rounding F-unit op. Only f64: the
  // FMA patterns are f64 and f32 FMA isn't promoted, so claiming it for f32
  // would form an unselectable f32 fma node (f32 a*b+c stays fmul+fadd).
  return VT == MVT::f64;
}

bool IA64TargetLowering::isFPImmLegal(const APFloat & /*Imm*/, EVT VT,
                                      bool /*ForCodeSize*/) const {
  // Keep f32/f64 constants out of the constant pool: we materialise them from
  // their integer bit pattern (movl + setf.d) -- see the fpimm patterns in
  // IA64InstrInfo.td. (There is no constant-pool selection in this backend.)
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

  // The physical registers the incoming arguments arrive in. The PSEUDO_ALLOC
  // below is made to "use" these (with an early-clobber result), so the register
  // allocator cannot place the ar.pfs save into a live argument register --
  // 'alloc' reconfigures the frame those registers occupy, and its destination
  // must not alias one of them. Without this, the coalescer can shorten an arg's
  // live range to end before the PSEUDO_ALLOC, letting ar.pfs reuse e.g. r32 and
  // clobber the incoming argument.
  SmallVector<Register, 8> ArgPhysRegs;

  for (CCValAssign &VA : ArgLocs) {
    if (VA.isRegLoc()) {
      // The argument arrives in a register.
      MVT RegVT = VA.getLocVT();
      const TargetRegisterClass *RC;
      if (RegVT == MVT::i64)
        RC = &IA64::GRRegClass;
      else if (RegVT == MVT::f64)
        RC = &IA64::FPRegClass;
      else
        report_fatal_error("IA64: unhandled formal-argument register type");

      Register VReg = RegInfo.createVirtualRegister(RC);
      RegInfo.addLiveIn(VA.getLocReg(), VReg);
      if (Ins[VA.getValNo()].Used)
        ArgPhysRegs.push_back(VA.getLocReg());
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
      // The argument arrives on the stack, above the 16-byte scratch area. In a
      // variadic function the eight parameter-slot register homes (64 bytes)
      // are reserved just below the stack arguments so va_arg can walk the
      // whole variadic list contiguously (see the spill loop below), so the
      // stack arguments start 64 bytes higher.
      assert(VA.isMemLoc() && "unexpected argument location");
      int FI = MF.getFrameInfo().CreateFixedObject(
          8, 16 + (isVarArg ? 64 : 0) + VA.getLocMemOffset(),
          /*IsImmutable=*/true);
      SDValue FIN = DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
      InVals.push_back(
          DAG.getLoad(VA.getValVT(), dl, Chain, FIN, MachinePointerInfo()));
    }
  }

  // Variadic functions: spill the unnamed incoming GP registers to their
  // parameter-slot memory homes so va_start/va_arg can walk the variadic
  // arguments as a single contiguous in-memory image. Slot i homes at offset
  // 16 + 8*i, i.e. the eight homes occupy [16, 80); the stack-passed varargs
  // start at 16 + 64 = 80 (the +64 reservation above), directly above the last
  // register home. A va_list is just an ascending pointer, so it walks out of
  // the register homes straight into the stack arguments. (Storing through r39
  // also marks it used, so frame lowering's 'alloc' keeps all eight incoming GP
  // registers live as locals.)
  if (isVarArg) {
    static const MCPhysReg ArgGPRs[] = {IA64::r32, IA64::r33, IA64::r34,
                                        IA64::r35, IA64::r36, IA64::r37,
                                        IA64::r38, IA64::r39};
    unsigned FirstVar = CCInfo.getFirstUnallocated(ArgGPRs);
    MachineFrameInfo &MFI = MF.getFrameInfo();
    int VAFI = 0;
    SmallVector<SDValue, 8> Stores;
    for (unsigned i = FirstVar; i < 8; ++i) {
      int FI = MFI.CreateFixedObject(8, 16 + 8 * i, /*IsImmutable=*/false);
      if (i == FirstVar)
        VAFI = FI; // va_start points at the first unnamed slot's home
      Register VReg = RegInfo.createVirtualRegister(&IA64::GRRegClass);
      RegInfo.addLiveIn(ArgGPRs[i], VReg);
      // Protect this incoming register from the ar.pfs save: the 'alloc' that
      // defines it runs before these spills, so it must not land on r32-r39
      // (see ArgPhysRegs / PSEUDO_ALLOC below).
      ArgPhysRegs.push_back(ArgGPRs[i]);
      SDValue Val = DAG.getCopyFromReg(Chain, dl, VReg, MVT::i64);
      SDValue Addr = DAG.getFrameIndex(FI, MVT::i64);
      Stores.push_back(DAG.getStore(Val.getValue(1), dl, Val, Addr,
                                    MachinePointerInfo::getFixedStack(MF, FI)));
    }
    // All eight GP slots named: no register varargs, so va_start points at the
    // start of the stack varargs (offset 16 + 64).
    if (FirstVar == 8)
      VAFI = MFI.CreateFixedObject(8, 16 + 64, /*IsImmutable=*/true);
    MF.getInfo<IA64FunctionInfo>()->setVarArgsFrameIndex(VAFI);
    if (!Stores.empty())
      Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Stores);
  }

  // Materialise the PSEUDO_ALLOC at function entry. Frame lowering later scans
  // for it to size and place the real 'alloc'; LowerReturn reads the captured
  // vreg to restore ar.pfs before the branch. The result is marked early-clobber
  // and the instruction is given the incoming argument registers as implicit
  // uses, so the allocator keeps the ar.pfs save off any live argument register
  // (see ArgPhysRegs above).
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  Register VirtGPR = RegInfo.createVirtualRegister(&IA64::GRRegClass);
  MachineBasicBlock &EntryBB = MF.front();
  MachineInstrBuilder MIB =
      BuildMI(EntryBB, EntryBB.begin(), DebugLoc(), TII.get(IA64::PSEUDO_ALLOC))
          .addReg(VirtGPR, RegState::Define | RegState::EarlyClobber);
  for (Register ArgReg : ArgPhysRegs)
    MIB.addReg(ArgReg, RegState::Implicit);
  MF.getInfo<IA64FunctionInfo>()->setVirtGPR(VirtGPR);

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
  // whole thing 16-byte aligned. A variadic call also reserves the 64-byte
  // parameter-slot register-home area below the stack arguments, into which the
  // callee spills its unnamed register args to form a contiguous va_list image
  // (see LowerFormalArguments); the stack arguments therefore start 64 bytes
  // higher (handled at the store below).
  unsigned NumBytes =
      (CCInfo.getStackSize() + 16 + (isVarArg ? 64 : 0) + 15) & ~15u;

  // Record how many output registers this call needs; the prologue 'alloc'
  // sizes its output region from the max over all of the function's calls.
  // (FP arguments that shadow an out slot are out of scope; fib passes only
  // integers, so this is just the argument count.)
  unsigned NumOutRegs = std::min<unsigned>(Outs.size(), 8);
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
    SDValue Arg = OutVals[i];

    switch (VA.getLocInfo()) {
    case CCValAssign::Full:
      break;
    case CCValAssign::BCvt:
      // A variadic FP arg routed into a GR slot: reinterpret the f64 as its
      // i64 IEEE bit pattern (selects to getf.d). See CC_IA64_Call_VarArgFP.
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
      // the 16-byte scratch area (plus, for a variadic call, the 64-byte
      // register-home reservation) -- the same layout LowerFormalArguments reads
      // incoming stack arguments from. The store is sp-relative: with a reserved
      // call frame (no variable-sized objects) sp is constant here; otherwise
      // the call-frame pseudos adjust it around the call.
      assert(VA.isMemLoc() && "argument neither in register nor on the stack");
      unsigned Off = 16 + (isVarArg ? 64 : 0) + VA.getLocMemOffset();
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

  // Save gp/sp/rp around the call. rp (b0) is the hard requirement -- br.call
  // overwrites it, so a non-leaf function must preserve its own return pointer;
  // gp/sp are saved conservatively. These reads must precede the call and the
  // restores must follow it, so the whole save -> args -> call -> restore
  // sequence is tied together with glue (note: rp is deliberately *not* in the
  // BRCALL clobber list, so glue, not the clobber set, is what orders it). The
  // save vregs are live across the call and therefore land in RSE locals.
  // Use the glue-carrying getCopyFromReg overload even for the first save (with
  // a null input glue): it still gives the node a glue *result* to start the
  // chain. The plain 4-operand form has no glue result, so reading getValue(2)
  // off it would be out of range.
  SDValue InGlue;
  SDValue GPSave = DAG.getCopyFromReg(Chain, dl, IA64::r1, MVT::i64, InGlue);
  Chain = GPSave.getValue(1);
  InGlue = GPSave.getValue(2);
  SDValue SPSave = DAG.getCopyFromReg(Chain, dl, IA64::r12, MVT::i64, InGlue);
  Chain = SPSave.getValue(1);
  InGlue = SPSave.getValue(2);
  SDValue RPSave = DAG.getCopyFromReg(Chain, dl, IA64::rp, MVT::i64, InGlue);
  Chain = RPSave.getValue(1);
  InGlue = RPSave.getValue(2);

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
  // parked values. The restore below reads them back out of r4/r6/r7.
  bool ReturnsTwice = MF.exposesReturnsTwice();
  if (ReturnsTwice) {
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

  // Restore gp/sp/rp after the call. For a returns_twice function read them back
  // out of r4/r6/r7 (longjmp-safe, see the save above); otherwise from the save
  // vregs directly.
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
  Chain = DAG.getCopyToReg(Chain, dl, IA64::rp, RPSave, InGlue);
  InGlue = Chain.getValue(1);

  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, InGlue, dl);
  InGlue = Chain.getValue(1);

  // Read the return value(s) out of r8 / F8, narrowing back to the declared type.
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

SDValue IA64TargetLowering::LowerReturn(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals, const SDLoc &dl,
    SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();

  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, isVarArg, MF, RVLocs, *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs, RetCC_IA64);

  // Read back the ar.pfs value saved into a vreg at function entry.
  Register VirtGPR = MF.getInfo<IA64FunctionInfo>()->getVirtGPR();
  SDValue ARPFS = DAG.getCopyFromReg(Chain, dl, VirtGPR, MVT::i64);
  Chain = ARPFS.getValue(1);

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

  // Restore ar.pfs immediately before the return, glued into it. Like the
  // return-value registers above, ar.pfs must also be added to RetOps so the
  // (SDNPVariadic) RET node carries it as an implicit use; otherwise the copy
  // — and the PSEUDO_ALLOC feeding it — are eliminated as dead.
  Chain = DAG.getCopyToReg(Chain, dl, IA64::AR_PFS, ARPFS, Glue);
  Glue = Chain.getValue(1);
  RetOps.push_back(DAG.getRegister(IA64::AR_PFS, MVT::i64));

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
}
