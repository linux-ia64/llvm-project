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
// Phase C ported the instruction *descriptions* (operands, asm strings, unit
// type, explicit flags / Defs / Uses) and wired -gen-instr-info, leaving every
// DAG selection pattern as an empty `[]` placeholder. Phase D (D6) restores
// only the patterns the Stage-1 acceptance test (plus.ll) exercises — the
// reg/reg `add` and the `retflag`/RET return — and wires -gen-dag-isel. The
// remaining patterns, PatLeaf predicates and the GETFD/BRCALL nodes stay
// deferred (calls, loads, FP, etc. are out of Stage-1 scope). The generic `ret`
// SDNode no longer exists, so the old `Pat<(ret), (RET)>` is dropped; RET is now
// matched directly from the target-specific `retflag` node.
//
//===----------------------------------------------------------------------===//

include "IA64InstrFormats.td"

//===----------------------------------------------------------------------===//
// IA-64 specific DAG Nodes.
//===----------------------------------------------------------------------===//

// RET_FLAG - return with a flag (glue) operand. LowerReturn emits this after
// copying the return values into place; it selects to the RET (br.ret)
// instruction below. SDNPVariadic is required so the instruction selector
// transfers the extra register operands LowerReturn glues onto this node (the
// return-value regs, e.g. r8) onto the selected RET machine instruction as
// implicit uses. Without it those registers look dead and the return-value
// copies (and everything feeding them) get eliminated, leaving just the
// 'alloc'. (ar.pfs is no longer among them: frame lowering now restores it from
// a parked stacked local in the epilogue, alongside rp.)
def retflag : SDNode<"IA64ISD::RET_FLAG", SDTNone,
                     [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

// Call-sequence markers are target-independent nodes (ISD::CALLSEQ_START/END)
// but each target declares them with its own operand type profile. IA-64 frame
// amounts are i64. ADJUSTCALLSTACKDOWN/UP select directly from these.
def SDT_IA64CallSeqStart : SDCallSeqStart<[SDTCisVT<0, i64>, SDTCisVT<1, i64>]>;
def SDT_IA64CallSeqEnd   : SDCallSeqEnd<[SDTCisVT<0, i64>, SDTCisVT<1, i64>]>;
def callseq_start : SDNode<"ISD::CALLSEQ_START", SDT_IA64CallSeqStart,
                           [SDNPHasChain, SDNPOutGlue]>;
def callseq_end : SDNode<"ISD::CALLSEQ_END", SDT_IA64CallSeqEnd,
                         [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

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
// Immediate predicates
//===----------------------------------------------------------------------===//

// The constant-materialization ladder: a 64-bit constant is realized by the
// cheapest instruction whose signed-immediate field it fits in. The three
// ranges nest (14 ⊂ 22 ⊂ 64); the AddedComplexity annotations on the
// materialization patterns below make TableGen pick the narrowest fit.

// immSExt14 - fits a signed 14-bit field (the 'adds' immediate).
def immSExt14 : PatLeaf<(i64 imm), [{
  int64_t v = (int64_t)N->getZExtValue();
  return (v <= 8191 && v >= -8192);
}]>;

// immSExt22 - fits a signed 22-bit field. 'mov r1 = imm22' is the architectural
// pseudo-op of 'addl r1 = imm22, r0' (the immediate is sign-extended to 64
// bits), so a single A-slot instruction covers this range without a full 'movl'.
def immSExt22 : PatLeaf<(i64 imm), [{
  int64_t v = (int64_t)N->getZExtValue();
  return (v < (1 << 21) && v >= -(1 << 21));
}]>;

// imm64 - matches any 64-bit constant; the unpredicated 'movl' catch-all.
def imm64 : PatLeaf<(i64 imm)>;

// is32ones - the constant 0x00000000FFFFFFFF, the mask of a 32->64 zero-extend
// ('zxt4'). Lets the i32->i64 zero-extend select to a single instruction.
def is32ones : PatLeaf<(i64 imm), [{
  return ((uint64_t)N->getZExtValue() == 0x00000000FFFFFFFFULL);
}]>;

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

def ADD  : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
           "add $dst = $src1, $src2",
           [(set GR:$dst, (add GR:$src1, GR:$src2))]>, isA;

def ADD1 : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
           "add $dst = $src1, $src2, 1", []>, isA;

// 'adds' (A4) is a register + signed-14-bit-immediate add. Its register
// operand r3 may be any GR, so it doubles as a general add-immediate.
let AddedComplexity = 2 in
def ADDS : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, s14imm:$imm),
           "adds $dst = $imm, $src1",
           [(set GR:$dst, (add GR:$src1, immSExt14:$imm))]>, isA;

// 'addl' (A5) carries a wider signed-22-bit immediate, but it pays for those
// extra bits with a 2-bit register field: its addend must be r0-r3 (in practice
// only r0, since r1/r2 are reserved and r3 is the lone allocatable member of
// GR03).
// Used for constant materialization only.
def ADDL : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR03:$src1, s22imm:$imm),
           "addl $dst = $imm, $src1", []>, isA;

def MOVL : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins s64imm:$imm),
           "movl $dst = $imm",
           [(set GR:$dst, imm64:$imm)]>, isLX;

// Bare-constant materialization ladder, narrowest-fit first (AddedComplexity
// 2 > 1 > 0): 'adds rX = imm14, r0' for 14-bit values, 'addl rX = imm22, r0'
// (the 'mov rX = imm22' pseudo-op; r0 is a valid GR03 addend) for 22-bit, and
// the unpredicated 'movl' (above) as the catch-all.
let AddedComplexity = 2 in
def : Pat<(i64 immSExt14:$imm), (ADDS r0, immSExt14:$imm)>;
let AddedComplexity = 1 in
def : Pat<(i64 immSExt22:$imm), (ADDL r0, immSExt22:$imm)>;

