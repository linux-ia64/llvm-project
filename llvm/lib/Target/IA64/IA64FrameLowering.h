//===-- IA64FrameLowering.h - Define frame lowering for IA64 ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class implements the IA64-specific bits of the TargetFrameLowering
// class. In the pre-removal backend this logic lived in IA64RegisterInfo;
// modern LLVM splits frame lowering into its own class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_IA64FRAMELOWERING_H
#define LLVM_LIB_TARGET_IA64_IA64FRAMELOWERING_H

#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/Support/Alignment.h"

namespace llvm {

class IA64FrameLowering : public TargetFrameLowering {
public:
  // StackRealignable=false: this backend does not dynamically realign the
  // stack. sp (r12) is only 16-byte aligned and the prologue never emits an
  // 'and sp, -N', so we cannot honor a local whose alignment exceeds 16 by
  // placing it at a static sp+offset slot. If we claimed otherwise (the
  // default is true), FunctionLoweringInfo would fold an over-aligned
  // (e.g. #[repr(align(64))]) alloca into the static frame; SelectionDAG's
  // computeKnownBits would then trust the frame-index pointer to be 64-aligned
  // and rewrite field GEPs 'add base, k' into 'or base, k' -- which collide
  // and corrupt fields once the runtime address is merely 16-aligned. With
  // this false, such allocas are instead demoted to variable-sized objects and
  // lowered via DYNAMIC_STACKALLOC (Expand emits 'sp -= size; sp &= -align'),
  // so the pointer is genuinely aligned and the 'or' rewrite is valid. The
  // demotion also sets hasVarSizedObjects(), which turns on hasFP so the
  // epilogue restores sp from the frame pointer.
  IA64FrameLowering()
      : TargetFrameLowering(StackGrowsDown, /*StackAlignment=*/Align(16),
                            /*LocalAreaOffset=*/0, /*TransientStackAlignment=*/
                            Align(16), /*StackRealignable=*/false) {}

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
