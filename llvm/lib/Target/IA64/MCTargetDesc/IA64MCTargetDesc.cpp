//===-- IA64MCTargetDesc.cpp - IA64 Target Descriptions -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides IA64 specific target descriptions.
//
// The generated Targets.def emits LLVM_TARGET(IA64), so InitializeAllTargetMCs()
// references LLVMInitializeIA64TargetMC(). By convention this entry point lives
// in the target's "Desc" library (LLVMIA64Desc), which object-file tools such
// as llvm-ar link via AllTargetsDescs.
//
// Phase B registers the MCAsmInfo (the modern replacement for the pre-removal
// IA64TargetAsmInfo). The MCInstrInfo, MCRegisterInfo, MCSubtargetInfo and
// MCInstPrinter registrations are deferred to Phase C: they consume the
// IA64Gen*.inc tables, which only exist once the .td files are ported.
//
//===----------------------------------------------------------------------===//

#include "IA64MCAsmInfo.h"
#include "TargetInfo/IA64TargetInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

static MCAsmInfo *createIA64MCAsmInfo(const MCRegisterInfo &MRI,
                                      const Triple &TT,
                                      const MCTargetOptions &Options) {
  return new IA64MCAsmInfo(TT, Options);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeIA64TargetMC() {
  Target &T = getTheIA64Target();

  // Register the MC asm info (replaces the pre-removal IA64TargetAsmInfo /
  // ELFTargetAsmInfo).
  RegisterMCAsmInfoFn X(T, createIA64MCAsmInfo);

  // TODO(Phase C): once IA64.td and friends generate the MC tables, also
  // register the remaining MC components here:
  //   TargetRegistry::RegisterMCInstrInfo(T, createIA64MCInstrInfo);
  //   TargetRegistry::RegisterMCRegInfo(T, createIA64MCRegisterInfo);
  //   TargetRegistry::RegisterMCSubtargetInfo(T, createIA64MCSubtargetInfo);
  //   TargetRegistry::RegisterMCInstPrinter(T, createIA64MCInstPrinter);
}
