//===-- IA64TargetMachine.cpp - Define TargetMachine for IA64 -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Phase A scaffold.
//
// LLVM's build system treats every target in LLVM_TARGETS_TO_BUILD as a
// complete backend: the generated Targets.def emits LLVM_TARGET(IA64), so
// InitializeAllTargets() references LLVMInitializeIA64Target(). This stub
// provides that entry point (lives in LLVMIA64CodeGen, which codegen tools
// link via AllTargetsCodeGens) so the target links and appears in
// `llc --version`. It is fleshed out in Phase D: LLVMInitializeIA64Target()
// registers the real IA64TargetMachine, TargetLowering and the pass
// configuration.
//
// Note: the companion LLVMInitializeIA64TargetMC() entry point lives in
// MCTargetDesc/ (LLVMIA64Desc), because object-file tools such as llvm-ar link
// AllTargetsDescs (not AllTargetsCodeGens) and reference it from there.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/Compiler.h"

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeIA64Target() {
  // TODO(Phase D): RegisterTargetMachine<IA64TargetMachine>, register the
  // TargetLowering and the IA64PassConfig.
}
