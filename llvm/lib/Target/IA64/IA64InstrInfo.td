//===- IA64InstrInfo.td - Describe the IA64 Instruction Set *- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the IA64 instruction set, defining the instructions, and
// properties of the instructions which are needed for code generation, machine
// code emission, and analysis.
//
// Phase C status: this ports the instruction *descriptions* (operands, asm
// strings, unit type, and the explicit instruction flags / Defs / Uses) and
// wires -gen-instr-info. The DAG selection patterns, PatLeaf predicates, the
// IA64ISD SDNodes and the standalone Pat<> rules from the pre-removal backend
// are deferred to Phase D, where they are restored together with
// IA64ISelLowering and the -gen-dag-isel / -gen-asm-writer generators (they
// reference IA64ISD nodes, the generic `ret` node and operand PrintMethods that
// only exist once the lowering is in place). Instructions that carried a
// pattern keep an empty `[]` placeholder so Phase D can drop the patterns back
// in.
//
//===----------------------------------------------------------------------===//

include "IA64InstrFormats.td"

//===----------------------------------------------------------------------===//
// Instruction types
//===----------------------------------------------------------------------===//

class isA { bit A=1; } // I or M unit
class isM { bit M=1; } // M unit
class isI { bit I=1; } // I unit
class isB { bit B=1; } // B unit
class isF { bit F=1; } // F unit
class isLX { bit LX=1; } // I/B

//===----------------------------------------------------------------------===//
// Operands
//===----------------------------------------------------------------------===//

def u2imm : Operand<i8>;
def u6imm : Operand<i8>;
def s8imm : Operand<i8> {
  let PrintMethod = "printS8ImmOperand";
}
def s14imm  : Operand<i64> {
  let PrintMethod = "printS14ImmOperand";
}
def s22imm  : Operand<i64> {
  let PrintMethod = "printS22ImmOperand";
}
def u64imm  : Operand<i64> {
  let PrintMethod = "printU64ImmOperand";
}
def s64imm  : Operand<i64> {
  let PrintMethod = "printS64ImmOperand";
}

let PrintMethod = "printGlobalOperand" in
  def globaladdress : Operand<i64>;

// the asmprinter needs to know about calls
let PrintMethod = "printCallOperand" in
  def calltarget : Operand<i64>;

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

def ADD  : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
           "add $dst = $src1, $src2", []>, isA;

def ADD1 : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
           "add $dst = $src1, $src2, 1", []>, isA;

def ADDS : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, s14imm:$imm),
           "adds $dst = $imm, $src1", []>, isA;

def MOVL : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins s64imm:$imm),
           "movl $dst = $imm", []>, isLX;

def ADDL_GA : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, globaladdress:$imm),
           "addl $dst = $imm, $src1", []>, isA;

// hmm
def ADDL_EA : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, calltarget:$imm),
           "addl $dst = $imm, $src1", []>, isA;

def SUB  : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
           "sub $dst = $src1, $src2", []>, isA;

def SUB1 : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
           "sub $dst = $src1, $src2, 1", []>, isA;

// The pre-removal backend marked these "isTwoAddress"; the modern equivalent
// is a tied-operand Constraints string (first input tied to the def).
let Constraints = "$src1 = $dst" in {
def TPCADDIMM22 : AForm<0x03, 0x0b,
  (outs GR:$dst), (ins GR:$src1, s22imm:$imm, PR:$qp),
    "($qp) add $dst = $imm, $dst">, isA;
def TPCADDS : AForm_DAG<0x03, 0x0b,
  (outs GR:$dst), (ins GR:$src1, s14imm:$imm, PR:$qp),
    "($qp) adds $dst = $imm, $dst", []>, isA;
def TPCMPIMM8NE : AForm<0x03, 0x0b,
  (outs PR:$dst), (ins PR:$src1, s22imm:$imm, GR:$src2, PR:$qp),
    "($qp) cmp.ne $dst , p0 = $imm, $src2">, isA;
}

// normal sign/zero-extends
def SXT1 : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src), "sxt1 $dst = $src",
           []>, isI;
