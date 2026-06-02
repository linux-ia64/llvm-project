//===-- IA64FrameLowering.cpp - IA64 Frame Information --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the IA64 implementation of TargetFrameLowering.
//
//===----------------------------------------------------------------------===//

#include "IA64FrameLowering.h"
#include "IA64MachineFunctionInfo.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"

using namespace llvm;

// hasFP - Return true if the specified function should have a dedicated frame
// pointer register. (The pre-removal backend also forced this off
// -fomit-frame-pointer; that global is gone, so we only key off var-sized
// objects.)
bool IA64FrameLowering::hasFPImpl(const MachineFunction &MF) const {
  return MF.getFrameInfo().hasVarSizedObjects();
}

void IA64FrameLowering::emitPrologue(MachineFunction &MF,
                                     MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.begin();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  bool FP = hasFP(MF);
  DebugLoc DL;

  // First, handle the 'alloc' instruction, which must be at the top of any
  // function. There are 96 stacked GPRs the RSE worries about.
  static const unsigned RegsInOrder[96] = {
      IA64::r32,  IA64::r33,  IA64::r34,  IA64::r35,  IA64::r36,  IA64::r37,
      IA64::r38,  IA64::r39,  IA64::r40,  IA64::r41,  IA64::r42,  IA64::r43,
      IA64::r44,  IA64::r45,  IA64::r46,  IA64::r47,  IA64::r48,  IA64::r49,
      IA64::r50,  IA64::r51,  IA64::r52,  IA64::r53,  IA64::r54,  IA64::r55,
      IA64::r56,  IA64::r57,  IA64::r58,  IA64::r59,  IA64::r60,  IA64::r61,
      IA64::r62,  IA64::r63,  IA64::r64,  IA64::r65,  IA64::r66,  IA64::r67,
      IA64::r68,  IA64::r69,  IA64::r70,  IA64::r71,  IA64::r72,  IA64::r73,
      IA64::r74,  IA64::r75,  IA64::r76,  IA64::r77,  IA64::r78,  IA64::r79,
      IA64::r80,  IA64::r81,  IA64::r82,  IA64::r83,  IA64::r84,  IA64::r85,
      IA64::r86,  IA64::r87,  IA64::r88,  IA64::r89,  IA64::r90,  IA64::r91,
      IA64::r92,  IA64::r93,  IA64::r94,  IA64::r95,  IA64::r96,  IA64::r97,
      IA64::r98,  IA64::r99,  IA64::r100, IA64::r101, IA64::r102, IA64::r103,
      IA64::r104, IA64::r105, IA64::r106, IA64::r107, IA64::r108, IA64::r109,
      IA64::r110, IA64::r111, IA64::r112, IA64::r113, IA64::r114, IA64::r115,
      IA64::r116, IA64::r117, IA64::r118, IA64::r119, IA64::r120, IA64::r121,
      IA64::r122, IA64::r123, IA64::r124, IA64::r125, IA64::r126, IA64::r127};

  unsigned NumStackedGPRsUsed = 0;
  for (int i = 0; i != 96; ++i) {
    if (MF.getRegInfo().isPhysRegUsed(RegsInOrder[i]))
      NumStackedGPRsUsed = i + 1; // i+1, not ++ - consider fn(fp, fp, int)
  }

  unsigned NumOutRegsUsed = MF.getInfo<IA64FunctionInfo>()->OutRegsUsed;

  // Find the PSEUDO_ALLOC to learn which register receives ar.pfs.
  Register DstRegOfPseudoAlloc;
  for (MachineInstr &MI : MBB) {
    if (MI.getOpcode() == IA64::PSEUDO_ALLOC) {
      DstRegOfPseudoAlloc = MI.getOperand(0).getReg();
      DL = MI.getDebugLoc();
      break;
    }
  }
  assert(DstRegOfPseudoAlloc && "no PSEUDO_ALLOC in entry block");

  // 'alloc' must be the first instruction in the function
  BuildMI(MBB, MBBI, DL, TII->get(IA64::ALLOC))
      .addReg(DstRegOfPseudoAlloc)
      .addImm(0)
      .addImm(NumStackedGPRsUsed)
      .addImm(NumOutRegsUsed)
      .addImm(0);

  // Get the number of bytes to allocate from the FrameInfo.
  unsigned NumBytes = MFI.getStackSize();

  if (FP)
    NumBytes += 8; // reserve space for the old FP

  // Do we need to allocate space on the stack?
  if (NumBytes == 0)
    return;

  // Add 16 bytes at the bottom of the stack (scratch area) and round the size
  // to a multiple of the alignment.
  unsigned Align = getStackAlign().value();
  unsigned Size = 16 + (FP ? 8 : 0);
  NumBytes = (NumBytes + Size + Align - 1) / Align * Align;
  MFI.setStackSize(NumBytes);

  // Adjust the stack pointer: r12 -= NumBytes.
  if (NumBytes <= 8191) {
    BuildMI(MBB, MBBI, DL, TII->get(IA64::ADDIMM22), IA64::r12)
        .addReg(IA64::r12)
        .addImm(-(int64_t)NumBytes);
  } else { // use r22 as a scratch register
    BuildMI(MBB, MBBI, DL, TII->get(IA64::MOVLIMM64), IA64::r22)
        .addImm(-(int64_t)NumBytes);
    BuildMI(MBB, MBBI, DL, TII->get(IA64::ADD), IA64::r12)
        .addReg(IA64::r12)
        .addReg(IA64::r22);
  }

  // Now, if we need to, save the old FP and set the new one.
  if (FP) {
    BuildMI(MBB, MBBI, DL, TII->get(IA64::ST8))
        .addReg(IA64::r12)
        .addReg(IA64::r5);
    BuildMI(MBB, MBBI, DL, TII->get(IA64::MOV), IA64::r5).addReg(IA64::r12);
  }
}

