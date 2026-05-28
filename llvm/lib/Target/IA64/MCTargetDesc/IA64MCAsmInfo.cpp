//===-- IA64MCAsmInfo.cpp - IA64 asm properties ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the IA64MCAsmInfo properties. The
// directive set is carried over from the pre-removal IA64TargetAsmInfo; section
// selection (text/cstring/mergeable) is now handled generically by
// TargetLoweringObjectFileELF, so it lives here no longer.
//
//===----------------------------------------------------------------------===//

#include "IA64MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

StringRef IA64::getSpecifierName(uint16_t S) {
  switch (S) {
  case IA64::S_None:
    return {};
  case IA64::S_LTOFF:
    return "ltoff";
  case IA64::S_FPTR:
    return "fptr";
  case IA64::S_TPREL:
    return "tprel";
  case IA64::S_DTPREL:
    return "dtprel";
  case IA64::S_DTPMOD:
    return "dtpmod";
  }
  llvm_unreachable("Unhandled IA64 relocation specifier");
}

void IA64MCAsmInfo::anchor() {}

IA64MCAsmInfo::IA64MCAsmInfo(const Triple &TheTriple,
                             const MCTargetOptions &Options)
    : MCAsmInfoELF() {
  // IA-64 is LP64.
  CodePointerSize = CalleeSaveStackSlotSize = 8;

  CommentString = "//";

  // The IA-64 backend has no integrated assembler (no MCCodeEmitter/AsmParser);
  // we always emit assembly text for GNU 'as'. Telling MC we don't use the
  // integrated assembler makes the AsmPrinter emit inline asm (e.g. the empty
  // barrier that `core::hint::black_box` lowers to) as raw text instead of
  // trying to parse it with a (nonexistent) target asm parser.
  UseIntegratedAssembler = false;

  // GNU 'as' for IA-64 treats a bare identifier that matches a register alias
  // (`gp`=r1, `sp`=r12, `tp`=r13, `rp`=b0, `r1`, ...) as that register even in
  // symbol position, so e.g. a C global named `tp` in `@ltoff(tp)` or a pointer
  // table entry `data8.ua tp` resolves to a register instead of the symbol
  // (a silent miscompile in the data case). Decorate every non-temporary symbol
  // with a trailing '#', which `as` strips -- the form gcc and the pre-removal
  // backend both emit.
  UseSymbolHashSuffix = true;

  // GNU 'as' for IA-64 spells the data directives "dataN"; the ".ua" suffix
  // requests unaligned storage (carried over from IA64TargetAsmInfo).
  Data8bitsDirective = "\tdata1\t";
  Data16bitsDirective = "\tdata2.ua\t";
  Data32bitsDirective = "\tdata4.ua\t";
  Data64bitsDirective = "\tdata8.ua\t";

  ZeroDirective = "\t.skip\t";
  AsciiDirective = "\tstring\t";

  // Emit source-level DWARF (.file/.loc) so the line table maps PCs back to the
  // C source rather than to the temporary .s we hand to GNU 'as'. Without this
  // the AsmPrinter suppresses all .loc directives, and the external assembler --
  // still invoked with -g -- can only synthesize a line table for the assembly
  // file it reads, so gdb shows e.g. "ldo-cbe475.s:257" instead of "ldo.c:NNN".
  SupportsDebugInformation = true;

  // GNU 'as' for IA-64 only accepts the single-string `.file N "name"` form, not
  // LLVM's default two-argument `.file N "dir" "name"` (it rejects the second
  // string as "junk at end of line"). Disabling the directory form makes the
  // MCAsmStreamer fold the directory into the filename: `.file N "dir/name"`.
  EnableDwarfFileDirectoryDefault = false;
}

// Print a relocation specifier as "@name(subexpr)", the form GNU 'as' for
// IA-64 expects (e.g. "@ltoff(.L.str)"). Mirrors SparcELFMCAsmInfo, which uses
// the "%name(...)" syntax.
void IA64MCAsmInfo::printSpecifierExpr(raw_ostream &OS,
                                       const MCSpecifierExpr &Expr) const {
  StringRef S = IA64::getSpecifierName(Expr.getSpecifier());
  if (!S.empty())
    OS << '@' << S << '(';
  printExpr(OS, *Expr.getSubExpr());
  if (!S.empty())
    OS << ')';
}
