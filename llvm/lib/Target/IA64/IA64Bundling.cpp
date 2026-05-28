//===-- IA64Bundling.cpp - IA-64 instruction bundling pass. --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Add stops (;;) where required to prevent read-after-write and write-after-
// write dependencies, for registers. (The pre-removal pass noted exceptions for
// parallel compares targeting p0; those are not reintroduced here.)
//
// FIXME: actual bundle formation is left to the assembler; this only inserts
// stop bits.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "llvm/ADT/SetOperations.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include <set>

using namespace llvm;

#define DEBUG_TYPE "ia64-bundling"

STATISTIC(StopBitsAdded, "Number of stop bits added");

namespace {
struct IA64BundlingPass : public MachineFunctionPass {
  static char ID;

  IA64BundlingPass() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "IA64 (Itanium) Bundling Pass";
  }

  bool runOnMachineBasicBlock(MachineBasicBlock &MBB);

  bool runOnMachineFunction(MachineFunction &F) override {
    TII = F.getSubtarget().getInstrInfo();
    RSEWrite = false;
    bool Changed = false;
    for (MachineBasicBlock &MBB : F)
      Changed |= runOnMachineBasicBlock(MBB);
    return Changed;
  }

private:
  const TargetInstrInfo *TII = nullptr;

  // Ugly carried state, but pending writes can cross basic blocks. Taken
  // branches end instruction groups, so only fallthrough code matters.
  std::set<unsigned> PendingRegWrites;

  // Likewise carried across blocks: an alloc writes the RSE/CFM and must be
  // separated from a later call by a stop. The alloc commonly lives in the entry
  // block while the first call sits in a fall-through successor (e.g. alloc in
  // the prologue, first call in the next block), so a per-block flag would lose
  // the pending alloc at the block boundary and skip the required stop. Reset
  // only at function entry and when a stop is emitted below.
  bool RSEWrite = false;
};
char IA64BundlingPass::ID = 0;
} // end anonymous namespace

/// createIA64BundlingPass - Returns a pass that adds STOP (;;) instructions
/// where inter-instruction register dependencies require them.
FunctionPass *llvm::createIA64BundlingPass() { return new IA64BundlingPass(); }

bool IA64BundlingPass::runOnMachineBasicBlock(MachineBasicBlock &MBB) {
  bool Changed = false;

  for (MachineBasicBlock::iterator I = MBB.begin(); I != MBB.end();) {
    MachineInstr &MI = *I;
    ++I;

    std::set<unsigned> CurrentReads, CurrentWrites, OrigWrites;
    for (const MachineOperand &MO : MI.operands()) {
      if (!MO.isReg())
        continue;
      if (MO.isUse()) // TODO: exclude p0
        CurrentReads.insert(MO.getReg());
      if (MO.isDef()) { // TODO: exclude p0
        CurrentWrites.insert(MO.getReg());
        OrigWrites.insert(MO.getReg());
      }
    }

    // Does this instruction read or write any register that is pending a
    // write (i.e. not yet separated from its writer by a stop)?
    set_intersect(CurrentReads, PendingRegWrites);
    set_intersect(CurrentWrites, PendingRegWrites);

    if ((RSEWrite && MI.isCall()) ||
        !(CurrentReads.empty() && CurrentWrites.empty())) {
      // Conflict (or the forced stop after an alloc): insert a stop before this
      // instruction and reset the pending set to this instruction's writes.
      BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(IA64::STOP));
      PendingRegWrites = OrigWrites;
      Changed = true;
      RSEWrite = false;
      ++StopBitsAdded;
    } else {
      // No conflict: accumulate this instruction's writes.
      set_union(PendingRegWrites, OrigWrites);
    }

    // An alloc writes into the RSE and has to be separated from calls
    if (MI.getOpcode() == IA64::ALLOC)
      RSEWrite = true;
  }

  return Changed;
}