def ZXT1 : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src), "zxt1 $dst = $src",
           []>, isI;
def SXT2 : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src), "sxt2 $dst = $src",
           []>, isI;
def ZXT2 : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src), "zxt2 $dst = $src",
           []>, isI;
def SXT4 : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src), "sxt4 $dst = $src",
           []>, isI;
def ZXT4 : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src), "zxt4 $dst = $src",
           []>, isI;

// fixme: shrs vs shru?
def MIX1L : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
          "mix1.l $dst = $src1, $src2", []>, isI;
def MIX2L : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
          "mix2.l $dst = $src1, $src2", []>, isI;
def MIX4L : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
          "mix4.l $dst = $src1, $src2", []>, isI;
def MIX1R : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
          "mix1.r $dst = $src1, $src2", []>, isI;
def MIX2R : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
          "mix2.r $dst = $src1, $src2", []>, isI;
def MIX4R : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
          "mix4.r $dst = $src1, $src2", []>, isI;

def GETFSIGD : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins FP:$src),
  "getf.sig $dst = $src", []>, isM;
def SETFSIGD : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins GR:$src),
  "setf.sig $dst = $src", []>, isM;

def XMALD : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2, FP:$src3),
  "xma.l $dst = $src1, $src2, $src3", []>, isF;
def XMAHD : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2, FP:$src3),
  "xma.h $dst = $src1, $src2, $src3", []>, isF;
def XMAHUD : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2, FP:$src3),
  "xma.hu $dst = $src1, $src2, $src3", []>, isF;

def AND   : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
          "and $dst = $src1, $src2", []>, isA;
def ANDCM : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
          "andcm $dst = $src1, $src2", []>, isA;
// TODO: and/andcm/or/xor/add/sub/shift immediate forms
def OR    : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
          "or $dst = $src1, $src2", []>, isA;

def pOR   : AForm<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2, PR:$qp),
          "($qp) or $dst = $src1, $src2">, isA;

// the following are all a bit unfortunate: we throw away the complement
// of the compare!
def CMPEQ : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins GR:$src1, GR:$src2),
          "cmp.eq $dst, p0 = $src1, $src2", []>, isA;
def CMPGT : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins GR:$src1, GR:$src2),
          "cmp.gt $dst, p0 = $src1, $src2", []>, isA;
def CMPGE : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins GR:$src1, GR:$src2),
          "cmp.ge $dst, p0 = $src1, $src2", []>, isA;
def CMPLT : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins GR:$src1, GR:$src2),
          "cmp.lt $dst, p0 = $src1, $src2", []>, isA;
def CMPLE : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins GR:$src1, GR:$src2),
          "cmp.le $dst, p0 = $src1, $src2", []>, isA;
def CMPNE : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins GR:$src1, GR:$src2),
          "cmp.ne $dst, p0 = $src1, $src2", []>, isA;
def CMPLTU: AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins GR:$src1, GR:$src2),
          "cmp.ltu $dst, p0 = $src1, $src2", []>, isA;
def CMPGTU: AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins GR:$src1, GR:$src2),
          "cmp.gtu $dst, p0 = $src1, $src2", []>, isA;
def CMPLEU: AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins GR:$src1, GR:$src2),
          "cmp.leu $dst, p0 = $src1, $src2", []>, isA;
def CMPGEU: AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins GR:$src1, GR:$src2),
          "cmp.geu $dst, p0 = $src1, $src2", []>, isA;

// and we do the whole thing again for FP compares!
def FCMPEQ : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins FP:$src1, FP:$src2),
          "fcmp.eq $dst, p0 = $src1, $src2", []>, isF;
def FCMPGT : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins FP:$src1, FP:$src2),
          "fcmp.gt $dst, p0 = $src1, $src2", []>, isF;
def FCMPGE : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins FP:$src1, FP:$src2),
          "fcmp.ge $dst, p0 = $src1, $src2", []>, isF;
def FCMPLT : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins FP:$src1, FP:$src2),
          "fcmp.lt $dst, p0 = $src1, $src2", []>, isF;
