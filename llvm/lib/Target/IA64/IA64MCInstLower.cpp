//===-- IA64MCInstLower.cpp - Lower MachineInstr to MCInst ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IA64MCInstLower.h"
#include "MCTargetDesc/IA64MCAsmInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

MCOperand IA64MCInstLower::lowerSymbolOperand(const MachineOperand &MO,
                                              MCSymbol *Sym) const {
  const MCExpr *Expr = MCSymbolRefExpr::create(Sym, Ctx);
  if (MO.getOffset())
    Expr = MCBinaryExpr::createAdd(
        Expr, MCConstantExpr::create(MO.getOffset(), Ctx), Ctx);
  // A relocation specifier (e.g. IA64::S_LTOFF) is carried on the operand's
  // target flags; wrap the symbol so it prints as "@ltoff(sym)". S_LTOFF_FPTR
  // is a marker for a function address loaded through the GOT: nest the two so
  // it prints @ltoff(@fptr(sym)), i.e. the GOT entry holds the descriptor.
  unsigned Specifier = MO.getTargetFlags();
  if (Specifier == IA64::S_LTOFF_FPTR) {
    Expr = MCSpecifierExpr::create(Expr, IA64::S_FPTR, Ctx);
    Expr = MCSpecifierExpr::create(Expr, IA64::S_LTOFF, Ctx);
  } else if (Specifier) {
    Expr = MCSpecifierExpr::create(Expr, Specifier, Ctx);
  }
  return MCOperand::createExpr(Expr);
}

void IA64MCInstLower::Lower(const MachineInstr *MI, MCInst &OutMI) const {
  OutMI.setOpcode(MI->getOpcode());

  for (const MachineOperand &MO : MI->operands()) {
    MCOperand MCOp;
    switch (MO.getType()) {
    default:
      report_fatal_error("IA64: unsupported operand type in MCInstLower");
    case MachineOperand::MO_Register:
      // Implicit operands aren't part of the asm template; drop them.
      if (MO.isImplicit())
        continue;
      MCOp = MCOperand::createReg(MO.getReg());
      break;
    case MachineOperand::MO_Immediate:
      MCOp = MCOperand::createImm(MO.getImm());
      break;
    case MachineOperand::MO_MachineBasicBlock:
      MCOp = MCOperand::createExpr(
          MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), Ctx));
      break;
    case MachineOperand::MO_GlobalAddress:
      MCOp = lowerSymbolOperand(MO, Printer.getSymbol(MO.getGlobal()));
      break;
    case MachineOperand::MO_ExternalSymbol:
      MCOp = lowerSymbolOperand(
          MO, Printer.GetExternalSymbolSymbol(MO.getSymbolName()));
      break;
    case MachineOperand::MO_RegisterMask:
      // Call-clobber masks carry no printable operand.
      continue;
    }
    OutMI.addOperand(MCOp);
  }
}
