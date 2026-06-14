//===- IA64RegisterInfo.h - IA64 Register Information Impl ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the IA64 implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_IA64REGISTERINFO_H
#define LLVM_LIB_TARGET_IA64_IA64REGISTERINFO_H

#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "IA64GenRegisterInfo.inc"

namespace llvm {

// Number of stacked general registers (r32-r127) the register stack engine
// manages; 'alloc' carves its inputs/locals/outputs from this window.
static constexpr unsigned IA64NumStackedGPRs = 96;

// The stacked general register at register-stack index Idx: 0 -> r32, ...,
// 95 -> r127. The GR enum values are not contiguous (other register classes are
// interleaved), so this maps an index through an explicit table.
MCRegister getIA64StackedGPR(unsigned Idx);

struct IA64RegisterInfo : public IA64GenRegisterInfo {
  IA64RegisterInfo();

  // Code Generation virtual methods.
  const MCPhysReg *
  getCalleeSavedRegs(const MachineFunction *MF = nullptr) const override;

  BitVector getReservedRegs(const MachineFunction &MF) const override;

  bool eliminateFrameIndex(MachineBasicBlock::iterator II, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;

  Register getFrameRegister(const MachineFunction &MF) const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_IA64REGISTERINFO_H