def FCMPLE : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins FP:$src1, FP:$src2),
          "fcmp.le $dst, p0 = $src1, $src2", []>, isF;
def FCMPNE : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins FP:$src1, FP:$src2),
          "fcmp.neq $dst, p0 = $src1, $src2", []>, isF;
def FCMPLTU: AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins FP:$src1, FP:$src2),
          "fcmp.lt $dst, p0 = $src1, $src2", []>, isF;
def FCMPGTU: AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins FP:$src1, FP:$src2),
          "fcmp.gt $dst, p0 = $src1, $src2", []>, isF;
def FCMPLEU: AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins FP:$src1, FP:$src2),
          "fcmp.le $dst, p0 = $src1, $src2", []>, isF;
def FCMPGEU: AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins FP:$src1, FP:$src2),
          "fcmp.ge $dst, p0 = $src1, $src2", []>, isF;

def PCMPEQUNCR0R0 : AForm<0x03, 0x0b, (outs PR:$dst), (ins PR:$qp),
    "($qp) cmp.eq.unc $dst, p0 = r0, r0">, isA;

let Constraints = "$bogus = $dst" in {
  def TPCMPEQR0R0 : AForm<0x03, 0x0b, (outs PR:$dst), (ins PR:$bogus, PR:$qp),
    "($qp) cmp.eq $dst, p0 = r0, r0">, isA;
  def TPCMPNER0R0 : AForm<0x03, 0x0b, (outs PR:$dst), (ins PR:$bogus, PR:$qp),
    "($qp) cmp.ne $dst, p0 = r0, r0">, isA;
}

def XOR   : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
          "xor $dst = $src1, $src2", []>, isA;

def SHLADD: AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1,s64imm:$imm,GR:$src2),
          "shladd $dst = $src1, $imm, $src2", []>, isA;

def SHL   : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
          "shl $dst = $src1, $src2", []>, isI;

def SHRU  : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
          "shr.u $dst = $src1, $src2", []>, isI;

def SHRS  : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
          "shr $dst = $src1, $src2", []>, isI;

def MOV : AForm<0x03, 0x0b, (outs GR:$dst), (ins GR:$src), "mov $dst = $src">, isA;
def FMOV : AForm<0x03, 0x0b, (outs FP:$dst), (ins FP:$src),
  "mov $dst = $src">, isF; // XXX: there _is_ no fmov
def PMOV : AForm<0x03, 0x0b, (outs GR:$dst), (ins GR:$src, PR:$qp),
  "($qp) mov $dst = $src">, isA;

def SPILL_ALL_PREDICATES_TO_GR : AForm<0x03, 0x0b, (outs GR:$dst), (ins),
  "mov $dst = pr">, isI;
def FILL_ALL_PREDICATES_FROM_GR : AForm<0x03, 0x0b, (outs), (ins GR:$src),
  "mov pr = $src">, isI;

let Constraints = "$src2 = $dst" in {
  def CMOV : AForm<0x03, 0x0b, (outs GR:$dst), (ins GR:$src2, GR:$src, PR:$qp),
    "($qp) mov $dst = $src">, isA;
}

def PFMOV : AForm<0x03, 0x0b, (outs FP:$dst), (ins FP:$src, PR:$qp),
  "($qp) mov $dst = $src">, isF;

let Constraints = "$src2 = $dst" in {
  def CFMOV : AForm<0x03, 0x0b, (outs FP:$dst), (ins FP:$src2, FP:$src, PR:$qp),
    "($qp) mov $dst = $src">, isF;
}

// load constants of various sizes // FIXME: prettyprint -ve constants

// TODO: support postincrement (reg, imm9) loads+stores - this needs more
// tablegen support

def IUSE : PseudoInstIA64<(outs), (ins variable_ops), "// IUSE">;
def ADJUSTCALLSTACKUP : PseudoInstIA64<(outs), (ins variable_ops),
                                        "// ADJUSTCALLSTACKUP">;
def ADJUSTCALLSTACKDOWN : PseudoInstIA64<(outs), (ins variable_ops),
                                         "// ADJUSTCALLSTACKDOWN">;
