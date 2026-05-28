; RUN: llc -mtriple=ia64 < %s | FileCheck %s

; Integer constant materialization.
;   14-bit immediates: adds rX = imm, r0
;   22-bit immediates: addl rX = imm, r0
;   wider:             movl rX = imm
;
; Regression baee1399 ("Limit ADDL to constant materialization"): ADDL used to
; have a general selection pattern and could fold a register operand, which
; cornered the register allocator (addl's GR3 destination class). It is now
; emitted only to materialize a constant (source r0); a register add of a wide
; constant materializes with movl and a separate add, never an addl that folds
; the register.

target datalayout = "e-m:e-p:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "ia64"

define i64 @small() {
; CHECK-LABEL: small#:
; CHECK: adds r8 = 100, r0
  ret i64 100
}

define i64 @medium() {
; CHECK-LABEL: medium#:
; CHECK: addl r8 = 1000000, r0
  ret i64 1000000
}

define i64 @big() {
; CHECK-LABEL: big#:
; CHECK: movl r8 = 1234605616436508552
  ret i64 1234605616436508552
}

; A wide constant added to a register: materialize with movl, then add.
; ADDL must not fold the register operand.
define i64 @add_wide_const(i64 %a) {
; CHECK-LABEL: add_wide_const#:
; CHECK: movl [[C:r[0-9]+]] = 1234605616436508552
; CHECK: add r8 = r32, [[C]]
; CHECK-NOT: addl {{r[0-9]+}} = {{r[0-9]+}}, r32
  %r = add i64 %a, 1234605616436508552
  ret i64 %r
}

; A small constant folds into adds, not addl.
define i64 @add_small_const(i64 %a) {
; CHECK-LABEL: add_small_const#:
; CHECK: adds r8 = 100, r32
  %r = add i64 %a, 100
  ret i64 %r
}
