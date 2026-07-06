//===-- IA64FrameLowering.h - Define frame lowering for IA64 ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class implements the IA64-specific bits of the TargetFrameLowering
// class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_IA64FRAMELOWERING_H
#define LLVM_LIB_TARGET_IA64_IA64FRAMELOWERING_H

#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/Support/Alignment.h"

namespace llvm {

class IA64FrameLowering : public TargetFrameLowering {
public:
  // This backend does not dynamically realign the stack: sp (r12) is only
  // 16-byte aligned and the prologue never emits an 'and sp, -N'.
  // Set StackAlignment=false to prevent overaligned allocas from being
  // allocated statically, which would lead to field GEP lowering emitting
  // incorrect index calculation.
  IA64FrameLowering()
      : TargetFrameLowering(StackGrowsDown, /*StackAlignment=*/Align(16),
                            /*LocalAreaOffset=*/0, /*TransientStackAlignment=*/
                            Align(16), /*StackRealignable=*/false) {}

  // Override emitPrologue/emitEpilogue to implement RSE (Register Stack Engine)
  // setup and saving/restoring ar.pfs/bp/rp.
  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I) const override;

protected:
  bool hasFPImpl(const MachineFunction &MF) const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_IA64FRAMELOWERING_H