def PSEUDO_ALLOC : PseudoInstIA64<(outs), (ins GR:$foo), "// PSEUDO_ALLOC">;

def ALLOC : AForm<0x03, 0x0b,
  (outs GR:$dst), (ins i8imm:$inputs, i8imm:$locals, i8imm:$outputs, i8imm:$rotating),
    "alloc $dst = ar.pfs,$inputs,$locals,$outputs,$rotating">, isM;

let Constraints = "$src2 = $dst" in {
  def TCMPNE : AForm<0x03, 0x0b,
  (outs PR:$dst), (ins PR:$src2, GR:$src3, GR:$src4),
    "cmp.ne $dst, p0 = $src3, $src4">, isA;

  def TPCMPEQOR : AForm<0x03, 0x0b,
  (outs PR:$dst), (ins PR:$src2, GR:$src3, GR:$src4, PR:$qp),
    "($qp) cmp.eq.or $dst, p0 = $src3, $src4">, isA;

  def TPCMPNE : AForm<0x03, 0x0b,
  (outs PR:$dst), (ins PR:$src2, GR:$src3, GR:$src4, PR:$qp),
    "($qp) cmp.ne $dst, p0 = $src3, $src4">, isA;

  def TPCMPEQ : AForm<0x03, 0x0b,
  (outs PR:$dst), (ins PR:$src2, GR:$src3, GR:$src4, PR:$qp),
    "($qp) cmp.eq $dst, p0 = $src3, $src4">, isA;
}

def MOVSIMM14 : AForm<0x03, 0x0b, (outs GR:$dst), (ins s14imm:$imm),
  "mov $dst = $imm">, isA;
def MOVSIMM22 : AForm<0x03, 0x0b, (outs GR:$dst), (ins s22imm:$imm),
  "mov $dst = $imm">, isA;
def MOVLIMM64 : AForm<0x03, 0x0b, (outs GR:$dst), (ins s64imm:$imm),
  "movl $dst = $imm">, isLX;

def SHLI : AForm<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, u6imm:$imm),
  "shl $dst = $src1, $imm">, isI;
def SHRUI : AForm<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, u6imm:$imm),
  "shr.u $dst = $src1, $imm">, isI;
def SHRSI : AForm<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, u6imm:$imm),
  "shr $dst = $src1, $imm">, isI;

def EXTRU : AForm<0x03, 0x0b,
  (outs GR:$dst), (ins GR:$src1, u6imm:$imm1, u6imm:$imm2),
  "extr.u $dst = $src1, $imm1, $imm2">, isI;

def DEPZ : AForm<0x03, 0x0b,
  (outs GR:$dst), (ins GR:$src1, u6imm:$imm1, u6imm:$imm2),
  "dep.z $dst = $src1, $imm1, $imm2">, isI;

def PCMPEQOR : AForm<0x03, 0x0b, (outs PR:$dst), (ins GR:$src1, GR:$src2, PR:$qp),
  "($qp) cmp.eq.or $dst, p0 = $src1, $src2">, isA;
def PCMPEQUNC : AForm<0x03, 0x0b, (outs PR:$dst), (ins GR:$src1, GR:$src2, PR:$qp),
  "($qp) cmp.eq.unc $dst, p0 = $src1, $src2">, isA;
def PCMPNE : AForm<0x03, 0x0b, (outs PR:$dst), (ins GR:$src1, GR:$src2, PR:$qp),
  "($qp) cmp.ne $dst, p0 = $src1, $src2">, isA;

// two destinations!
def BCMPEQ : AForm<0x03, 0x0b, (outs PR:$dst1, PR:$dst2), (ins GR:$src1, GR:$src2),
  "cmp.eq $dst1, dst2 = $src1, $src2">, isA;

def ADDIMM14 : AForm<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, s14imm:$imm),
  "adds $dst = $imm, $src1">, isA;

def ADDIMM22 : AForm<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, s22imm:$imm),
  "add $dst = $imm, $src1">, isA;
def CADDIMM22 : AForm<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, s22imm:$imm, PR:$qp),
  "($qp) add $dst = $imm, $src1">, isA;

