//===-- IA64TargetStreamer.cpp - IA64 Target Streamer Methods ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides IA64 specific target streamer methods: the textual form of
// the IA-64 unwind directives. See IA64TargetStreamer.h for the rationale.
//
//===----------------------------------------------------------------------===//

#include "IA64TargetStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/FormattedStream.h"

using namespace llvm;

// pin vtable to this file
IA64TargetStreamer::IA64TargetStreamer(MCStreamer &S) : MCTargetStreamer(S) {}

void IA64TargetStreamer::anchor() {}

IA64TargetAsmStreamer::IA64TargetAsmStreamer(MCStreamer &S,
                                             formatted_raw_ostream &OS)
    : IA64TargetStreamer(S), OS(OS) {}

// gas references IA-64 symbols with a trailing '#' (to disambiguate them from
// register names); the function labels are printed that way too, so the .proc /
// .endp operands must match.
void IA64TargetAsmStreamer::emitProc(const MCSymbol *Sym) {
  OS << "\t.proc\t" << Sym->getName() << "#\n";
}

void IA64TargetAsmStreamer::emitEndP(const MCSymbol *Sym) {
  OS << "\t.endp\t" << Sym->getName() << "#\n";
}

void IA64TargetAsmStreamer::emitPrologueDirective() { OS << "\t.prologue\n"; }

void IA64TargetAsmStreamer::emitSaveARPFS(StringRef Reg) {
  OS << "\t.save\tar.pfs, " << Reg << '\n';
}

void IA64TargetAsmStreamer::emitFFrame(int64_t Size) {
  OS << "\t.fframe\t" << Size << '\n';
}

void IA64TargetAsmStreamer::emitSaveRP(StringRef Reg) {
  OS << "\t.save\trp, " << Reg << '\n';
}

void IA64TargetAsmStreamer::emitBody() { OS << "\t.body\n"; }

void IA64TargetAsmStreamer::emitLabelState(unsigned N) {
  OS << "\t.label_state\t" << N << '\n';
}

void IA64TargetAsmStreamer::emitCopyState(unsigned N) {
  OS << "\t.copy_state\t" << N << '\n';
}

void IA64TargetAsmStreamer::emitRestoreSP() { OS << "\t.restore\tsp\n"; }
