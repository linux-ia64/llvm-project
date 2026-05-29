//===-- IA64TargetMachine.h - Define TargetMachine for IA64 ---*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the IA64 specific subclass of TargetMachine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_IA64TARGETMACHINE_H
#define LLVM_LIB_TARGET_IA64_IA64TARGETMACHINE_H

#include "IA64Subtarget.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/CodeGen/CodeGenTargetMachineImpl.h"
#include "llvm/Target/TargetMachine.h"
#include <memory>
#include <optional>

namespace llvm {

class IA64TargetMachine : public CodeGenTargetMachineImpl {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  mutable StringMap<std::unique_ptr<IA64Subtarget>> SubtargetMap;

public:
  IA64TargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                    StringRef FS, const TargetOptions &Options,
                    std::optional<Reloc::Model> RM,
                    std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                    bool JIT);
  ~IA64TargetMachine() override;

  const IA64Subtarget *getSubtargetImpl(const Function &F) const override;

  // Pass Pipeline Configuration.
  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;

  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }

  MachineFunctionInfo *
  createMachineFunctionInfo(BumpPtrAllocator &Allocator, const Function &F,
                            const TargetSubtargetInfo *STI) const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_IA64TARGETMACHINE_H
