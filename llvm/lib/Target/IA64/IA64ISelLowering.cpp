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
// saved around calls. Indirect / function-descriptor calls, >8 or FP/aggregate
// arguments, varargs and TLS remain deferred.
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

#define GET_CALLING_CONV_IMPL

#include "IA64GenCallingConv.inc"

IA64TargetLowering::IA64TargetLowering(const TargetMachine &TM,
                                       const TargetSubtargetInfo &STI)
    : TargetLowering(TM, STI) {
  // Register classes: general (i64), floating-point (f64) and predicate (i1).
  addRegisterClass(MVT::i64, &IA64::GRRegClass);
  addRegisterClass(MVT::f64, &IA64::FPRegClass);
  addRegisterClass(MVT::i1, &IA64::PRRegClass);

  // IA-64 uses SELECT, not SELECT_CC, and has no native BR_CC / jump tables.
  setOperationAction(ISD::BRIND, MVT::Other, Expand);
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

  setOperationAction(ISD::SINT_TO_FP, MVT::i1, Promote);
  setOperationAction(ISD::UINT_TO_FP, MVT::i1, Promote);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);

  setOperationAction(ISD::FREM, MVT::f32, Expand);
  setOperationAction(ISD::FREM, MVT::f64, Expand);

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

  // Use the default (library/expansion) implementations.
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
      else if (RegVT == MVT::f64)
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
      // The argument arrives on the stack, above the 16-byte scratch area.
      assert(VA.isMemLoc() && "unexpected argument location");
      int FI = MF.getFrameInfo().CreateFixedObject(
          8, 16 + VA.getLocMemOffset(), /*IsImmutable=*/true);
      SDValue FIN = DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
      InVals.push_back(
          DAG.getLoad(VA.getValVT(), dl, Chain, FIN, MachinePointerInfo()));
    }
  }

  // Materialise the PSEUDO_ALLOC at function entry. Frame lowering later scans
  // for it to size and place the real 'alloc'; LowerReturn reads the captured
  // vreg to restore ar.pfs before the branch.
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  Register VirtGPR = RegInfo.createVirtualRegister(&IA64::GRRegClass);
  MachineBasicBlock &EntryBB = MF.front();
  BuildMI(EntryBB, EntryBB.begin(), DebugLoc(), TII.get(IA64::PSEUDO_ALLOC),
          VirtGPR);
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

  // No tail calls or varargs yet (fib needs neither).
  CLI.IsTailCall = false;
  if (isVarArg)
    report_fatal_error("IA64: variadic calls are not yet supported");

  // Assign the outgoing arguments to out0-out7 / F8-F15 (caller convention).
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeCallOperands(Outs, CC_IA64_Call);

  // A 16-byte scratch area sits at the bottom of the outgoing frame; keep the
  // whole thing 16-byte aligned.
  unsigned NumBytes = (CCInfo.getStackSize() + 16 + 15) & ~15u;

  // Record how many output registers this call needs; the prologue 'alloc'
  // sizes its output region from the max over all of the function's calls.
  // (FP arguments that shadow an out slot are out of scope; fib passes only
  // integers, so this is just the argument count.)
  unsigned NumOutRegs = std::min<unsigned>(Outs.size(), 8);
  IA64FunctionInfo *FInfo = MF.getInfo<IA64FunctionInfo>();
  FInfo->OutRegsUsed = std::max(FInfo->OutRegsUsed, NumOutRegs);

  Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, dl);

  // Collect the (out-register, value) pairs to copy in just before the call.
  SmallVector<std::pair<unsigned, SDValue>, 8> RegsToPass;
  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];
    SDValue Arg = OutVals[i];

    switch (VA.getLocInfo()) {
    case CCValAssign::Full:
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

    if (!VA.isRegLoc())
      report_fatal_error("IA64: stack (>8) call arguments are not yet supported");
    RegsToPass.push_back(std::make_pair(VA.getLocReg(), Arg));
  }

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

  // Copy the outgoing arguments into their out registers, glued before the call.
  for (auto &R : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, dl, R.first, R.second, InGlue);
    InGlue = Chain.getValue(1);
  }

  // Make a direct callee a target node so the generic selector leaves it alone;
  // the IA64ISD::BRCALL selection consumes it as the br.call target. (Indirect
  // / function-descriptor calls are deferred.)
  if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee))
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), dl, MVT::i64);
  else if (ExternalSymbolSDNode *E = dyn_cast<ExternalSymbolSDNode>(Callee))
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(), MVT::i64);

  // Emit the call.
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  SmallVector<SDValue, 4> Ops = {Chain, Callee};
  if (InGlue.getNode())
    Ops.push_back(InGlue);
  Chain = DAG.getNode(IA64ISD::BRCALL, dl, NodeTys, Ops);
  InGlue = Chain.getValue(1);

  // Restore gp/sp/rp after the call.
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