def ADDL_GA : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, globaladdress:$imm),
           "addl $dst = $imm, $src1", []>, isA;

// movl of a relocated symbol: materializes a full 64-bit symbol-relative value
// directly in the instruction (no GOT). Hand-selected (no DAG pattern) for the
// local-exec TLS sequence -- 'movl rX = @tprel(sym)' (R_IA64_TPREL64I) -- which
// must use a direct relocation since local-exec by definition skips the GOT;
// the 64-bit form dodges the addl imm22/r0-r3 restriction for any tp offset.
def MOVL_GA : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins globaladdress:$imm),
           "movl $dst = $imm", []>, isLX;

// hmm
def ADDL_EA : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, calltarget:$imm),
           "addl $dst = $imm, $src1", []>, isA;

def SUB  : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
           "sub $dst = $src1, $src2",
           [(set GR:$dst, (sub GR:$src1, GR:$src2))]>, isA;

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
           [(set GR:$dst, (sext_inreg GR:$src, i8))]>, isI;
def ZXT1 : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src), "zxt1 $dst = $src",
           [(set GR:$dst, (and GR:$src, 255))]>, isI;
def SXT2 : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src), "sxt2 $dst = $src",
           [(set GR:$dst, (sext_inreg GR:$src, i16))]>, isI;
def ZXT2 : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src), "zxt2 $dst = $src",
           [(set GR:$dst, (and GR:$src, 65535))]>, isI;
def SXT4 : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src), "sxt4 $dst = $src",
           [(set GR:$dst, (sext_inreg GR:$src, i32))]>, isI;
def ZXT4 : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src), "zxt4 $dst = $src",
           [(set GR:$dst, (and GR:$src, is32ones))]>, isI;

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

// IA-64 has no integer multiply unit; 'mul' is synthesized through the FP
// fixed-point multiply-add (xma) pipe. Move both operands into FP registers
// (setf.sig), multiply-add with F0 (the fixed +0.0) as the addend so the
// result is just the product -- xma.l yields the low 64 bits, which is exactly
// the low-half integer 'mul' -- then move the result back to a GR (getf.sig).
// The high 64 bits of a 64x64 product come from the same pipe: xma.hu for the
// unsigned high (mulhu), xma.h for the signed high (mulhs). These feed clang's
// divide-by-constant strength reduction (a magic-number multiply-high).
def : Pat<(mul GR:$src1, GR:$src2),
          (GETFSIGD (f64 (XMALD (f64 (SETFSIGD GR:$src1)),
                                (f64 (SETFSIGD GR:$src2)), (f64 F0))))>;
def : Pat<(mulhu GR:$src1, GR:$src2),
          (GETFSIGD (f64 (XMAHUD (f64 (SETFSIGD GR:$src1)),
                                 (f64 (SETFSIGD GR:$src2)), (f64 F0))))>;
def : Pat<(mulhs GR:$src1, GR:$src2),
          (GETFSIGD (f64 (XMAHD (f64 (SETFSIGD GR:$src1)),
                                (f64 (SETFSIGD GR:$src2)), (f64 F0))))>;

def AND   : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
          "and $dst = $src1, $src2",
          [(set GR:$dst, (and GR:$src1, GR:$src2))]>, isA;
def ANDCM : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
          "andcm $dst = $src1, $src2", []>, isA;
// TODO: and/andcm/or/xor/add/sub/shift immediate forms
def OR    : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
          "or $dst = $src1, $src2",
          [(set GR:$dst, (or GR:$src1, GR:$src2))]>, isA;

def pOR   : AForm<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2, PR:$qp),
          "($qp) or $dst = $src1, $src2">, isA;

// the following are all a bit unfortunate: we throw away the complement
// of the compare!
def CMPEQ : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins GR:$src1, GR:$src2),
          "cmp.eq $dst, p0 = $src1, $src2",
          [(set PR:$dst, (setcc GR:$src1, GR:$src2, SETEQ))]>, isA;
def CMPGT : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins GR:$src1, GR:$src2),
          "cmp.gt $dst, p0 = $src1, $src2",
          [(set PR:$dst, (setcc GR:$src1, GR:$src2, SETGT))]>, isA;
def CMPGE : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins GR:$src1, GR:$src2),
          "cmp.ge $dst, p0 = $src1, $src2",
          [(set PR:$dst, (setcc GR:$src1, GR:$src2, SETGE))]>, isA;
def CMPLT : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins GR:$src1, GR:$src2),
          "cmp.lt $dst, p0 = $src1, $src2",
          [(set PR:$dst, (setcc GR:$src1, GR:$src2, SETLT))]>, isA;
def CMPLE : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins GR:$src1, GR:$src2),
          "cmp.le $dst, p0 = $src1, $src2",
          [(set PR:$dst, (setcc GR:$src1, GR:$src2, SETLE))]>, isA;
def CMPNE : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins GR:$src1, GR:$src2),
          "cmp.ne $dst, p0 = $src1, $src2",
          [(set PR:$dst, (setcc GR:$src1, GR:$src2, SETNE))]>, isA;
def CMPLTU: AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins GR:$src1, GR:$src2),
          "cmp.ltu $dst, p0 = $src1, $src2",
          [(set PR:$dst, (setcc GR:$src1, GR:$src2, SETULT))]>, isA;
def CMPGTU: AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins GR:$src1, GR:$src2),
          "cmp.gtu $dst, p0 = $src1, $src2",
          [(set PR:$dst, (setcc GR:$src1, GR:$src2, SETUGT))]>, isA;
def CMPLEU: AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins GR:$src1, GR:$src2),
          "cmp.leu $dst, p0 = $src1, $src2",
          [(set PR:$dst, (setcc GR:$src1, GR:$src2, SETULE))]>, isA;
