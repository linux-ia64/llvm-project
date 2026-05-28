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

#include "llvm/CodeGen/MachineJumpTableInfo.h"
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
  RET_FLAG,

  /// TLS_TPREL - local-exec thread-pointer-relative offset of a thread-local
  /// symbol. Its single operand is a TargetGlobalAddress tagged S_TPREL;
  /// selected to 'movl rX = @tprel(sym)'.
  TLS_TPREL,

  /// TLS_GOTLOAD - a value loaded from the symbol's GOT slot. Its single
  /// operand is a TargetGlobalAddress whose target flags carry the @ltoff(...)
  /// specifier (S_LTOFF_TPREL / S_LTOFF_DTPMOD / S_LTOFF_DTPREL); selected to
  /// 'addl rX = <spec>, gp ;; ld8 rX = [rX]', the GlobalAddress GOT sequence.
  TLS_GOTLOAD
};
} // end namespace IA64ISD

class IA64TargetLowering : public TargetLowering {
public:
  explicit IA64TargetLowering(const TargetMachine &TM,
                              const TargetSubtargetInfo &STI);

  const char *getTargetNodeName(unsigned Opcode) const override;

  /// Jump-table entries are absolute code pointers (data8 <label>), loaded with
  /// a plain LD8 and branched to via BRIND -- the simplest path, and it avoids
  /// the 32-bit label-difference entries (which would need a sext-load).
  unsigned getJumpTableEncoding() const override {
    return MachineJumpTableInfo::EK_BlockAddress;
  }

  /// The entries above are absolute, so BR_JT must branch straight to the loaded
  /// entry -- it must NOT add the table base back. The default keys this off
  /// isPositionIndependent() (true here, since the ABI is PIC), which would make
  /// the expansion compute base+entry and jump to garbage; force it off.
  bool isJumpTableRelative() const override { return false; }

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

  /// IA-64 ld/st carry no implicit ordering, so acquire/release/seq_cst
  /// atomics need explicit barriers. Returning true makes AtomicExpand bracket
  /// stronger-than-monotonic atomic accesses with fences (which we select to
  /// 'mf') and demote the access itself to monotonic -- and a monotonic,
  /// aligned <=8-byte access is just a plain ld/st on the hardware (lowered as
  /// such in LowerOperation).
  bool shouldInsertFencesForAtomic(const Instruction *I) const override {
    return true;
  }

  /// The only atomic read-modify-write IA-64 has a single instruction for is
  /// fetchadd (and only for a few immediates), so lower every atomicrmw
  /// (add/sub/and/or/xor/nand/min/max/xchg/...) to a cmpxchg loop in IR. That
  /// reduces all of them to the one primitive the backend selects natively,
  /// ISD::ATOMIC_CMP_SWAP (cmpxchg{1,2,4,8}). Correct, not yet optimized.
  AtomicExpansionKind
  shouldExpandAtomicRMWInIR(AtomicRMWInst *RMW) const override {
    return AtomicExpansionKind::CmpXChg;
  }

  /// Lower a thread-local address access (ISD::GlobalTLSAddress) per the model
  /// TargetMachine::getTLSModel picks: local-exec / initial-exec materialise a
  /// tp-relative offset and add tp (r13); general/local-dynamic call
  /// __tls_get_addr(module, offset).
  SDValue LowerGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) const;

  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &dl,
                      SelectionDAG &DAG) const override;

  /// Mark a call to a non-local callee as clobbering gp (r1), so the gp
  /// save/restore that LowerCall emits survives coalescing. Local (dso_local)
  /// callees keep gp and are left alone.
  void AdjustInstrPostInstrSelection(MachineInstr &MI,
                                     SDNode *Node) const override;

  /// Inline-asm support. We recognise the GCC IA-64 register constraints 'r'
  /// (general register) and 'f' (floating-point register); everything else
  /// falls back to the generic handling.
  ConstraintType getConstraintType(StringRef Constraint) const override;

  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_IA64ISELLOWERING_H
