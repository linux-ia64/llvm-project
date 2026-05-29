//===-- IA64MachineFunctionInfo.cpp - IA64 machine function info ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IA64MachineFunctionInfo.h"

using namespace llvm;

void IA64FunctionInfo::anchor() {}

MachineFunctionInfo *IA64FunctionInfo::clone(
    BumpPtrAllocator &Allocator, MachineFunction &DestMF,
    const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB) const {
  return DestMF.cloneInfo<IA64FunctionInfo>(*this);
}