def CMPGEU: AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins GR:$src1, GR:$src2),
          "cmp.geu $dst, p0 = $src1, $src2",
          [(set PR:$dst, (setcc GR:$src1, GR:$src2, SETUGE))]>, isA;

// FP compares. Each sets a predicate from an IA-64 fcmp relation. The ordered
// relations (eq/lt/le/gt/ge) are false when an operand is NaN; the negated
// relations (nlt/nle/ngt/nge) and unord are true when unordered -- exactly the
// U-prefixed setcc semantics. SETONE/SETUEQ have no single relation and are
// expanded by the legalizer (setCondCodeAction) into a pair of these joined by
// the i1 and/or patterns.
def FCMPEQ : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins FP:$src1, FP:$src2),
          "fcmp.eq $dst, p0 = $src1, $src2",
          [(set PR:$dst, (setcc (f64 FP:$src1), (f64 FP:$src2), SETOEQ))]>, isF;
def FCMPGT : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins FP:$src1, FP:$src2),
          "fcmp.gt $dst, p0 = $src1, $src2",
          [(set PR:$dst, (setcc (f64 FP:$src1), (f64 FP:$src2), SETOGT))]>, isF;
def FCMPGE : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins FP:$src1, FP:$src2),
          "fcmp.ge $dst, p0 = $src1, $src2",
          [(set PR:$dst, (setcc (f64 FP:$src1), (f64 FP:$src2), SETOGE))]>, isF;
def FCMPLT : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins FP:$src1, FP:$src2),
          "fcmp.lt $dst, p0 = $src1, $src2",
          [(set PR:$dst, (setcc (f64 FP:$src1), (f64 FP:$src2), SETOLT))]>, isF;
def FCMPLE : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins FP:$src1, FP:$src2),
          "fcmp.le $dst, p0 = $src1, $src2",
          [(set PR:$dst, (setcc (f64 FP:$src1), (f64 FP:$src2), SETOLE))]>, isF;
def FCMPNE : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins FP:$src1, FP:$src2),
          "fcmp.neq $dst, p0 = $src1, $src2",
          [(set PR:$dst, (setcc (f64 FP:$src1), (f64 FP:$src2), SETUNE))]>, isF;
def FCMPORD : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins FP:$src1, FP:$src2),
          "fcmp.ord $dst, p0 = $src1, $src2",
          [(set PR:$dst, (setcc (f64 FP:$src1), (f64 FP:$src2), SETO))]>, isF;
def FCMPUNORD : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins FP:$src1, FP:$src2),
          "fcmp.unord $dst, p0 = $src1, $src2",
          [(set PR:$dst, (setcc (f64 FP:$src1), (f64 FP:$src2), SETUO))]>, isF;
def FCMPNLT : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins FP:$src1, FP:$src2),
          "fcmp.nlt $dst, p0 = $src1, $src2",
          [(set PR:$dst, (setcc (f64 FP:$src1), (f64 FP:$src2), SETUGE))]>, isF;
def FCMPNLE : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins FP:$src1, FP:$src2),
          "fcmp.nle $dst, p0 = $src1, $src2",
          [(set PR:$dst, (setcc (f64 FP:$src1), (f64 FP:$src2), SETUGT))]>, isF;
def FCMPNGT : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins FP:$src1, FP:$src2),
          "fcmp.ngt $dst, p0 = $src1, $src2",
          [(set PR:$dst, (setcc (f64 FP:$src1), (f64 FP:$src2), SETULE))]>, isF;
def FCMPNGE : AForm_DAG<0x03, 0x0b, (outs PR:$dst), (ins FP:$src1, FP:$src2),
          "fcmp.nge $dst, p0 = $src1, $src2",
          [(set PR:$dst, (setcc (f64 FP:$src1), (f64 FP:$src2), SETULT))]>, isF;

// The "plain" (NaN-don't-care) FP conditions, which the optimizer emits when
// NaN ordering is irrelevant (e.g. comparing a folded constant). Map them to
// the ordered relations -- NaN compares false, matching the hardware fcmp.
def : Pat<(setcc (f64 FP:$a), (f64 FP:$b), SETEQ), (FCMPEQ FP:$a, FP:$b)>;
def : Pat<(setcc (f64 FP:$a), (f64 FP:$b), SETGT), (FCMPGT FP:$a, FP:$b)>;
def : Pat<(setcc (f64 FP:$a), (f64 FP:$b), SETGE), (FCMPGE FP:$a, FP:$b)>;
def : Pat<(setcc (f64 FP:$a), (f64 FP:$b), SETLT), (FCMPLT FP:$a, FP:$b)>;
def : Pat<(setcc (f64 FP:$a), (f64 FP:$b), SETLE), (FCMPLE FP:$a, FP:$b)>;
def : Pat<(setcc (f64 FP:$a), (f64 FP:$b), SETNE), (FCMPNE FP:$a, FP:$b)>;