void IA64FrameLowering::emitEpilogue(MachineFunction &MF,
                                     MachineBasicBlock &MBB) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  MachineBasicBlock::iterator MBBI = std::prev(MBB.end());
  assert(MBBI->getOpcode() == IA64::RET &&
         "Can only insert epilog into returning blocks");
  DebugLoc DL = MBBI->getDebugLoc();
  bool FP = hasFP(MF);

  unsigned NumBytes = MFI.getStackSize();

  // If we need to, restore the old FP.
  if (FP) {
    // Copy the FP into the SP (discards allocas).
    BuildMI(MBB, MBBI, DL, TII->get(IA64::MOV), IA64::r12).addReg(IA64::r5);
    // Restore the FP.
    BuildMI(MBB, MBBI, DL, TII->get(IA64::LD8), IA64::r5).addReg(IA64::r5);
  }

  if (NumBytes != 0) {
    if (NumBytes <= 8191) {
      BuildMI(MBB, MBBI, DL, TII->get(IA64::ADDIMM22), IA64::r12)
          .addReg(IA64::r12)
          .addImm(NumBytes);
    } else {
      BuildMI(MBB, MBBI, DL, TII->get(IA64::MOVLIMM64), IA64::r22)
          .addImm(NumBytes);
      BuildMI(MBB, MBBI, DL, TII->get(IA64::ADD), IA64::r12)
          .addReg(IA64::r12)
          .addReg(IA64::r22);
    }
  }
}

MachineBasicBlock::iterator IA64FrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();

  if (hasFP(MF)) {
    // If we have a frame pointer, turn the adjcallstackup instruction into a
    // 'sub sp, <amt>' and the adjcallstackdown instruction into 'add sp, <amt>'.
    MachineInstr &Old = *I;
    unsigned Amount = Old.getOperand(0).getImm();
    DebugLoc DL = Old.getDebugLoc();
    if (Amount != 0) {
      // Keep the stack aligned: round up to the next alignment boundary.
      unsigned Align = getStackAlign().value();
      Amount = (Amount + Align - 1) / Align * Align;

      if (Old.getOpcode() == IA64::ADJUSTCALLSTACKDOWN) {
        BuildMI(MBB, I, DL, TII->get(IA64::ADDIMM22), IA64::r12)
            .addReg(IA64::r12)
            .addImm(-(int64_t)Amount);
      } else {
        assert(Old.getOpcode() == IA64::ADJUSTCALLSTACKUP);
        BuildMI(MBB, I, DL, TII->get(IA64::ADDIMM22), IA64::r12)
            .addReg(IA64::r12)
            .addImm(Amount);
      }
    }
  }

  return MBB.erase(I);
}
