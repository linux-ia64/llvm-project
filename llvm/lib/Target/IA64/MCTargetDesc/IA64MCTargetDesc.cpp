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
// This registers the full MC component set for the asm-output path: MCAsmInfo
// (replacing the pre-removal IA64TargetAsmInfo), MCInstrInfo (the instruction
// table also backs IA64InstrInfo's generated constructor), MCRegisterInfo,
// MCInstPrinter and MCSubtargetInfo. The object-emission components
// (MCCodeEmitter / MCAsmBackend / ELFObjectWriter) remain out of Stage-1 scope.
//
//===----------------------------------------------------------------------===//

#include "IA64MCTargetDesc.h"
#include "IA64InstPrinter.h"
#include "IA64MCAsmInfo.h"
#include "IA64TargetStreamer.h"
#include "TargetInfo/IA64TargetInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#include "IA64GenInstrInfo.inc"

#define GET_REGINFO_MC_DESC
#include "IA64GenRegisterInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "IA64GenSubtargetInfo.inc"

static MCAsmInfo *createIA64MCAsmInfo(const MCRegisterInfo &MRI,
                                      const Triple &TT,
                                      const MCTargetOptions &Options) {
  return new IA64MCAsmInfo(TT, Options);
}

static MCInstrInfo *createIA64MCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitIA64MCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createIA64MCRegisterInfo(const Triple & /*TT*/) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitIA64MCRegisterInfo(X, IA64::rp); // rp (b0) is the return-address register
  return X;
}

static MCInstPrinter *createIA64MCInstPrinter(const Triple & /*T*/,
                                              unsigned /*SyntaxVariant*/,
                                              const MCAsmInfo &MAI,
                                              const MCInstrInfo &MII,
                                              const MCRegisterInfo &MRI) {
  return new IA64InstPrinter(MAI, MII, MRI);
}

// The asm streamer carries the IA-64 unwind directives. There is no object
// streamer (the backend has no integrated assembler), so the null streamer just
// uses the no-op base class.
static MCTargetStreamer *createIA64AsmTargetStreamer(MCStreamer &S,
                                                     formatted_raw_ostream &OS,
                                                     MCInstPrinter *) {
  return new IA64TargetAsmStreamer(S, OS);
}

static MCTargetStreamer *createIA64NullTargetStreamer(MCStreamer &S) {
  return new IA64TargetStreamer(S);
}

static MCSubtargetInfo *createIA64MCSubtargetInfo(const Triple &TT,
                                                  StringRef CPU, StringRef FS) {
  if (CPU.empty())
    CPU = "generic";
  return createIA64MCSubtargetInfoImpl(TT, CPU, /*TuneCPU=*/CPU, FS);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeIA64TargetMC() {
  Target &T = getTheIA64Target();

  // Register the MC asm info (replaces the pre-removal IA64TargetAsmInfo /
  // ELFTargetAsmInfo).
  RegisterMCAsmInfoFn X(T, createIA64MCAsmInfo);

  // Register the MC instruction info (the table also backs IA64InstrInfo).
  TargetRegistry::RegisterMCInstrInfo(T, createIA64MCInstrInfo);

  // Register the MC register info and the asm-output instruction printer.
  TargetRegistry::RegisterMCRegInfo(T, createIA64MCRegisterInfo);
  TargetRegistry::RegisterMCInstPrinter(T, createIA64MCInstPrinter);

  // Register the target streamer that emits the IA-64 unwind directives.
  TargetRegistry::RegisterAsmTargetStreamer(T, createIA64AsmTargetStreamer);
  TargetRegistry::RegisterNullTargetStreamer(T, createIA64NullTargetStreamer);

  // Register the MC subtarget info.
  TargetRegistry::RegisterMCSubtargetInfo(T, createIA64MCSubtargetInfo);
}