// f32 comparisons reuse the same (precision-independent) FCMP* instructions:
// the register compare looks at the full 82-bit register-format value, so a
// single-precision operand needs no separate compare. Mirror the f64 rules.
def : Pat<(setcc (f32 FP:$a), (f32 FP:$b), SETOEQ), (FCMPEQ FP:$a, FP:$b)>;
def : Pat<(setcc (f32 FP:$a), (f32 FP:$b), SETOGT), (FCMPGT FP:$a, FP:$b)>;
def : Pat<(setcc (f32 FP:$a), (f32 FP:$b), SETOGE), (FCMPGE FP:$a, FP:$b)>;
def : Pat<(setcc (f32 FP:$a), (f32 FP:$b), SETOLT), (FCMPLT FP:$a, FP:$b)>;
def : Pat<(setcc (f32 FP:$a), (f32 FP:$b), SETOLE), (FCMPLE FP:$a, FP:$b)>;
def : Pat<(setcc (f32 FP:$a), (f32 FP:$b), SETUNE), (FCMPNE FP:$a, FP:$b)>;
def : Pat<(setcc (f32 FP:$a), (f32 FP:$b), SETO),   (FCMPORD FP:$a, FP:$b)>;
def : Pat<(setcc (f32 FP:$a), (f32 FP:$b), SETUO),  (FCMPUNORD FP:$a, FP:$b)>;
def : Pat<(setcc (f32 FP:$a), (f32 FP:$b), SETUGE), (FCMPNLT FP:$a, FP:$b)>;
def : Pat<(setcc (f32 FP:$a), (f32 FP:$b), SETUGT), (FCMPNLE FP:$a, FP:$b)>;
def : Pat<(setcc (f32 FP:$a), (f32 FP:$b), SETULE), (FCMPNGT FP:$a, FP:$b)>;
def : Pat<(setcc (f32 FP:$a), (f32 FP:$b), SETULT), (FCMPNGE FP:$a, FP:$b)>;
def : Pat<(setcc (f32 FP:$a), (f32 FP:$b), SETEQ),  (FCMPEQ FP:$a, FP:$b)>;
def : Pat<(setcc (f32 FP:$a), (f32 FP:$b), SETGT),  (FCMPGT FP:$a, FP:$b)>;
def : Pat<(setcc (f32 FP:$a), (f32 FP:$b), SETGE),  (FCMPGE FP:$a, FP:$b)>;
def : Pat<(setcc (f32 FP:$a), (f32 FP:$b), SETLT),  (FCMPLT FP:$a, FP:$b)>;
def : Pat<(setcc (f32 FP:$a), (f32 FP:$b), SETLE),  (FCMPLE FP:$a, FP:$b)>;
def : Pat<(setcc (f32 FP:$a), (f32 FP:$b), SETNE),  (FCMPNE FP:$a, FP:$b)>;

def PCMPEQUNCR0R0 : AForm<0x03, 0x0b, (outs PR:$dst), (ins PR:$qp),
    "($qp) cmp.eq.unc $dst, p0 = r0, r0">, isA;

// materialization of PR (i1) constants using CMPNE/CMPEQ
def : Pat<(i1 0), (CMPNE r0, r0)>;
def : Pat<(i1 1), (CMPEQ r0, r0)>;
def : Pat<(i1 -1), (CMPEQ r0, r0)>;

let Constraints = "$bogus = $dst" in {
  def TPCMPEQR0R0 : AForm<0x03, 0x0b, (outs PR:$dst), (ins PR:$bogus, PR:$qp),
    "($qp) cmp.eq $dst, p0 = r0, r0">, isA;
  def TPCMPNER0R0 : AForm<0x03, 0x0b, (outs PR:$dst), (ins PR:$bogus, PR:$qp),
    "($qp) cmp.ne $dst, p0 = r0, r0">, isA;
}

def XOR   : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
          "xor $dst = $src1, $src2",
          [(set GR:$dst, (xor GR:$src1, GR:$src2))]>, isA;

def SHLADD: AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1,s64imm:$imm,GR:$src2),
          "shladd $dst = $src1, $imm, $src2", []>, isA;

def SHL   : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
          "shl $dst = $src1, $src2",
          [(set GR:$dst, (shl GR:$src1, GR:$src2))]>, isI;

def SHRU  : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
          "shr.u $dst = $src1, $src2",
          [(set GR:$dst, (srl GR:$src1, GR:$src2))]>, isI;

def SHRS  : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src1, GR:$src2),
          "shr $dst = $src1, $src2",
          [(set GR:$dst, (sra GR:$src1, GR:$src2))]>, isI;

def MOV : AForm<0x03, 0x0b, (outs GR:$dst), (ins GR:$src), "mov $dst = $src">, isA;
// Restore ar.pfs from a general register: 'mov ar.pfs = rN'. ar.pfs lives in
// its own (AR) register class, so this is a distinct instruction from the
// GR->GR MOV above; copyPhysReg emits it for copies into AR_PFS.
def MOV_TO_AR_PFS : AForm<0x03, 0x0b, (outs AR:$dst), (ins GR:$src),
  "mov $dst = $src">, isA;
// Move a general register into a branch register: 'mov b6 = rN'. Like ar.pfs
// above, b6 is in its own (BR) class; copyPhysReg emits this for copies into it.
def MOV_TO_BR : AForm<0x03, 0x0b, (outs BR:$dst), (ins GR:$src),
  "mov $dst = $src">, isA;
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

// IA-64 predicates rather than branching: lower (select p, t, f) to a single
// predicated move whose *false* value enters through the tied default operand,
// predicating only the *true* arm:
//   mov dst = f  ;;  (p) mov dst = t
// The complement predicate is never needed, so no bidirectional compare is
// required; the mandatory stop after the predicate-defining cmp covers the
// def->use hazard regardless. The i1 condition is already a PR (our setcc
// yields one), so select(setcc(..), t, f) consumes the cmp we already emit.
def : Pat<(i64 (select PR:$qp, GR:$t, GR:$f)), (CMOV  GR:$f, GR:$t, PR:$qp)>;
def : Pat<(f32 (select PR:$qp, FP:$t, FP:$f)), (CFMOV FP:$f, FP:$t, PR:$qp)>;
def : Pat<(f64 (select PR:$qp, FP:$t, FP:$f)), (CFMOV FP:$f, FP:$t, PR:$qp)>;
// Selecting one predicate from two: there is no predicated predicate-move, so
// widen each arm to a 0/1 GR (the zext-PR sequence), CMOV those, and compare the
// result back to a predicate (!= 0). Mirrors the pre-removal SELECTBOOL.
def : Pat<(i1 (select PR:$qp, PR:$t, PR:$f)),
          (CMPNE (CMOV (TPCADDS (ADDS r0, 0), 1, PR:$f),
                       (TPCADDS (ADDS r0, 0), 1, PR:$t), PR:$qp),
                 r0)>;

