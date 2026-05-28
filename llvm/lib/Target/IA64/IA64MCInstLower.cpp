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
  // A jump-table index carries no addend (and getOffset() asserts on it); only
  // globals/external symbols can have a non-zero offset here.
  if (!MO.isJTI() && MO.getOffset())
    Expr = MCBinaryExpr::createAdd(
        Expr, MCConstantExpr::create(MO.getOffset(), Ctx), Ctx);
  // A relocation specifier (e.g. IA64::S_LTOFF) is carried on the operand's
  // target flags; wrap the symbol so it prints as "@ltoff(sym)". The S_LTOFF_*
  // values are markers for a value loaded through the GOT: nest the inner
  // specifier inside @ltoff so the GOT entry holds the descriptor / TLS datum,
  // e.g. @ltoff(@fptr(sym)) or @ltoff(@tprel(sym)).
  unsigned Specifier = MO.getTargetFlags();
  unsigned Inner = 0;
  switch (Specifier) {
  case IA64::S_LTOFF_FPTR:
    Inner = IA64::S_FPTR;
    break;
  case IA64::S_LTOFF_TPREL:
    Inner = IA64::S_TPREL;
    break;
  case IA64::S_LTOFF_DTPMOD:
    Inner = IA64::S_DTPMOD;
    break;
  case IA64::S_LTOFF_DTPREL:
    Inner = IA64::S_DTPREL;
    break;
  }
  if (Inner) {
    Expr = MCSpecifierExpr::create(Expr, Inner, Ctx);
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
    case MachineOperand::MO_JumpTableIndex:
      MCOp = lowerSymbolOperand(MO, Printer.GetJTISymbol(MO.getIndex()));
      break;
    case MachineOperand::MO_ConstantPoolIndex:
      MCOp = lowerSymbolOperand(MO, Printer.GetCPISymbol(MO.getIndex()));
      break;
    case MachineOperand::MO_RegisterMask:
      // Call-clobber masks carry no printable operand.
      continue;
    }
    OutMI.addOperand(MCOp);
  }
}
