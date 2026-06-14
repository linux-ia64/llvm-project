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
#include "IA64RegisterInfo.h"
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
  unsigned NumStackedGPRsUsed = 0;
  for (unsigned i = 0; i != IA64NumStackedGPRs; ++i) {
    if (MF.getRegInfo().isPhysRegUsed(getIA64StackedGPR(i)))
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

  // For a non-leaf function, br.call overwrites the return pointer (b0/rp), so
  // we must preserve the caller's return address for our own br.ret. The
  // register allocator already does this lazily -- it copies rp into a stacked
  // local around each call -- but those copies land in a different register at
  // each call site, so there is no single location the unwinder can name. To
  // make the frame describable by one '.save rp, <reg>' unwind directive --
  // which, with '.save ar.pfs', is what lets gdb/libunwind walk past this frame
  // from anywhere in the body -- park rp once here, in a fresh stacked local
  // just above the ones the allocator used (a register stack engine local is
  // preserved across calls for free). emitEpilogue restores b0 from it.
  //
  // hasCalls() is the right test: it covers libcalls (e.g. the __divdi3 a sdiv
  // lowers to) that clobber rp without any IR-level call, which a check earlier
  // than frame lowering could not see.
  IA64FunctionInfo *FInfo = MF.getInfo<IA64FunctionInfo>();
  Register SavedRPReg;
  if (MFI.hasCalls()) {
    assert(NumStackedGPRsUsed < IA64NumStackedGPRs &&
           "no free stacked GPR for the rp save");
    SavedRPReg = getIA64StackedGPR(NumStackedGPRsUsed);
    // Reserve it as an extra local in the 'alloc' frame. The output registers
    // (out0-out7) are symbolic and follow the locals, so gas shifts them up by
    // one automatically; the absolutely-named locals below are unaffected.
    ++NumStackedGPRsUsed;
    FInfo->setSavedRPReg(SavedRPReg);
  }

  // 'alloc' must be the first instruction in the function. Tag it (and the rest
  // of the prologue below) as frame setup so the asm printer can hang the IA-64
  // unwind directives (.prologue / .save ar.pfs / .save rp / .fframe) off the
  // right instructions.
  BuildMI(MBB, MBBI, DL, TII->get(IA64::ALLOC))
      .addReg(DstRegOfPseudoAlloc)
      .addImm(0)
      .addImm(NumStackedGPRsUsed)
      .addImm(NumOutRegsUsed)
      .addImm(0)
      .setMIFlag(MachineInstr::FrameSetup);

  // Save the incoming return pointer into the parked local. Mark the local live
  // in every later block so the value is correctly seen as live across the whole
  // function (it is defined here and used in emitEpilogue, in another block).
  if (SavedRPReg) {
    BuildMI(MBB, MBBI, DL, TII->get(IA64::MOV), SavedRPReg)
        .addReg(IA64::rp)
        .setMIFlag(MachineInstr::FrameSetup);
    for (MachineBasicBlock &Block : MF)
      if (&Block != &MBB)
        Block.addLiveIn(SavedRPReg);
  }

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
        .addImm(-(int64_t)NumBytes)
        .setMIFlag(MachineInstr::FrameSetup);
  } else { // use r22 as a scratch register
    BuildMI(MBB, MBBI, DL, TII->get(IA64::MOVLIMM64), IA64::r22)
        .addImm(-(int64_t)NumBytes)
        .setMIFlag(MachineInstr::FrameSetup);
    BuildMI(MBB, MBBI, DL, TII->get(IA64::ADD), IA64::r12)
        .addReg(IA64::r12)
        .addReg(IA64::r22)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  // Now, if we need to, save the old FP and set the new one.
  if (FP) {
    BuildMI(MBB, MBBI, DL, TII->get(IA64::ST8))
        .addReg(IA64::r12)
        .addReg(IA64::r5)
        .setMIFlag(MachineInstr::FrameSetup);
    BuildMI(MBB, MBBI, DL, TII->get(IA64::MOV), IA64::r5)
        .addReg(IA64::r12)
        .setMIFlag(MachineInstr::FrameSetup);
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

  // Restore the incoming return pointer (b0/rp) from the local the prologue
  // parked it in, so our br.ret returns to the caller. This also keeps that
  // local live, anchoring the '.save rp' unwind region across the whole body.
  if (Register SavedRPReg = MF.getInfo<IA64FunctionInfo>()->getSavedRPReg())
    BuildMI(MBB, MBBI, DL, TII->get(IA64::MOV), IA64::rp)
        .addReg(SavedRPReg)
        .setMIFlag(MachineInstr::FrameDestroy);

  // If we need to, restore the old FP.
  if (FP) {
    // Copy the FP into the SP (discards allocas).
    BuildMI(MBB, MBBI, DL, TII->get(IA64::MOV), IA64::r12)
        .addReg(IA64::r5)
        .setMIFlag(MachineInstr::FrameDestroy);
    // Restore the FP.
    BuildMI(MBB, MBBI, DL, TII->get(IA64::LD8), IA64::r5)
        .addReg(IA64::r5)
        .setMIFlag(MachineInstr::FrameDestroy);
  }

  if (NumBytes != 0) {
    if (NumBytes <= 8191) {
      BuildMI(MBB, MBBI, DL, TII->get(IA64::ADDIMM22), IA64::r12)
          .addReg(IA64::r12)
          .addImm(NumBytes)
          .setMIFlag(MachineInstr::FrameDestroy);
    } else {
      BuildMI(MBB, MBBI, DL, TII->get(IA64::MOVLIMM64), IA64::r22)
          .addImm(NumBytes)
          .setMIFlag(MachineInstr::FrameDestroy);
      BuildMI(MBB, MBBI, DL, TII->get(IA64::ADD), IA64::r12)
          .addReg(IA64::r12)
          .addReg(IA64::r22)
          .setMIFlag(MachineInstr::FrameDestroy);
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
