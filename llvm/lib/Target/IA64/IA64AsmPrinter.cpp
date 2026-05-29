//===-- IA64AsmPrinter.cpp - Print out IA64 LLVM as assembly --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts the machine-dependent LLVM code
// to GNU 'gas'-compatible IA-64 assembly. Unlike the pre-removal backend, which
// hand-formatted each MachineInstr, this lowers each MachineInstr to an MCInst
// and lets the streamer + IA64InstPrinter emit the text.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "IA64MCInstLower.h"
#include "TargetInfo/IA64TargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

namespace {
class IA64AsmPrinter : public AsmPrinter {
public:
  static char ID;

  explicit IA64AsmPrinter(TargetMachine &TM,
                          std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer), ID) {}

  StringRef getPassName() const override { return "IA64 Assembly Printer"; }

  void emitStartOfAsmFile(Module &M) override;
  void emitInstruction(const MachineInstr *MI) override;
};
} // end anonymous namespace

char IA64AsmPrinter::ID = 0;

void IA64AsmPrinter::emitStartOfAsmFile(Module & /*M*/) {
  // The IA-64 assembly preamble expected by GNU gas, matching the pre-removal
  // output. (lsb should be msb on HP-UX; we only support 64-bit.)
  OutStreamer->emitRawText(StringRef("\t.psr\tlsb"));
  OutStreamer->emitRawText(StringRef("\t.radix\tC"));
  OutStreamer->emitRawText(StringRef("\t.psr\tabi64"));
}

void IA64AsmPrinter::emitInstruction(const MachineInstr *MI) {
  IA64MCInstLower Lower(OutContext, *this);
  MCInst TmpInst;
  Lower.Lower(MI, TmpInst);
  EmitToStreamer(*OutStreamer, TmpInst);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeIA64AsmPrinter() {
  RegisterAsmPrinter<IA64AsmPrinter> X(getTheIA64Target());
}