def SUBIMM8 : AForm<0x03, 0x0b, (outs GR:$dst), (ins s8imm:$imm, GR:$src2),
  "sub $dst = $imm, $src2">, isA;

let mayStore = 1 in {
  def ST1 : AForm<0x03, 0x0b, (outs), (ins GR:$dstPtr, GR:$value),
    "st1 [$dstPtr] = $value">, isM;
  def ST2 : AForm<0x03, 0x0b, (outs), (ins GR:$dstPtr, GR:$value),
    "st2 [$dstPtr] = $value">, isM;
  def ST4 : AForm<0x03, 0x0b, (outs), (ins GR:$dstPtr, GR:$value),
    "st4 [$dstPtr] = $value">, isM;
  def ST8 : AForm<0x03, 0x0b, (outs), (ins GR:$dstPtr, GR:$value),
    "st8 [$dstPtr] = $value">, isM;
  def STF4 : AForm<0x03, 0x0b, (outs), (ins GR:$dstPtr, FP:$value),
    "stfs [$dstPtr] = $value">, isM;
  def STF8 : AForm<0x03, 0x0b, (outs), (ins GR:$dstPtr, FP:$value),
    "stfd [$dstPtr] = $value">, isM;
  def STF_SPILL : AForm<0x03, 0x0b, (outs), (ins GR:$dstPtr, FP:$value),
    "stf.spill [$dstPtr] = $value">, isM;
}

let canFoldAsLoad = 1 in {
  def LD1 : AForm<0x03, 0x0b, (outs GR:$dst), (ins GR:$srcPtr),
    "ld1 $dst = [$srcPtr]">, isM;
  def LD2 : AForm<0x03, 0x0b, (outs GR:$dst), (ins GR:$srcPtr),
    "ld2 $dst = [$srcPtr]">, isM;
  def LD4 : AForm<0x03, 0x0b, (outs GR:$dst), (ins GR:$srcPtr),
    "ld4 $dst = [$srcPtr]">, isM;
  def LD8 : AForm<0x03, 0x0b, (outs GR:$dst), (ins GR:$srcPtr),
    "ld8 $dst = [$srcPtr]">, isM;
  def LDF4 : AForm<0x03, 0x0b, (outs FP:$dst), (ins GR:$srcPtr),
    "ldfs $dst = [$srcPtr]">, isM;
  def LDF8 : AForm<0x03, 0x0b, (outs FP:$dst), (ins GR:$srcPtr),
    "ldfd $dst = [$srcPtr]">, isM;
  def LDF_FILL : AForm<0x03, 0x0b, (outs FP:$dst), (ins GR:$srcPtr),
    "ldf.fill $dst = [$srcPtr]">, isM;
}

def POPCNT : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src),
  "popcnt $dst = $src", []>, isI;

// some FP stuff:  // TODO: single-precision stuff?
def FADD : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2),
  "fadd $dst = $src1, $src2", []>, isF;
def FADDS: AForm<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2),
  "fadd.s $dst = $src1, $src2">, isF;
def FSUB : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2),
  "fsub $dst = $src1, $src2", []>, isF;
def FMPY : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2),
  "fmpy $dst = $src1, $src2", []>, isF;
def FMA : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2, FP:$src3),
  "fma $dst = $src1, $src2, $src3", []>, isF;
def FMS : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2, FP:$src3),
  "fms $dst = $src1, $src2, $src3", []>, isF;
def FNMA : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2, FP:$src3),
  "fnma $dst = $src1, $src2, $src3", []>, isF;
def FABS : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src),
  "fabs $dst = $src", []>, isF;
def FNEG : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src),
  "fneg $dst = $src", []>, isF;
def FNEGABS : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src),
  "fnegabs $dst = $src", []>, isF;

let Constraints = "$bogussrc = $dst" in {
def TCFMAS1 : AForm<0x03, 0x0b,
  (outs FP:$dst), (ins FP:$bogussrc, FP:$src1, FP:$src2, FP:$src3, PR:$qp),
    "($qp) fma.s1 $dst = $src1, $src2, $src3">, isF;
def TCFMADS0 : AForm<0x03, 0x0b,
  (outs FP:$dst), (ins FP:$bogussrc, FP:$src1, FP:$src2, FP:$src3, PR:$qp),
    "($qp) fma.d.s0 $dst = $src1, $src2, $src3">, isF;
}

