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
    // SkipRegMaskTest: count a stacked register only if it is really allocated
    // to a value here, not merely clobbered by a call's regmask. A returns_twice
    // (vfork/setjmp) call carries a regmask clobbering all of r32-r127 (see
    // IA64TargetLowering::AdjustInstrPostInstrSelection) to keep values out of
    // the RSE-backed frame across it; without skipping the mask that would size
    // this 'alloc' to the full 96-register frame.
    if (MF.getRegInfo().isPhysRegUsed(getIA64StackedGPR(i),
                                      /*SkipRegMaskTest=*/true))
      NumStackedGPRsUsed = i + 1; // i+1, not ++ - consider fn(fp, fp, int)
  }

  unsigned NumOutRegsUsed = MF.getInfo<IA64FunctionInfo>()->OutRegsUsed;

  IA64FunctionInfo *FInfo = MF.getInfo<IA64FunctionInfo>();

  // Park the caller's ar.pfs in a fixed stacked local for the whole function.
  // 'alloc' writes the incoming ar.pfs into its destination register, and every
  // function must restore that value before br.ret so the register stack engine
  // can recover the caller's frame. Make the destination a fresh stacked local
  // just above the ones the allocator used: a register stack engine local is
  // preserved across calls for free, and because the allocator never sees this
  // register it is never spilled -- so the value stays in one place that the
  // unwinder can name in a single '.save ar.pfs, <reg>' directive valid for the
  // entire body.
  //
  // The old backend instead let the allocator place the ar.pfs-save value (via
  // PSEUDO_ALLOC). In a non-leaf function the allocator spilled that value to a
  // stack slot across calls and reused the register, so '.save ar.pfs, <reg>'
  // named a register that no longer held ar.pfs at the call sites. That was
  // invisible to gdb's read-only backtrace (which only needs the return address
  // from '.save rp') but crashed libgcc's forced unwinder (pthread_exit /
  // pthread_cancel), which must actually restore ar.pfs to pop the RSE frame.
  Register SavedPFSReg = getIA64StackedGPR(NumStackedGPRsUsed);
  ++NumStackedGPRsUsed;
  FInfo->setSavedPFSReg(SavedPFSReg);

  // For a non-leaf function, br.call overwrites the return pointer (b0/rp), so
  // we must preserve the caller's return address for our own br.ret. The
  // register allocator already does this lazily -- it copies rp into a stacked
  // local around each call -- but those copies land in a different register at
  // each call site, so there is no single location the unwinder can name. Park
  // rp once here, in a fresh stacked local (just like ar.pfs above), so the
  // frame is describable by one '.save rp, <reg>' directive. emitEpilogue
  // restores b0 from it.
  //
  // hasCalls() is the right test: it covers libcalls (e.g. the __divdi3 a sdiv
  // lowers to) that clobber rp without any IR-level call, which a check earlier
  // than frame lowering could not see.
  Register SavedRPReg;
  if (MFI.hasCalls()) {
    SavedRPReg = getIA64StackedGPR(NumStackedGPRsUsed);
    ++NumStackedGPRsUsed;
    FInfo->setSavedRPReg(SavedRPReg);
  }

  // The whole stacked frame -- locals (the allocator's plus our ar.pfs/rp saves)
  // and the outputs (out0-out7, placed by gas above the locals) -- must fit in
  // the 96-register window. getReservedRegs guarantees this by capping the
  // allocator's locals: it reserves the top 10 stacked GPRs (8 outputs + the rp
  // save + the ar.pfs save).
  assert(NumStackedGPRsUsed + NumOutRegsUsed <= IA64NumStackedGPRs &&
         "stacked-GPR frame overflow: locals + saves + outputs > 96");

  // 'alloc' must be the first instruction in the function; its destination is
  // the parked ar.pfs local. Mark that operand as a Define: 'alloc' writes the
  // caller's ar.pfs into it, and the bundling pass needs to see that write so it
  // inserts the mandatory stop before any instruction that reads the register --
  // notably the epilogue's 'mov ar.pfs = <reg>'. (Using 'alloc's result, or any
  // register it renames, in the same instruction group is illegal and faults
  // with SIGILL.) The old backend's PSEUDO_ALLOC supplied this def; without it,
  // a use of the addReg default would leave 'alloc' looking like a reader.
  //
  // Tag it (and the rest of the prologue below) as frame setup so the asm
  // printer can hang the IA-64 unwind directives (.prologue / .save ar.pfs /
  // .save rp / .fframe) off the right instructions.
  BuildMI(MBB, MBBI, DL, TII->get(IA64::ALLOC))
      .addReg(SavedPFSReg, RegState::Define)
      .addImm(0)
      .addImm(NumStackedGPRsUsed)
      .addImm(NumOutRegsUsed)
      .addImm(0)
      .setMIFlag(MachineInstr::FrameSetup);

  // The ar.pfs local is defined by 'alloc' here and used by emitEpilogue's
  // restore in another block; mark it live across the whole function so its
  // value is correctly seen as live everywhere.
  for (MachineBasicBlock &Block : MF)
    if (&Block != &MBB)
      Block.addLiveIn(SavedPFSReg);

  // Save the incoming return pointer into its parked local, and likewise mark it
  // live across the function.
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

  // Restore the caller's ar.pfs from the local 'alloc' parked it in, so our
  // br.ret lets the register stack engine recover the caller's frame. Every
  // function has this save (see emitPrologue), so the register is always valid.
  // Keeping it live here anchors the '.save ar.pfs' unwind region across the
  // whole body.
  Register SavedPFSReg = MF.getInfo<IA64FunctionInfo>()->getSavedPFSReg();
  BuildMI(MBB, MBBI, DL, TII->get(IA64::MOV_TO_AR_PFS), IA64::AR_PFS)
      .addReg(SavedPFSReg)
      .setMIFlag(MachineInstr::FrameDestroy);

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