// load constants of various sizes // FIXME: prettyprint -ve constants

// TODO: support postincrement (reg, imm9) loads+stores - this needs more
// tablegen support

def IUSE : PseudoInstIA64<(outs), (ins variable_ops), "// IUSE">;
// Call-frame setup/teardown. Selected directly from callseq_start/callseq_end
// via these patterns (the modern idiom, as on Sparc) so TableGen threads the
// chain and glue for us -- the pre-removal backend hand-selected these, which
// predates the two-operand + glue callseq_end shape. They def/use sp (r12);
// IA64FrameLowering::eliminateCallFramePseudoInstr reads the byte count from
// operand 0 and turns them into sp adjustments (or erases them).
let Defs = [r12], Uses = [r12] in {
def ADJUSTCALLSTACKDOWN : PseudoInstIA64_DAG<(outs),
                          (ins i64imm:$amt1, i64imm:$amt2),
                          "// ADJUSTCALLSTACKDOWN",
                          [(callseq_start timm:$amt1, timm:$amt2)]>;
def ADJUSTCALLSTACKUP : PseudoInstIA64_DAG<(outs),
                        (ins i64imm:$amt1, i64imm:$amt2),
                        "// ADJUSTCALLSTACKUP",
                        [(callseq_end timm:$amt1, timm:$amt2)]>;
}
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

// extend PR into GR. zext/anyext set the low bit (0/1); sext fills the whole
// register, since for a 1-bit value the bit is the sign (set -> all-ones, -1).
def : Pat<(zext PR:$src),   (TPCADDS (ADDS r0, 0),  1, PR:$src)>;
def : Pat<(anyext PR:$src), (TPCADDS (ADDS r0, 0),  1, PR:$src)>;
def : Pat<(sext PR:$src),   (TPCADDS (ADDS r0, 0), -1, PR:$src)>;

// truncate GR into PR: the predicate is the low bit (extr.u then compare != 0)
def : Pat<(i1 (trunc GR:$src)), (CMPNE (EXTRU GR:$src, 0, 1), r0)>;

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

// Binary logical operations (implemented using conditional assignment)
def : Pat<(i1 (or  PR:$src1, PR:$src2)),
  // $dst <- 0
  // if ($src1) $dst <- 1
  // if ($src2) $dst <- 1
  (TPCMPEQ (TPCMPEQ (CMPNE r0, r0), r0, r0, $src1), r0, r0, $src2)>;
def : Pat<(i1 (and PR:$src1, PR:$src2)),
  // $dst <- $src1
  // $aux <- 1
  // if ($src2) $aux <- 0
  // if ($aux)  $dst <- 0
  (TPCMPNE (TPCMPEQ (CMPNE r0, r0), r0, r0, $src1), r0, r0,
    (TPCMPNE (CMPEQ r0, r0), r0, r0, $src2))>;
def : Pat<(i1 (xor PR:$src1, PR:$src2)),
  // $aux1 <- $src1
  // if ($src2) $aux1 <- 0
  // $aux2 <- $src2
  // if ($src1) $aux2 <- 0
  // $dst <- $p1
  // if ($aux2) $dst <- 1
  (TPCMPEQ
    (TPCMPNE (TPCMPEQ (CMPNE r0, r0), r0, r0, $src1), r0, r0, $src2),
    r0, r0,
    (TPCMPNE (TPCMPEQ (CMPNE r0, r0), r0, r0, $src2), r0, r0, $src1))>;

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
  // Store the 80-bit double-extended (long double) memory format -- 10 bytes,
  // the C 'long double' object format. Distinct from stf.spill (16-byte raw
  // register format used for vreg spills).
  def STFE : AForm<0x03, 0x0b, (outs), (ins GR:$dstPtr, FP:$value),
    "stfe [$dstPtr] = $value">, isM;
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
  // Load the 80-bit double-extended (long double) memory format; see STFE.
  def LDFE : AForm<0x03, 0x0b, (outs FP:$dst), (ins GR:$srcPtr),
    "ldfe $dst = [$srcPtr]">, isM;
}

def POPCNT : AForm_DAG<0x03, 0x0b, (outs GR:$dst), (ins GR:$src),
  "popcnt $dst = $src", [(set GR:$dst, (ctpop GR:$src))]>, isI;

// Memory fence. IA-64 ld/st are unordered beyond data dependence, so every
// atomic ordering above monotonic is realised as an 'mf' barrier (the type
// legalizer / AtomicExpand reduces acquire/release/seq_cst to monotonic
// access + fence). One full fence covers all of them. The atomic_fence node
// carries chain + side-effect (but not mayLoad/mayStore), so hasSideEffects=1
// matches its inferred properties and keeps the mf from being reordered or
// deleted.
let hasSideEffects = 1 in
def MF : AForm<0x03, 0x0b, (outs), (ins), "mf">, isM;
def : Pat<(atomic_fence timm, timm), (MF)>;

// Atomic compare-and-exchange. The IA-64 cmpxchg takes its comparand from the
// ar.ccv application register, so the comparand is first moved there by
// MOV_TO_AR_CCV; the cmpxchg then atomically compares ar.ccv against the memory
// word, stores $new on a match, and returns the original word (zero-extended
// into the GR for the narrow forms). Both are selected by hand in
// IA64ISelDAGToDAG: the mov is glued immediately before the cmpxchg, and the
// ar.ccv physreg def/use makes the bundler insert the required stop between
// them. The .acq completer gives the access acquire ordering; the
// release/seq_cst side is an 'mf' the selector prepends.
let Defs = [AR_CCV] in
def MOV_TO_AR_CCV : AForm<0x03, 0x0b, (outs), (ins GR:$src),
  "mov ar.ccv = $src">, isM;