def CFMAS1 : AForm<0x03, 0x0b,
  (outs FP:$dst), (ins FP:$src1, FP:$src2, FP:$src3, PR:$qp),
    "($qp) fma.s1 $dst = $src1, $src2, $src3">, isF;
def CFNMAS1 : AForm<0x03, 0x0b,
  (outs FP:$dst), (ins FP:$src1, FP:$src2, FP:$src3, PR:$qp),
    "($qp) fnma.s1 $dst = $src1, $src2, $src3">, isF;

def CFMADS1 : AForm<0x03, 0x0b,
  (outs FP:$dst), (ins FP:$src1, FP:$src2, FP:$src3, PR:$qp),
    "($qp) fma.d.s1 $dst = $src1, $src2, $src3">, isF;
def CFMADS0 : AForm<0x03, 0x0b,
  (outs FP:$dst), (ins FP:$src1, FP:$src2, FP:$src3, PR:$qp),
    "($qp) fma.d.s0 $dst = $src1, $src2, $src3">, isF;
def CFNMADS1 : AForm<0x03, 0x0b,
  (outs FP:$dst), (ins FP:$src1, FP:$src2, FP:$src3, PR:$qp),
    "($qp) fnma.d.s1 $dst = $src1, $src2, $src3">, isF;

def FRCPAS0 : AForm<0x03, 0x0b, (outs FP:$dstFR, PR:$dstPR), (ins FP:$src1, FP:$src2),
  "frcpa.s0 $dstFR, $dstPR = $src1, $src2">, isF;
def FRCPAS1 : AForm<0x03, 0x0b, (outs FP:$dstFR, PR:$dstPR), (ins FP:$src1, FP:$src2),
  "frcpa.s1 $dstFR, $dstPR = $src1, $src2">, isF;

def XMAL : AForm<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2, FP:$src3),
  "xma.l $dst = $src1, $src2, $src3">, isF;

def FCVTXF : AForm<0x03, 0x0b, (outs FP:$dst), (ins FP:$src),
  "fcvt.xf $dst = $src">, isF;
def FCVTXUF : AForm<0x03, 0x0b, (outs FP:$dst), (ins FP:$src),
  "fcvt.xuf $dst = $src">, isF;
def FCVTXUFS1 : AForm<0x03, 0x0b, (outs FP:$dst), (ins FP:$src),
  "fcvt.xuf.s1 $dst = $src">, isF;
def FCVTFX : AForm<0x03, 0x0b, (outs FP:$dst), (ins FP:$src),
  "fcvt.fx $dst = $src">, isF;
def FCVTFXU : AForm<0x03, 0x0b, (outs FP:$dst), (ins FP:$src),
  "fcvt.fxu $dst = $src">, isF;

def FCVTFXTRUNC : AForm<0x03, 0x0b, (outs FP:$dst), (ins FP:$src),
  "fcvt.fx.trunc $dst = $src">, isF;
def FCVTFXUTRUNC : AForm<0x03, 0x0b, (outs FP:$dst), (ins FP:$src),
  "fcvt.fxu.trunc $dst = $src">, isF;

def FCVTFXTRUNCS1 : AForm<0x03, 0x0b, (outs FP:$dst), (ins FP:$src),
  "fcvt.fx.trunc.s1 $dst = $src">, isF;
def FCVTFXUTRUNCS1 : AForm<0x03, 0x0b, (outs FP:$dst), (ins FP:$src),
  "fcvt.fxu.trunc.s1 $dst = $src">, isF;

def FNORMD : AForm<0x03, 0x0b, (outs FP:$dst), (ins FP:$src),
  "fnorm.d $dst = $src">, isF;

def GETFD : AForm<0x03, 0x0b, (outs GR:$dst), (ins FP:$src),
  "getf.d $dst = $src">, isM;
