//===- IA64RegisterInfo.cpp - IA64 Register Information ---------*- C++ -*-===//
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

#include "IA64RegisterInfo.h"
#include "IA64FrameLowering.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"

#define GET_REGINFO_TARGET_DESC
#include "IA64GenRegisterInfo.inc"

using namespace llvm;

// rp (the return pointer, branch register b0) is the return-address register.
IA64RegisterInfo::IA64RegisterInfo() : IA64GenRegisterInfo(IA64::rp) {}

const MCPhysReg *
IA64RegisterInfo::getCalleeSavedRegs(const MachineFunction * /*MF*/) const {
  static const MCPhysReg CalleeSavedRegs[] = {IA64::r5, 0};
  return CalleeSavedRegs;
}

BitVector IA64RegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  Reserved.set(IA64::r0);  // always zero
  Reserved.set(IA64::r1);  // global data pointer (gp)
  Reserved.set(IA64::r2);  // reserved for spilling/filling predicates
  Reserved.set(IA64::r5);  // frame pointer
  Reserved.set(IA64::r12); // stack pointer (sp)
  Reserved.set(IA64::r13); // thread pointer (tp)
  Reserved.set(IA64::r22); // reserved as an address-calculation scratch
  Reserved.set(IA64::rp);  // return pointer (b0)
  return Reserved;
}

bool IA64RegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                           int SPAdj, unsigned FIOperandNum,
                                           RegScavenger * /*RS*/) const {
  assert(SPAdj == 0 && "Unexpected");

  MachineInstr &MI = *II;
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  DebugLoc DL = MI.getDebugLoc();

  bool FP = TFI->hasFP(MF);

  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();

  // Choose a base register: ( hasFP ? frame pointer : stack pointer ).
  unsigned BaseRegister = FP ? IA64::r5 : IA64::r12;

  // Add the frame object offset to the offset from the base register.
  int Offset = MF.getFrameInfo().getObjectOffset(FrameIndex);
  Offset += MF.getFrameInfo().getStackSize();

  // We use 'r22' as an address-calculation scratch register here.
  MI.getOperand(FIOperandNum).ChangeToRegister(IA64::r22, false);
  if (Offset <= 8191 && Offset >= -8192) { // smallish offset
    BuildMI(MBB, II, DL, TII->get(IA64::ADDIMM22), IA64::r22)
        .addReg(BaseRegister)
        .addImm(Offset);
  } else { // it's big
    BuildMI(MBB, II, DL, TII->get(IA64::MOVLIMM64), IA64::r22).addImm(Offset);
    BuildMI(MBB, II, DL, TII->get(IA64::ADD), IA64::r22)
        .addReg(BaseRegister)
        .addReg(IA64::r22);
  }

  return false;
}

Register IA64RegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  return TFI->hasFP(MF) ? IA64::r5 : IA64::r12;
}
