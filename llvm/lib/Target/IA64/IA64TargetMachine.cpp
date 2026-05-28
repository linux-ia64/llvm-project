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
#include "IA64RegisterInfo.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "TargetInfo/IA64TargetInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/IR/Function.h"
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
    // This needs to happen before the subtarget is created, since the latter
    // depends on the code-generation flags on the function.
    resetTargetOptions(F);
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
// Rewrite the symbolic output registers out0-out7 in debug values to the real
// stacked register they alias. gas resolves 'out0' to r(32+inputs+locals) from
// the 'alloc', but the .td gives out0-out7 the fixed DwarfRegNum 120-127 (=
// physical r120-r127), so a variable that lives in an output register at some PC
// -- e.g. a parameter already moved into place for a call -- would be read by
// gdb from the wrong register (seen as a bogus '0x0' in test_gdb.test_pretty_
// print). The actual stacked register has the correct DwarfRegNum, so map to it.
// Runs in addPreEmitPass2, after LiveDebugValues has finalized the debug values.
struct IA64FixupDebugOutRegs : public MachineFunctionPass {
  static char ID;
  IA64FixupDebugOutRegs() : MachineFunctionPass(ID) {}
  StringRef getPassName() const override {
    return "IA64 debug output-register fixup";
  }
  bool runOnMachineFunction(MachineFunction &MF) override {
    if (!MF.getFunction().getSubprogram())
      return false; // no debug info -> no debug values to fix

    static const MCPhysReg OutRegs[8] = {
        IA64::out0, IA64::out1, IA64::out2, IA64::out3,
        IA64::out4, IA64::out5, IA64::out6, IA64::out7};

    // out_i is the stacked register just above the input+local region the
    // 'alloc' sized: index (inputs + locals + i). alloc operands are
    // dst, inputs, locals, outputs, rotating.
    unsigned Base = 0;
    bool FoundAlloc = false;
    for (MachineInstr &MI : MF.front())
      if (MI.getOpcode() == IA64::ALLOC) {
        Base = MI.getOperand(1).getImm() + MI.getOperand(2).getImm();
        FoundAlloc = true;
        break;
      }
    if (!FoundAlloc)
      return false;

    bool Changed = false;
    for (MachineBasicBlock &MBB : MF)
      for (MachineInstr &MI : MBB) {
        if (!MI.isDebugValue())
          continue;
        for (MachineOperand &MO : MI.debug_operands()) {
          if (!MO.isReg() || !MO.getReg())
            continue;
          for (unsigned i = 0; i != 8; ++i)
            if (MO.getReg() == OutRegs[i] &&
                Base + i < IA64NumStackedGPRs) {
              MO.setReg(getIA64StackedGPR(Base + i));
              Changed = true;
              break;
            }
        }
      }
    return Changed;
  }
};
char IA64FixupDebugOutRegs::ID = 0;

class IA64PassConfig : public TargetPassConfig {
public:
  IA64PassConfig(IA64TargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  IA64TargetMachine &getIA64TargetMachine() const {
    return getTM<IA64TargetMachine>();
  }

  void addIRPasses() override;
  bool addInstSelector() override;
  void addPreEmitPass() override;
  void addPreEmitPass2() override;
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

void IA64PassConfig::addPreEmitPass() {
  // Insert stop bits so the assembler can bundle correctly.
  addPass(createIA64BundlingPass());
}

void IA64PassConfig::addPreEmitPass2() {
  // Fix up out0-out7 in debug values now that LiveDebugValues has run and the
  // debug locations are final (see IA64FixupDebugOutRegs).
  addPass(new IA64FixupDebugOutRegs());
}
