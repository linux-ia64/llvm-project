//===-- IA64MCAsmInfo.h - IA64 asm properties ------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the IA64MCAsmInfo class. It is the
// modern (MC-layer) replacement for the pre-removal IA64TargetAsmInfo, which
// subclassed the long-deleted ELFTargetAsmInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64MCASMINFO_H
#define LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64MCASMINFO_H

#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCAsmInfoELF.h"
#include "llvm/MC/MCExpr.h"

namespace llvm {

class MCSpecifierExpr;
class MCTargetOptions;
class Triple;
class raw_ostream;

namespace IA64 {
// Relocation specifiers. The backend is asm-output only, so these only select
// the printed form (e.g. "@ltoff(sym)"); the GNU assembler turns that into the
// matching R_IA64_* relocation. The specifier value is carried on the symbol
// operand's target flags (set in IA64ISelDAGToDAG) and read back in
// IA64MCInstLower.
enum Specifier : uint16_t {
  S_None = 0,
  // @ltoff(sym): the gp-relative offset of the symbol's linkage-table (GOT)
  // entry; emitted for the ADDL_GA + LD8 global-address sequence.
  S_LTOFF = MCSymbolRefExpr::FirstTargetSpecifier,
  // @fptr(sym): the address of the function descriptor { entry, gp } for a
  // function symbol -- what a C function pointer must hold. Emitted for
  // function pointers stored in data (data8 @fptr(f)).
  S_FPTR,
  // Thread-local storage offsets. @tprel(sym) is the symbol's offset from the
  // thread pointer (tp/r13), used directly in local-exec (movl @tprel). @dtprel
  // and @dtpmod are the dynamic-model offset and module id, materialised through
  // the GOT (see the S_LTOFF_* markers below) and consumed by __tls_get_addr.
  S_TPREL,
  S_DTPREL,
  S_DTPMOD,
  // Marker flags (never stored in an MCSpecifierExpr): a value loaded through
  // the GOT, so lowerSymbolOperand nests the inner specifier inside @ltoff and
  // prints @ltoff(@fptr(f)) / @ltoff(@tprel(x)) / @ltoff(@dtpmod(x)) / etc.
  S_LTOFF_FPTR,
  S_LTOFF_TPREL,
  S_LTOFF_DTPMOD,
  S_LTOFF_DTPREL,
};

StringRef getSpecifierName(uint16_t S);
} // namespace IA64

class IA64MCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit IA64MCAsmInfo(const Triple &TheTriple,
                         const MCTargetOptions &Options);

  void printSpecifierExpr(raw_ostream &OS,
                          const MCSpecifierExpr &Expr) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64MCASMINFO_H
