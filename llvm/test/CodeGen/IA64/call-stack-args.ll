; RUN: llc -mtriple=ia64 < %s | FileCheck %s

; The IA-64 SysV ABI passes the first 8 integer arguments in the outgoing
; register window (out0-out7); arguments beyond that spill to the memory
; stack starting at sp+16 (the 16-byte scratch area is reserved below it).
;
; Regression test for the outgoing stacked/memory-argument offset: an earlier
; version homed memory args at sp+64/sp+80, which corrupted the argument list
; seen by GCC-compiled variadic callees (e.g. clang-built bash calling printf).
; The first memory arg must land at [r12+16], the second at [r12+24].

target datalayout = "e-m:e-p:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "ia64"

declare i64 @callee(i64, i64, i64, i64, i64, i64, i64, i64, i64, i64)

define i64 @caller() {
; CHECK-LABEL: caller#:
entry:
; The 9th and 10th arguments go to the memory stack at sp+16 and sp+24.
; CHECK-DAG: adds [[R9:r[0-9]+]] = 16, r12
; CHECK-DAG: adds [[R10:r[0-9]+]] = 24, r12
; CHECK-DAG: st8 {{\[}}[[R9]]] =
; CHECK-DAG: st8 {{\[}}[[R10]]] =
;
; The first 8 arguments stay in the outgoing register window.
; CHECK-DAG: out0 = 1,
; CHECK-DAG: out7 = 8,
;
; CHECK: br.call.sptk rp = callee#
  %r = call i64 @callee(i64 1, i64 2, i64 3, i64 4, i64 5, i64 6, i64 7, i64 8, i64 9, i64 10)
  ret i64 %r
}
