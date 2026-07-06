//===- IA64InstrFormats.td - IA64 Instruction Formats -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  These classes are the base classes for every instruction defined in
//  IA64InstrInfo.td. The backend is assembly-only (there is no MCCodeEmitter),
//  so the instruction-encoding fields (the 'Inst' bits) are not consumed by
//  anything; only the operand lists, asm strings, and instruction flags matter.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Instruction format superclass
//===----------------------------------------------------------------------===//

class InstIA64<bits<4> op, dag OOL, dag IOL, string asmstr> : Instruction {
  field bits<41> Inst;
  let Namespace = "IA64";
  let OutOperandList = OOL;
  let InOperandList = IOL;
  let AsmString = asmstr;

  let Inst{40-37} = op;
}

class AForm<bits<4> opcode, bits<6> qpReg, dag OOL, dag IOL, string asmstr> :
  InstIA64<opcode, OOL, IOL, asmstr> {

  let Inst{5-0} = qpReg;
}

class AForm_DAG<bits<4> opcode, bits<6> qpReg, dag OOL, dag IOL, string asmstr,
      list<dag> pattern> :
  InstIA64<opcode, OOL, IOL, asmstr> {

  let Pattern = pattern;
  let Inst{5-0} = qpReg;
}

class RawForm<bits<4> opcode, bits<26> rest, dag OOL, dag IOL, string asmstr> :
  InstIA64<opcode, OOL, IOL, asmstr> {
    let Inst{25-0} = rest;
}

// Pseudo instructions.
class PseudoInstIA64<dag OOL, dag IOL, string nm> : InstIA64<0, OOL, IOL, nm>  {
}

class PseudoInstIA64_DAG<dag OOL, dag IOL, string nm, list<dag> pattern>
  : InstIA64<0, OOL, IOL, nm> {
  let Pattern = pattern;
}
