; RUN: llc -mtriple=ia64 < %s | FileCheck %s

; Direct and indirect calls.
;
; Integer arguments are passed in the outgoing register window out0..out7; the
; call is a br.call through rp. An indirect call loads the function descriptor
; (entry point + gp), sets gp, and branches through a branch register.
;
; Regression c7908264 ("Fix bug in BRCALL selection"): BRCALL's register-use
; operands sit before the optional InGlue; selection used to look for InGlue at
; a fixed position and mis-shuffled the operands. Both call forms below carry
; glued register uses and exercise that path.

target datalayout = "e-m:e-p:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "ia64"

declare i64 @f3(i64, i64, i64)

define i64 @call_direct(i64 %a, i64 %b, i64 %c) {
; CHECK-LABEL: call_direct#:
; CHECK-DAG: mov out0 = r32
; CHECK-DAG: mov out1 = r33
; CHECK-DAG: mov out2 = r34
; CHECK: br.call.sptk rp = f3#
  %r = call i64 @f3(i64 %a, i64 %b, i64 %c)
  ret i64 %r
}

define i64 @call_indirect(ptr %fp, i64 %a, i64 %b) {
; CHECK-LABEL: call_indirect#:
; CHECK-DAG: mov out0 = r33
; CHECK-DAG: mov out1 = r34
; The descriptor's gp is loaded into r1, the entry point into a branch register.
; CHECK: mov b6 = {{r[0-9]+}}
; CHECK: br.call.sptk rp = b6
  %r = call i64 %fp(i64 %a, i64 %b)
  ret i64 %r
}