def SETFD : AForm<0x03, 0x0b, (outs FP:$dst), (ins GR:$src),
  "setf.d $dst = $src">, isM;

def GETFSIG : AForm<0x03, 0x0b, (outs GR:$dst), (ins FP:$src),
  "getf.sig $dst = $src">, isM;
def SETFSIG : AForm<0x03, 0x0b, (outs FP:$dst), (ins GR:$src),
  "setf.sig $dst = $src">, isM;

let isTerminator = 1, isBranch = 1 in {
  def BRL_NOTCALL : RawForm<0x03, 0xb0, (outs), (ins i64imm:$dst),
    "(p0) brl.cond.sptk $dst">, isB;
  def BRLCOND_NOTCALL : RawForm<0x03, 0xb0, (outs), (ins PR:$qp, i64imm:$dst),
    "($qp) brl.cond.sptk $dst">, isB;
  def BRCOND_NOTCALL : RawForm<0x03, 0xb0, (outs), (ins PR:$qp, GR:$dst),
    "($qp) br.cond.sptk $dst">, isB;
}

let isCall = 1, /* isTerminator = 1, isBranch = 1, */
  Uses = [out0,out1,out2,out3,out4,out5,out6,out7],
// all calls clobber non-callee-saved registers, and for now, they are these:
  Defs = [r2,r3,r8,r9,r10,r11,r14,r15,r16,r17,r18,r19,r20,r21,r22,r23,r24,
  r25,r26,r27,r28,r29,r30,r31,
  p6,p7,p8,p9,p10,p11,p12,p13,p14,p15,
  F6,F7,F8,F9,F10,F11,F12,F13,F14,F15,
  F32,F33,F34,F35,F36,F37,F38,F39,F40,F41,F42,F43,F44,F45,F46,F47,F48,F49,
  F50,F51,F52,F53,F54,F55,F56,
  F57,F58,F59,F60,F61,F62,F63,F64,F65,F66,F67,F68,F69,F70,F71,F72,F73,F74,
  F75,F76,F77,F78,F79,F80,F81,
  F82,F83,F84,F85,F86,F87,F88,F89,F90,F91,F92,F93,F94,F95,F96,F97,F98,F99,
  F100,F101,F102,F103,F104,F105,
  F106,F107,F108,F109,F110,F111,F112,F113,F114,F115,F116,F117,F118,F119,
  F120,F121,F122,F123,F124,F125,F126,F127,
  out0,out1,out2,out3,out4,out5,out6,out7] in {
// old pattern call
  def BRCALL: RawForm<0x03, 0xb0, (outs), (ins calltarget:$dst),
  "br.call.sptk rp = $dst">, isB;   // FIXME: teach llvm about branch regs?

// calls a globaladdress
  def BRCALL_IPREL_GA : RawForm<0x03, 0xb0, (outs), (ins calltarget:$dst),
  "br.call.sptk rp = $dst">, isB;       // FIXME: teach llvm about branch regs?
// calls an externalsymbol
  def BRCALL_IPREL_ES : RawForm<0x03, 0xb0, (outs), (ins calltarget:$dst),
  "br.call.sptk rp = $dst">, isB;       // FIXME: teach llvm about branch regs?
// calls through a function descriptor
  def BRCALL_INDIRECT : RawForm<0x03, 0xb0, (outs), (ins GR:$branchreg),
  "br.call.sptk rp = $branchreg">, isB; // FIXME: teach llvm about branch regs?
  def BRLCOND_CALL : RawForm<0x03, 0xb0, (outs), (ins PR:$qp, i64imm:$dst),
    "($qp) brl.cond.call.sptk $dst">, isB;
  def BRCOND_CALL : RawForm<0x03, 0xb0, (outs), (ins PR:$qp, GR:$dst),
    "($qp) br.cond.call.sptk $dst">, isB;
}

// Return branch:
let isTerminator = 1, isReturn = 1 in
  def RET : AForm<0x03, 0x0b, (outs), (ins),
            "br.ret.sptk.many rp">, isB; // return

// the evil stop bit of despair
def STOP : PseudoInstIA64<(outs), (ins variable_ops), ";;">;
