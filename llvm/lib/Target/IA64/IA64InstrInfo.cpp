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
    if (IA64::PRRegClass.contains(SrcReg)) {
      // Predicate -> predicate: (SrcReg) DestReg = cmp.eq.unc(r0, r0). The .unc
      // form writes DestReg in both cases (1 when SrcReg holds, else 0).
      BuildMI(MBB, I, DL, get(IA64::PCMPEQUNC), DestReg)
          .addReg(IA64::r0)
          .addReg(IA64::r0)
          .addReg(SrcReg, getKillRegState(KillSrc));
    } else {
      // General register -> predicate: DestReg = (SrcReg != 0), the inverse of
      // the GR<-PR copy below. There is no 'mov PR = GR'.
      BuildMI(MBB, I, DL, get(IA64::CMPNE), DestReg)
          .addReg(SrcReg, getKillRegState(KillSrc))
          .addReg(IA64::r0);
    }
    return;
  }

  if (IA64::ARRegClass.contains(DestReg)) {
    // Restoring ar.pfs from a general register: 'mov ar.pfs = rN'. ar.pfs is
    // in its own register class, so the generic GR MOV below cannot name it.
    BuildMI(MBB, I, DL, get(IA64::MOV_TO_AR_PFS), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  if (IA64::BRRegClass.contains(DestReg)) {
    // Loading a branch register (b6) for an indirect call: 'mov b6 = rN'. Like
    // ar.pfs, b6 is in its own class, so the generic GR MOV below cannot name it.
    BuildMI(MBB, I, DL, get(IA64::MOV_TO_BR), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  if (IA64::GRRegClass.contains(DestReg) && IA64::PRRegClass.contains(SrcReg)) {
    // Reading a predicate into a general register: materialize its 0/1 boolean
    // value (there is no 'mov GR = PR'). DestReg = 0 ;; (SrcReg) DestReg = 1 --
    // the same zext-PR sequence used in the td. The tied TPCADDS adds 1 only
    // when the predicate holds.
    BuildMI(MBB, I, DL, get(IA64::ADDS), DestReg).addReg(IA64::r0).addImm(0);
    BuildMI(MBB, I, DL, get(IA64::TPCADDS), DestReg)
        .addReg(DestReg)
        .addImm(1)
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
  } else if (IA64::GRRegClass.hasSubClassEq(RC)) {
    // GR or a GR sub-class (e.g. GR03, the restricted r0-r3 ADDL-addend class):
    // any of them spills with a plain 8-byte store.
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
  } else if (IA64::GRRegClass.hasSubClassEq(RC)) {
    // GR or a GR sub-class (e.g. GR03): reload with a plain 8-byte load.
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

// The two branch forms our selector emits: 'BRL_NOTCALL' is the unconditional
// '(p0) brl.cond TBB' (operand 0 = target block); 'BRLCOND_NOTCALL' is the
// conditional '($qp) brl.cond TBB' (operand 0 = predicate, operand 1 = target).
static bool isUncondBranchOpcode(unsigned Opc) {
  return Opc == IA64::BRL_NOTCALL;
}
static bool isCondBranchOpcode(unsigned Opc) {
  return Opc == IA64::BRLCOND_NOTCALL;
}

// The branch condition for IA-64 is the single qualifying predicate register.
static void parseCondBranch(MachineInstr *LastInst, MachineBasicBlock *&TBB,
                            SmallVectorImpl<MachineOperand> &Cond) {
  Cond.push_back(LastInst->getOperand(0)); // the predicate
  TBB = LastInst->getOperand(1).getMBB();
}

bool IA64InstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                  MachineBasicBlock *&TBB,
                                  MachineBasicBlock *&FBB,
                                  SmallVectorImpl<MachineOperand> &Cond,
                                  bool AllowModify) const {
  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end())
    return false; // empty block, falls through

  if (!isUnpredicatedTerminator(*I))
    return false; // last instruction isn't a terminator, falls through

  MachineInstr *LastInst = &*I;
  unsigned LastOpc = LastInst->getOpcode();

  // Just one terminator.
  if (I == MBB.begin() || !isUnpredicatedTerminator(*--I)) {
    if (isUncondBranchOpcode(LastOpc)) {
      TBB = LastInst->getOperand(0).getMBB();
      return false;
    }
    if (isCondBranchOpcode(LastOpc)) {
      parseCondBranch(LastInst, TBB, Cond); // ends with fall-through cond branch
      return false;
    }
    return true; // some other terminator (e.g. indirect branch): can't analyze
  }

  MachineInstr *SecondLastInst = &*I;
  unsigned SecondLastOpc = SecondLastInst->getOpcode();

  // If the block ends with two or more unconditional branches, the trailing
  // ones are dead; drop them when allowed.
  if (AllowModify && isUncondBranchOpcode(LastOpc)) {
    while (isUncondBranchOpcode(SecondLastOpc)) {
      LastInst->eraseFromParent();
      LastInst = SecondLastInst;
      LastOpc = LastInst->getOpcode();
      if (I == MBB.begin() || !isUnpredicatedTerminator(*--I)) {
        TBB = LastInst->getOperand(0).getMBB();
        return false;
      }
      SecondLastInst = &*I;
      SecondLastOpc = SecondLastInst->getOpcode();
    }
  }

  // Three terminators: bail out.
  if (I != MBB.begin() && isUnpredicatedTerminator(*--I))
    return true;

  // Conditional branch to TBB followed by an unconditional branch to FBB.
  if (isCondBranchOpcode(SecondLastOpc) && isUncondBranchOpcode(LastOpc)) {
    parseCondBranch(SecondLastInst, TBB, Cond);
    FBB = LastInst->getOperand(0).getMBB();
    return false;
  }

  // Two unconditional branches: the second is unreachable.
  if (isUncondBranchOpcode(SecondLastOpc) && isUncondBranchOpcode(LastOpc)) {
    TBB = SecondLastInst->getOperand(0).getMBB();
    return false;
  }

  return true; // anything else: can't analyze
}

unsigned IA64InstrInfo::removeBranch(MachineBasicBlock &MBB,
                                     int *BytesRemoved) const {
  assert(!BytesRemoved && "code size not handled");
  MachineBasicBlock::iterator I = MBB.end();
  unsigned Count = 0;
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;
    if (!isCondBranchOpcode(I->getOpcode()) &&
        !isUncondBranchOpcode(I->getOpcode()))
      break; // not a branch
    I->eraseFromParent();
    I = MBB.end();
    ++Count;
  }
  return Count;
}

unsigned IA64InstrInfo::insertBranch(MachineBasicBlock &MBB,
                                     MachineBasicBlock *TBB,
                                     MachineBasicBlock *FBB,
                                     ArrayRef<MachineOperand> Cond,
                                     const DebugLoc &DL, int *BytesAdded) const {
  assert(!BytesAdded && "code size not handled");
  assert(TBB && "insertBranch must not be told to insert a fallthrough");
  assert(Cond.size() <= 1 &&
         "IA64 branch condition is a single qualifying predicate!");

  if (Cond.empty()) {
    // Unconditional branch.
    assert(!FBB && "Unconditional branch with multiple successors!");
    BuildMI(&MBB, DL, get(IA64::BRL_NOTCALL)).addMBB(TBB);
    return 1;
  }

  // Conditional branch '($qp) brl.cond TBB'.
  BuildMI(&MBB, DL, get(IA64::BRLCOND_NOTCALL)).add(Cond[0]).addMBB(TBB);
  if (!FBB)
    return 1;

  // Two-way branch: append the unconditional branch to the false target.
  BuildMI(&MBB, DL, get(IA64::BRL_NOTCALL)).addMBB(FBB);
  return 2;
}

bool IA64InstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  // The condition is a single qualifying predicate register. Its complement is
  // not available -- the CMP* instructions discard the complement predicate
  // (they write 'p0' for it) -- so the condition cannot be reversed in place.
  // Returning true signals "cannot reverse"; callers fall back accordingly
  // (e.g. they still remove a redundant fall-through branch, which needs no
  // reversal).
  return true;
}
