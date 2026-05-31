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
  }
  llvm_unreachable("Unhandled IA64 relocation specifier");
}

void IA64MCAsmInfo::anchor() {}

IA64MCAsmInfo::IA64MCAsmInfo(const Triple &TheTriple,
                             const MCTargetOptions &Options)
    : MCAsmInfoELF(Options) {
  // IA-64 is LP64.
  CodePointerSize = CalleeSaveStackSlotSize = 8;

  CommentString = "//";

  // GNU 'as' for IA-64 spells the data directives "dataN"; the ".ua" suffix
  // requests unaligned storage (carried over from IA64TargetAsmInfo).
  Data8bitsDirective = "\tdata1\t";
  Data16bitsDirective = "\tdata2.ua\t";
  Data32bitsDirective = "\tdata4.ua\t";
  Data64bitsDirective = "\tdata8.ua\t";

  ZeroDirective = "\t.skip\t";
  AsciiDirective = "\tstring\t";
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
