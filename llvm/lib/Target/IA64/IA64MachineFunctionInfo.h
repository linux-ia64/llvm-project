//===-- IA64MachineFunctionInfo.h - IA64 machine function info --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares IA64-specific per-machine-function information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_IA64MACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_IA64_IA64MACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

class IA64FunctionInfo : public MachineFunctionInfo {
  virtual void anchor();

  // The stacked local that emitPrologue makes 'alloc' write the incoming ar.pfs
  // into, and that emitEpilogue restores ar.pfs from before the return. Like
  // SavedRPReg below it is picked just above the locals the allocator used (so
  // the register stack engine preserves it across calls and it is never spilled)
  // and reserved by widening the 'alloc' frame. This gives the unwinder one
  // fixed location to name in a '.save ar.pfs, <reg>' directive.
  Register SavedPFSReg;

  // FrameIndex of the varargs register save area: the slot holding the first
  // variadic argument. LowerFormalArguments spills the unnamed incoming GP
  // registers here; LowerVASTART hands its address to va_start.
  int VarArgsFrameIndex = 0;

  // The stacked local that emitPrologue parks the incoming return pointer
  // (b0/rp) in, for a non-leaf function. It is picked just above the locals the
  // allocator used (so the register stack engine preserves it across calls for
  // free) and reserved by widening the 'alloc' frame; emitEpilogue restores b0
  // from it before the return. The unwinder gets one fixed location to name in a
  // '.save rp, <reg>' directive, which the asm printer reads off the FrameSetup
  // 'mov <reg> = rp'. Left null for a leaf function, which never clobbers b0.
  Register SavedRPReg;

public:
  // How many 'out' registers are used by this MachineFunction. Used to compute
  // the appropriate entry in the 'alloc' instruction at the top of the
  // function. Set during call lowering.
  unsigned OutRegsUsed = 0;

  IA64FunctionInfo(const Function &F, const TargetSubtargetInfo *STI) {}

  Register getSavedPFSReg() const { return SavedPFSReg; }
  void setSavedPFSReg(Register Reg) { SavedPFSReg = Reg; }

  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int FI) { VarArgsFrameIndex = FI; }

  Register getSavedRPReg() const { return SavedRPReg; }
  void setSavedRPReg(Register Reg) { SavedRPReg = Reg; }

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_IA64MACHINEFUNCTIONINFO_H
