//===-- IA64ISelLowering.h - IA64 DAG Lowering Interface --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that IA64 uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_IA64ISELLOWERING_H
#define LLVM_LIB_TARGET_IA64_IA64ISELLOWERING_H

#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {

class MachineInstr;
class TargetSubtargetInfo;

namespace IA64ISD {
enum NodeType : unsigned {
  // Start the numbering where the builtin ops and target ops leave off.
  FIRST_NUMBER = ISD::BUILTIN_OP_END,

  /// GETFD - the getf.d instruction takes a floating point operand and
  /// returns its 64-bit memory representation as an i64.
  GETFD,

  /// BRCALL - the call hack (see the pre-removal backend).
  BRCALL,

  /// RET_FLAG - Return with a flag operand.
  RET_FLAG
};
} // end namespace IA64ISD

class IA64TargetLowering : public TargetLowering {
public:
  explicit IA64TargetLowering(const TargetMachine &TM,
                              const TargetSubtargetInfo &STI);

  const char *getTargetNodeName(unsigned Opcode) const override;

  /// IA-64 has a single-rounding fused multiply-add (fma/fms/fnma), so a*b+c
  /// is cheaper (and more accurate) fused. Returning true makes llvm.fmuladd
  /// (clang's default -ffp-contract=on form of a*b+c) lower to ISD::FMA.
  bool isFMAFasterThanFMulAndFAdd(const MachineFunction &MF,
                                  EVT VT) const override;

  /// getSetCCResultType - SETCC produces a predicate (i1) on IA-64.
  EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Context,
                         EVT VT) const override;

  /// isFPImmLegal - return true for all FP immediates so the legalizer keeps
  /// them as ConstantFP nodes (which we materialise from their integer bit
  /// pattern via movl + setf.d) rather than emitting a constant-pool load,
  /// which this backend does not lower.
  bool isFPImmLegal(const APFloat &Imm, EVT VT,
                    bool ForCodeSize) const override;

  SDValue
  LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                       const SmallVectorImpl<ISD::InputArg> &Ins,
                       const SDLoc &dl, SelectionDAG &DAG,
                       SmallVectorImpl<SDValue> &InVals) const override;

  SDValue LowerCall(TargetLowering::CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;

  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;

  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &dl,
                      SelectionDAG &DAG) const override;

  /// Mark a call to a non-local callee as clobbering gp (r1), so the gp
  /// save/restore that LowerCall emits survives coalescing. Local (dso_local)
  /// callees keep gp and are left alone.
  void AdjustInstrPostInstrSelection(MachineInstr &MI,
                                     SDNode *Node) const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_IA64ISELLOWERING_H