let mayLoad = 1, mayStore = 1, hasSideEffects = 1, Uses = [AR_CCV] in {
  def CMPXCHG1 : AForm<0x03, 0x0b, (outs GR:$dst), (ins GR:$ptr, GR:$new),
    "cmpxchg1.acq $dst = [$ptr], $new, ar.ccv">, isM;
  def CMPXCHG2 : AForm<0x03, 0x0b, (outs GR:$dst), (ins GR:$ptr, GR:$new),
    "cmpxchg2.acq $dst = [$ptr], $new, ar.ccv">, isM;
  def CMPXCHG4 : AForm<0x03, 0x0b, (outs GR:$dst), (ins GR:$ptr, GR:$new),
    "cmpxchg4.acq $dst = [$ptr], $new, ar.ccv">, isM;
  def CMPXCHG8 : AForm<0x03, 0x0b, (outs GR:$dst), (ins GR:$ptr, GR:$new),
    "cmpxchg8.acq $dst = [$ptr], $new, ar.ccv">, isM;
}

// some FP stuff:
// Double-precision (f64) arithmetic carries the .d completer (53-bit rounding);
// the unsuffixed forms below are the extended (f80) ops, and the .s forms are
// single (f32). Naming mirrors the completer: FADDD/.d, FADD/none, FADDS/.s.
def FADDD : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2),
  "fadd.d $dst = $src1, $src2",
  [(set FP:$dst, (f64 (fadd FP:$src1, FP:$src2)))]>, isF;
def FADDS : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2),
  "fadd.s $dst = $src1, $src2",
  [(set FP:$dst, (f32 (fadd FP:$src1, FP:$src2)))]>, isF;
def FSUBD : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2),
  "fsub.d $dst = $src1, $src2",
  [(set FP:$dst, (f64 (fsub FP:$src1, FP:$src2)))]>, isF;
def FSUBS : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2),
  "fsub.s $dst = $src1, $src2",
  [(set FP:$dst, (f32 (fsub FP:$src1, FP:$src2)))]>, isF;
def FMPYD : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2),
  "fmpy.d $dst = $src1, $src2",
  [(set FP:$dst, (f64 (fmul FP:$src1, FP:$src2)))]>, isF;
def FMPYS : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2),
  "fmpy.s $dst = $src1, $src2",
  [(set FP:$dst, (f32 (fmul FP:$src1, FP:$src2)))]>, isF;
// Fused multiply-add forms. These match the ISD::FMA node (a single-rounding
// a*b+c), which the DAG only forms when FP contraction is permitted -- so unlike
// the pre-removal (fadd (fmul ...)) pattern they never silently fuse. fms is
// a*b-c, fnma is c-a*b (= (-a)*b+c).
def FMAD : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2, FP:$src3),
  "fma.d $dst = $src1, $src2, $src3",
  [(set FP:$dst, (f64 (fma FP:$src1, FP:$src2, FP:$src3)))]>, isF;
def FMSD : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2, FP:$src3),
  "fms.d $dst = $src1, $src2, $src3",
  [(set FP:$dst, (f64 (fma FP:$src1, FP:$src2, (fneg FP:$src3))))]>, isF;
def FNMAD : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2, FP:$src3),
  "fnma.d $dst = $src1, $src2, $src3",
  [(set FP:$dst, (f64 (fma (fneg FP:$src1), FP:$src2, FP:$src3)))]>, isF;
// f32 single-rounding forms (fma.s). compiler_builtins' libm calls
// llvm.fma.f32 directly, which produces an f32 ISD::FMA node regardless of
// contraction, so f32 needs its own pattern.
def FMAS : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2, FP:$src3),
  "fma.s $dst = $src1, $src2, $src3",
  [(set FP:$dst, (f32 (fma FP:$src1, FP:$src2, FP:$src3)))]>, isF;
def FMSS : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2, FP:$src3),
  "fms.s $dst = $src1, $src2, $src3",
  [(set FP:$dst, (f32 (fma FP:$src1, FP:$src2, (fneg FP:$src3))))]>, isF;
def FNMAS : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2, FP:$src3),
  "fnma.s $dst = $src1, $src2, $src3",
  [(set FP:$dst, (f32 (fma (fneg FP:$src1), FP:$src2, FP:$src3)))]>, isF;

// Operations only affecting sign do not care about width, so there is just one
// instruction for all of f32/f64/f80.
def FABS : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src),
  "fabs $dst = $src", []>, isF;
def FNEG : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src),
  "fneg $dst = $src", []>, isF;
def FNEGABS : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src),
  "fnegabs $dst = $src", []>, isF;
foreach vt = [f32, f64, f80] in {
  def : Pat<(vt (fabs vt:$src)), (FABS FP:$src)>;
  def : Pat<(vt (fneg vt:$src)), (FNEG FP:$src)>;
  def : Pat<(vt (fneg (fabs vt:$src))), (FNEGABS FP:$src)>;
}

// Extended-precision (f80 / 'long double') arithmetic. With no precision
// completer the F-unit rounds to the dynamic precision of status field sf0,
// which the SysV ABI initialises to 80-bit double-extended -- exactly the C
// long double. (FADDD etc. force .d / 53-bit; FADDS forces .s.) Memory format
// is the 10-byte ldfe/stfe; the 82-bit register holds it exactly.
def FADD : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2),
  "fadd $dst = $src1, $src2",
  [(set FP:$dst, (f80 (fadd FP:$src1, FP:$src2)))]>, isF;
