//===-- IA64InstPrinter.cpp - Convert IA64 MCInst to assembly syntax ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class prints an IA64 MCInst to a .s file.
//
//===----------------------------------------------------------------------===//

#include "IA64InstPrinter.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

// Include the auto-generated portion of the assembly writer.
#include "IA64GenAsmWriter.inc"

void IA64InstPrinter::printRegName(raw_ostream &OS, MCRegister Reg) {
  OS << getRegisterName(Reg);
}

void IA64InstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                StringRef Annot, const MCSubtargetInfo & /*STI*/,
                                raw_ostream &O) {
  printInstruction(MI, Address, O);
  printAnnotation(O, Annot);
}

void IA64InstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                   raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isReg()) {
    O << getRegisterName(Op.getReg());
    return;
  }
  if (Op.isImm()) {
    O << Op.getImm();
    return;
  }
  assert(Op.isExpr() && "Unknown operand kind in printOperand");
  MAI.printExpr(O, *Op.getExpr());
}

// Sign-extend and print an immediate of the given bit width. The pre-removal
// AsmPrinter did this by hand because the operands are stored unsigned.
void IA64InstPrinter::printS8ImmOperand(const MCInst *MI, unsigned OpNo,
                                        raw_ostream &O) {
  int Val = (int)MI->getOperand(OpNo).getImm();
  if (Val >= 128)
    Val -= 256;
  O << Val;
}

void IA64InstPrinter::printS14ImmOperand(const MCInst *MI, unsigned OpNo,
                                         raw_ostream &O) {
  int Val = (int)MI->getOperand(OpNo).getImm();
  if (Val >= 8192)
    Val -= 16384;
  O << Val;
}

void IA64InstPrinter::printS22ImmOperand(const MCInst *MI, unsigned OpNo,
                                         raw_ostream &O) {
  int Val = (int)MI->getOperand(OpNo).getImm();
  if (Val >= 2097152)
    Val -= 4194304;
  O << Val;
}

void IA64InstPrinter::printS64ImmOperand(const MCInst *MI, unsigned OpNo,
                                         raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isImm())
    O << Op.getImm();
  else // a constant-pool / symbol reference
    printOperand(MI, OpNo, O);
}

// plus.ll exercises no globals or calls; the @ltoff(@fptr(...)) decoration the
// pre-removal backend applied is out of Stage-1 scope, so these defer to the
// generic operand printer for now.
void IA64InstPrinter::printGlobalOperand(const MCInst *MI, unsigned OpNo,
                                         raw_ostream &O) {
  printOperand(MI, OpNo, O);
}

void IA64InstPrinter::printCallOperand(const MCInst *MI, unsigned OpNo,
                                       raw_ostream &O) {
  printOperand(MI, OpNo, O);
}
