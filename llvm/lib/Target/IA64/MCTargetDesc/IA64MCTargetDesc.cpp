//===-- IA64MCTargetDesc.cpp - IA64 Target Descriptions -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Phase A scaffold.
//
// The generated Targets.def emits LLVM_TARGET(IA64), so InitializeAllTargetMCs()
// references LLVMInitializeIA64TargetMC(). By convention this entry point lives
// in the target's "Desc" library (LLVMIA64Desc), which object-file tools such
// as llvm-ar link via AllTargetsDescs. This stub provides it so those tools
// link cleanly.
//
// Phase B fleshes this out to register MCAsmInfo, MCInstrInfo, MCRegisterInfo,
// MCSubtargetInfo and the MCInstPrinter.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/Compiler.h"

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeIA64TargetMC() {
  // TODO(Phase B): register MCAsmInfo (replacing the old TargetAsmInfo),
  // MCInstrInfo, MCRegisterInfo, MCSubtargetInfo and the MCInstPrinter.
}