def FSUB : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2),
  "fsub $dst = $src1, $src2",
  [(set FP:$dst, (f80 (fsub FP:$src1, FP:$src2)))]>, isF;
def FMPY : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2),
  "fmpy $dst = $src1, $src2",
  [(set FP:$dst, (f80 (fmul FP:$src1, FP:$src2)))]>, isF;
def FMA : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2, FP:$src3),
  "fma $dst = $src1, $src2, $src3",
  [(set FP:$dst, (f80 (fma FP:$src1, FP:$src2, FP:$src3)))]>, isF;
def FMS : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2, FP:$src3),
  "fms $dst = $src1, $src2, $src3",
  [(set FP:$dst, (f80 (fma FP:$src1, FP:$src2, (fneg FP:$src3))))]>, isF;
def FNMA : AForm_DAG<0x03, 0x0b, (outs FP:$dst), (ins FP:$src1, FP:$src2, FP:$src3),
  "fnma $dst = $src1, $src2, $src3",
  [(set FP:$dst, (f80 (fma (fneg FP:$src1), FP:$src2, FP:$src3)))]>, isF;

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
def FNORMS : AForm<0x03, 0x0b, (outs FP:$dst), (ins FP:$src),
  "fnorm.s $dst = $src">, isF;

// FP register<->register rounding/widening.
// Rounding uses FNORM-type instructions (FNORMD for f80->f64, FNORMS for
// f80/f64->f32).
// Widening needs no instruction as all sources of f32/f64 are correctly
// widened already.
def : Pat<(f64 (fpround  FP:$src)),         (FNORMD FP:$src)>;  // f80 -> f64
def : Pat<(f32 (fpround (f64 FP:$src))),    (FNORMS FP:$src)>;
def : Pat<(f32 (fpround (f80 FP:$src))),    (FNORMS FP:$src)>;
def : Pat<(f64 (fpextend FP:$src)),         (COPY_TO_REGCLASS FP:$src, FP)>;
def : Pat<(f80 (fpextend (f32 FP:$src))),   (COPY_TO_REGCLASS FP:$src, FP)>;
def : Pat<(f80 (fpextend (f64 FP:$src))),   (COPY_TO_REGCLASS FP:$src, FP)>;

// f80 comparisons reuse the (precision-independent) FCMP* instructions; the
// register compare looks at the full 82-bit value. Mirror the f64 SETCC rules.
def : Pat<(setcc (f80 FP:$a), (f80 FP:$b), SETOEQ), (FCMPEQ FP:$a, FP:$b)>;
def : Pat<(setcc (f80 FP:$a), (f80 FP:$b), SETOGT), (FCMPGT FP:$a, FP:$b)>;
def : Pat<(setcc (f80 FP:$a), (f80 FP:$b), SETOGE), (FCMPGE FP:$a, FP:$b)>;
def : Pat<(setcc (f80 FP:$a), (f80 FP:$b), SETOLT), (FCMPLT FP:$a, FP:$b)>;
def : Pat<(setcc (f80 FP:$a), (f80 FP:$b), SETOLE), (FCMPLE FP:$a, FP:$b)>;
def : Pat<(setcc (f80 FP:$a), (f80 FP:$b), SETUNE), (FCMPNE FP:$a, FP:$b)>;
def : Pat<(setcc (f80 FP:$a), (f80 FP:$b), SETO),   (FCMPORD FP:$a, FP:$b)>;
def : Pat<(setcc (f80 FP:$a), (f80 FP:$b), SETUO),  (FCMPUNORD FP:$a, FP:$b)>;
def : Pat<(setcc (f80 FP:$a), (f80 FP:$b), SETUGE), (FCMPNLT FP:$a, FP:$b)>;
def : Pat<(setcc (f80 FP:$a), (f80 FP:$b), SETUGT), (FCMPNLE FP:$a, FP:$b)>;
def : Pat<(setcc (f80 FP:$a), (f80 FP:$b), SETULE), (FCMPNGT FP:$a, FP:$b)>;
def : Pat<(setcc (f80 FP:$a), (f80 FP:$b), SETULT), (FCMPNGE FP:$a, FP:$b)>;
def : Pat<(setcc (f80 FP:$a), (f80 FP:$b), SETEQ),  (FCMPEQ FP:$a, FP:$b)>;
def : Pat<(setcc (f80 FP:$a), (f80 FP:$b), SETGT),  (FCMPGT FP:$a, FP:$b)>;
def : Pat<(setcc (f80 FP:$a), (f80 FP:$b), SETGE),  (FCMPGE FP:$a, FP:$b)>;
def : Pat<(setcc (f80 FP:$a), (f80 FP:$b), SETLT),  (FCMPLT FP:$a, FP:$b)>;
def : Pat<(setcc (f80 FP:$a), (f80 FP:$b), SETLE),  (FCMPLE FP:$a, FP:$b)>;
def : Pat<(setcc (f80 FP:$a), (f80 FP:$b), SETNE),  (FCMPNE FP:$a, FP:$b)>;

