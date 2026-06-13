//===-- IA64TargetMachine.cpp - Define TargetMachine for IA64 -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the IA64 specific subclass of TargetMachine. It is the
// capstone that aggregates the subtarget and wires up the codegen pass pipeline
// (instruction selection + the bundling pre-emit pass), and registers the
// target machine so `llc -mtriple=ia64` can allocate one.
//
// The companion LLVMInitializeIA64TargetMC() lives in MCTargetDesc/.
//
//===----------------------------------------------------------------------===//

#include "IA64TargetMachine.h"
#include "IA64.h"
#include "IA64MachineFunctionInfo.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "TargetInfo/IA64TargetInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include <optional>

using namespace llvm;

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeIA64Target() {
  // Register the target machine, so `llc -mtriple=ia64` can allocate one.
  RegisterTargetMachine<IA64TargetMachine> X(getTheIA64Target());

  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeIA64DAGToDAGISelLegacyPass(PR);
}

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::Static);
}

IA64TargetMachine::IA64TargetMachine(const Target &T, const Triple &TT,
                                     StringRef CPU, StringRef FS,
                                     const TargetOptions &Options,
                                     std::optional<Reloc::Model> RM,
                                     std::optional<CodeModel::Model> CM,
                                     CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, TT.computeDataLayout(), TT, CPU, FS, Options,
                               getEffectiveRelocModel(RM),
                               getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()) {
  initAsmInfo();
}

IA64TargetMachine::~IA64TargetMachine() = default;

const IA64Subtarget *
IA64TargetMachine::getSubtargetImpl(const Function &F) const {
  Attribute CPUAttr = F.getFnAttribute("target-cpu");
  Attribute TuneAttr = F.getFnAttribute("tune-cpu");
  Attribute FSAttr = F.getFnAttribute("target-features");

  std::string CPU =
      CPUAttr.isValid() ? CPUAttr.getValueAsString().str() : TargetCPU;
  std::string TuneCPU =
      TuneAttr.isValid() ? TuneAttr.getValueAsString().str() : CPU;
  std::string FS =
      FSAttr.isValid() ? FSAttr.getValueAsString().str() : TargetFS;

  auto &I = SubtargetMap[CPU + FS];
  if (!I) {
    I = std::make_unique<IA64Subtarget>(getTargetTriple(), CPU, TuneCPU, FS,
                                        *this);
  }
  return I.get();
}

MachineFunctionInfo *IA64TargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return IA64FunctionInfo::create<IA64FunctionInfo>(Allocator, F, STI);
}

//===----------------------------------------------------------------------===//
// Pass Pipeline Configuration
//===----------------------------------------------------------------------===//

namespace {
// Hoist PSEUDO_ALLOC to the front of the entry block, ahead of the formal-arg
// live-in copies. Frame lowering's real 'alloc' writes the ar.pfs-save register
// at function entry, so RA must see that register defined there too; otherwise
// it reuses it across the leading copies/spills and clobbers the saved ar.pfs.
struct IA64AllocHoist : public MachineFunctionPass {
  static char ID;
  IA64AllocHoist() : MachineFunctionPass(ID) {}
  StringRef getPassName() const override { return "IA64 PSEUDO_ALLOC hoisting"; }
  bool runOnMachineFunction(MachineFunction &MF) override {
    MachineBasicBlock &EntryMBB = MF.front();
    for (MachineInstr &MI : EntryMBB)
      if (MI.getOpcode() == IA64::PSEUDO_ALLOC) {
        if (&MI == &EntryMBB.front())
          return false;
        EntryMBB.splice(EntryMBB.begin(), &EntryMBB, MI.getIterator());
        return true;
      }
    return false;
  }
};
char IA64AllocHoist::ID = 0;

class IA64PassConfig : public TargetPassConfig {
public:
  IA64PassConfig(IA64TargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  IA64TargetMachine &getIA64TargetMachine() const {
    return getTM<IA64TargetMachine>();
  }

  void addIRPasses() override;
  bool addInstSelector() override;
  void addPreRegAlloc() override;
  void addPreEmitPass() override;
};
} // end anonymous namespace

TargetPassConfig *IA64TargetMachine::createPassConfig(PassManagerBase &PM) {
  return new IA64PassConfig(*this, PM);
}

void IA64PassConfig::addIRPasses() {
  // Expand atomics the backend cannot select directly: turn every atomicrmw
  // into a cmpxchg loop (shouldExpandAtomicRMWInIR) and bracket stronger-than-
  // monotonic atomics with fences (shouldInsertFencesForAtomic). This is no
  // longer part of the target-independent addIRPasses, so each target adds it
  // (cf. SparcPassConfig); without it atomicrmw reaches isel as AtomicLoadAdd
  // etc. and fails to select, and ordering fences are never inserted.
  addPass(createAtomicExpandLegacyPass());

  TargetPassConfig::addIRPasses();
}

bool IA64PassConfig::addInstSelector() {
  addPass(createIA64ISelDag(getIA64TargetMachine()));
  return false;
}

void IA64PassConfig::addPreRegAlloc() {
  // Make PSEUDO_ALLOC the first instruction so the ar.pfs-save register is live
  // from function entry (see IA64AllocHoist above).
  addPass(new IA64AllocHoist());
}

void IA64PassConfig::addPreEmitPass() {
  // Insert stop bits so the assembler can bundle correctly.
  addPass(createIA64BundlingPass());
}
