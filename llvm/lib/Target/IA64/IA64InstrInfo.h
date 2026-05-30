//===- IA64InstrInfo.h - IA64 Instruction Information -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the IA64 implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_IA64INSTRINFO_H
#define LLVM_LIB_TARGET_IA64_IA64INSTRINFO_H

#include "IA64RegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "IA64GenInstrInfo.inc"

namespace llvm {

class IA64InstrInfo : public IA64GenInstrInfo {
  const IA64RegisterInfo RI;
  virtual void anchor();

public:
  // The pre-removal backend held the InstrInfo standalone; the modern
  // -gen-instr-info constructor needs the subtarget + register info. We take
  // the base TargetSubtargetInfo (rather than a not-yet-existing IA64Subtarget)
  // so this class can be built before the subtarget aggregate lands.
  explicit IA64InstrInfo(const TargetSubtargetInfo &STI);

  /// getRegisterInfo - TargetInstrInfo is a superset of MRegister info. As
  /// such, whenever a client has an instance of instruction info, it should
  /// always be able to get register info as well (through this method).
  const IA64RegisterInfo &getRegisterInfo() const { return RI; }

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                   const DebugLoc &DL, Register DestReg, Register SrcReg,
                   bool KillSrc, bool RenamableDest = false,
                   bool RenamableSrc = false) const override;

  void storeRegToStackSlot(
      MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI, Register SrcReg,
      bool isKill, int FrameIndex, const TargetRegisterClass *RC, Register VReg,
      MachineInstr::MIFlag Flags = MachineInstr::NoFlags) const override;

  void loadRegFromStackSlot(
      MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI, Register DestReg,
      int FrameIndex, const TargetRegisterClass *RC, Register VReg,
      unsigned SubReg = 0,
      MachineInstr::MIFlag Flags = MachineInstr::NoFlags) const override;

  bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify = false) const override;

  unsigned removeBranch(MachineBasicBlock &MBB,
                        int *BytesRemoved = nullptr) const override;

  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        const DebugLoc &DL,
                        int *BytesAdded = nullptr) const override;

  bool
  reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_IA64INSTRINFO_H