// Integer <-> floating-point conversions.
// Converted integer lands in f80 form (full 82-bit width), normalize using
// FNORM if f64/f32 requested.
// Converted float is always rounded to zero (.trunc variant of fcvt.fx).
def : Pat<(f80 (sint_to_fp GR:$src)), (FCVTXF  (SETFSIGD GR:$src))>;
def : Pat<(f80 (uint_to_fp GR:$src)), (FCVTXUF (SETFSIGD GR:$src))>;
def : Pat<(f64 (sint_to_fp GR:$src)), (FNORMD (FCVTXF (SETFSIGD GR:$src)))>;
def : Pat<(f64 (uint_to_fp GR:$src)), (FNORMD (FCVTXUF (SETFSIGD GR:$src)))>;
def : Pat<(f32 (sint_to_fp GR:$src)), (FNORMS (f64 (FCVTXF  (f64 (SETFSIGD GR:$src)))))>;
def : Pat<(f32 (uint_to_fp GR:$src)), (FNORMS (f64 (FCVTXUF (f64 (SETFSIGD GR:$src)))))>;
foreach vt = [f32, f64, f80] in {
  def : Pat<(i64 (fp_to_sint (vt FP:$src))), (GETFSIGD (FCVTFXTRUNC  FP:$src))>;
  def : Pat<(i64 (fp_to_uint (vt FP:$src))), (GETFSIGD (FCVTFXUTRUNC FP:$src))>;
}

def GETFD : AForm<0x03, 0x0b, (outs GR:$dst), (ins FP:$src),
  "getf.d $dst = $src">, isM;
def SETFD : AForm<0x03, 0x0b, (outs FP:$dst), (ins GR:$src),
  "setf.d $dst = $src">, isM;

def GETFSIG : AForm<0x03, 0x0b, (outs GR:$dst), (ins FP:$src),
  "getf.sig $dst = $src">, isM;
def SETFSIG : AForm<0x03, 0x0b, (outs FP:$dst), (ins GR:$src),
  "setf.sig $dst = $src">, isM;

// Implement f64/f32 immediates using i64 immediates
def fpimm_bits64 : SDNodeXForm<fpimm, [{
  APFloat F = N->getValueAPF();
  bool Ignored;
  F.convert(APFloat::IEEEdouble(), APFloat::rmNearestTiesToEven, &Ignored);
  return CurDAG->getTargetConstant(F.bitcastToAPInt().getZExtValue(),
                                   SDLoc(N), MVT::i64);
}]>;
def : Pat<(f64 fpimm:$v), (SETFD (MOVL (fpimm_bits64 $v)))>;
def : Pat<(f32 fpimm:$v), (SETFD (MOVL (fpimm_bits64 $v)))>;

// Reinterpret an f64's bits as an i64 and back (getf.d / setf.d), the 64-bit
// IEEE memory form -- used to pass a variadic double in a general register
// (see CC_IA64_Call_VarArgFP).
def : Pat<(i64 (bitconvert (f64 FP:$src))), (GETFD FP:$src)>;
def : Pat<(f64 (bitconvert (i64 GR:$src))), (SETFD GR:$src)>;

let isTerminator = 1, isBranch = 1 in {
  let isBarrier = 1 in
  def BRL_NOTCALL : RawForm<0x03, 0xb0, (outs), (ins i64imm:$dst),
    "(p0) brl.cond.sptk $dst">, isB;
  def BRLCOND_NOTCALL : RawForm<0x03, 0xb0, (outs), (ins PR:$qp, i64imm:$dst),
    "($qp) brl.cond.sptk $dst">, isB;
  def BRCOND_NOTCALL : RawForm<0x03, 0xb0, (outs), (ins PR:$qp, GR:$dst),
    "($qp) br.cond.sptk $dst">, isB;
  // Indirect branch (computed goto): the target address is moved into a branch
  // register (b6) and branched to. Unconditional, so a barrier.
  let isBarrier = 1, isIndirectBranch = 1 in
  def BRINDIRECT : RawForm<0x03, 0xb0, (outs), (ins BR:$target),
    "(p0) br.cond.sptk $target">, isB;
}

let isCall = 1, /* isTerminator = 1, isBranch = 1, */
  // A non-local callee may clobber gp (r1) via an import stub; the call lowering
  // can't know that statically, so AdjustInstrPostInstrSelection adds r1 to the
  // Defs of a non-dso_local call after selection. hasPostISelHook routes the
  // call instructions through that hook.
  hasPostISelHook = 1,
// all calls clobber non-callee-saved registers, and for now, they are these:
// rp (b0) leads the list: br.call writes the return address into it, so every
// call clobbers it. Without this the rp save/restore LowerCall emits around a
// call is a no-op round trip (rp looks unchanged) and the coalescer deletes it,
// leaving b0 corrupted across the call -> the function's own br.ret jumps to
// garbage (observed as SIGILL on a recursive call).
  Defs = [rp,
  r2,r3,r8,r9,r10,r11,r14,r15,r16,r17,r18,r19,r20,r21,r22,r23,r24,
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
  def BRCALL_INDIRECT : RawForm<0x03, 0xb0, (outs), (ins BR:$branchreg),
  "br.call.sptk rp = $branchreg">, isB;
  def BRLCOND_CALL : RawForm<0x03, 0xb0, (outs), (ins PR:$qp, i64imm:$dst),
    "($qp) brl.cond.call.sptk $dst">, isB;
  def BRCOND_CALL : RawForm<0x03, 0xb0, (outs), (ins PR:$qp, GR:$dst),
    "($qp) br.cond.call.sptk $dst">, isB;
}

// Return branch: br.ret reads the return pointer (b0/rp) and ar.pfs (to restore
// the caller's register stack frame). Declaring those uses lets the bundling
// pass insert the mandatory stop between the epilogue's 'mov rp = <reg>' /
// 'mov ar.pfs = <reg>' and this branch; without it br.ret could read the stale
// values from the same instruction group (and writing ar.pfs in br.ret's group
// is an RSE hazard).
let isTerminator = 1, isReturn = 1, Uses = [rp, AR_PFS] in
  def RET : AForm_DAG<0x03, 0x0b, (outs), (ins),
            "br.ret.sptk.many rp",
            [(retflag)]>, isB; // return

// the evil stop bit of despair
def STOP : PseudoInstIA64<(outs), (ins variable_ops), ";;">;
