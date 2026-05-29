//===-- IA64MCInstLower.h - Lower MachineInstr to MCInst -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_IA64MCINSTLOWER_H
#define LLVM_LIB_TARGET_IA64_IA64MCINSTLOWER_H

namespace llvm {

class AsmPrinter;
class MachineInstr;
class MachineOperand;
class MCContext;
class MCInst;
class MCOperand;
class MCSymbol;

/// IA64MCInstLower - This class lowers a MachineInstr into an MCInst. This did
/// not exist in the pre-removal backend, which printed MachineInstrs directly;
/// modern LLVM routes machine code through the MC layer.
class IA64MCInstLower {
  MCContext &Ctx;
  AsmPrinter &Printer;

public:
  IA64MCInstLower(MCContext &Ctx, AsmPrinter &Printer)
      : Ctx(Ctx), Printer(Printer) {}

  void Lower(const MachineInstr *MI, MCInst &OutMI) const;
  MCOperand lowerSymbolOperand(const MachineOperand &MO, MCSymbol *Sym) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_IA64MCINSTLOWER_H
