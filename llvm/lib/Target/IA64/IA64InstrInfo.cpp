//===- IA64InstrInfo.cpp - IA64 Instruction Information -------------------===//
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

#include "IA64InstrInfo.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "IA64GenInstrInfo.inc"

// Pin the vtable to this translation unit.
void IA64InstrInfo::anchor() {}

IA64InstrInfo::IA64InstrInfo(const TargetSubtargetInfo &STI)
    : IA64GenInstrInfo(STI, RI, IA64::ADJUSTCALLSTACKDOWN,
                       IA64::ADJUSTCALLSTACKUP),
      RI() {}

void IA64InstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I,
                                const DebugLoc &DL, Register DestReg,
                                Register SrcReg, bool KillSrc,
                                bool /*RenamableDest*/,
                                bool /*RenamableSrc*/) const {
  if (IA64::PRRegClass.contains(DestReg)) {
    // Copying a predicate: (SrcReg) DestReg = cmp.eq.unc(r0, r0).
    BuildMI(MBB, I, DL, get(IA64::PCMPEQUNC), DestReg)
        .addReg(IA64::r0)
        .addReg(IA64::r0)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  // Otherwise MOV works for both general and FP registers.
  BuildMI(MBB, I, DL, get(IA64::MOV), DestReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
}

void IA64InstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MI,
                                        Register SrcReg, bool isKill,
                                        int FrameIdx,
                                        const TargetRegisterClass *RC,
                                        Register /*VReg*/,
                                        MachineInstr::MIFlag /*Flags*/) const {
  DebugLoc DL;
  if (MI != MBB.end())
    DL = MI->getDebugLoc();

  if (RC == &IA64::FPRegClass) {
    BuildMI(MBB, MI, DL, get(IA64::STF_SPILL))
        .addFrameIndex(FrameIdx)
        .addReg(SrcReg, getKillRegState(isKill));
  } else if (RC == &IA64::GRRegClass) {
    BuildMI(MBB, MI, DL, get(IA64::ST8))
        .addFrameIndex(FrameIdx)
        .addReg(SrcReg, getKillRegState(isKill));
  } else if (RC == &IA64::PRRegClass) {
    // We use IA64::r2 as a temporary register for doing this hackery.
    // First we load 0:
    BuildMI(MBB, MI, DL, get(IA64::MOV), IA64::r2).addReg(IA64::r0);
    // Then conditionally add 1:
    BuildMI(MBB, MI, DL, get(IA64::CADDIMM22), IA64::r2)
        .addReg(IA64::r2)
        .addImm(1)
        .addReg(SrcReg, getKillRegState(isKill));
    // And then store it to the stack:
    BuildMI(MBB, MI, DL, get(IA64::ST8)).addFrameIndex(FrameIdx).addReg(IA64::r2);
  } else {
    llvm_unreachable("sorry, I don't know how to store this sort of reg "
                     "in the stack");
  }
}

void IA64InstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator MI,
                                         Register DestReg, int FrameIdx,
                                         const TargetRegisterClass *RC,
                                         Register /*VReg*/, unsigned /*SubReg*/,
                                         MachineInstr::MIFlag /*Flags*/) const {
  DebugLoc DL;
  if (MI != MBB.end())
    DL = MI->getDebugLoc();

  if (RC == &IA64::FPRegClass) {
    BuildMI(MBB, MI, DL, get(IA64::LDF_FILL), DestReg).addFrameIndex(FrameIdx);
  } else if (RC == &IA64::GRRegClass) {
    BuildMI(MBB, MI, DL, get(IA64::LD8), DestReg).addFrameIndex(FrameIdx);
  } else if (RC == &IA64::PRRegClass) {
    // First we load a byte from the stack into r2, our 'predicate hackery'
    // scratch reg.
    BuildMI(MBB, MI, DL, get(IA64::LD8), IA64::r2).addFrameIndex(FrameIdx);
    // Then we compare it to zero. If it _is_ zero, compare-not-equal to r0
    // gives us 0, which is what we want, so that's nice.
    BuildMI(MBB, MI, DL, get(IA64::CMPNE), DestReg)
        .addReg(IA64::r2)
        .addReg(IA64::r0);
  } else {
    llvm_unreachable("sorry, I don't know how to load this sort of reg "
                     "from the stack");
  }
}

unsigned IA64InstrInfo::insertBranch(MachineBasicBlock &MBB,
                                     MachineBasicBlock *TBB,
                                     MachineBasicBlock *FBB,
                                     ArrayRef<MachineOperand> Cond,
                                     const DebugLoc &DL, int *BytesAdded) const {
  assert(!BytesAdded && "code size not handled");
  // Can only insert uncond branches so far.
  assert(Cond.empty() && !FBB && TBB && "Can only handle uncond branches!");
  BuildMI(&MBB, DL, get(IA64::BRL_NOTCALL)).addMBB(TBB);
  return 1;
}
