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

public:
  // How many 'out' registers are used by this MachineFunction. Used to compute
  // the appropriate entry in the 'alloc' instruction at the top of the
  // function. Set during call lowering.
  unsigned OutRegsUsed = 0;

  IA64FunctionInfo(const Function &F, const TargetSubtargetInfo *STI) {}

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_IA64MACHINEFUNCTIONINFO_H
