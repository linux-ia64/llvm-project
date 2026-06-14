; RUN: llc -mtriple=ia64 < %s | FileCheck %s

; Regression 48f447fc ("Save rp/gp/sp outside RSE for double return functions").
;
; A call to a returns_twice function (setjmp) can be re-entered via longjmp, at
; which point the Register Stack Engine state is gone. gp (r1), sp (r12) and the
; return pointer must therefore NOT be parked in stacked registers across such a
; call; they are held in the static callee-saved registers r4/r6/r7 (themselves
; spilled to the frame in the prologue), so they survive the second return.

target datalayout = "e-m:e-p:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "ia64"

declare i32 @setjmp(ptr) returns_twice
declare void @use(i32)

define i32 @doublereturn(ptr %env) {
; CHECK-LABEL: doublereturn#:
; Prologue spills the callee-saved scratch registers to the frame.
; CHECK-DAG: st8 {{\[}}{{r[0-9]+}}] = r4
; CHECK-DAG: st8 {{\[}}{{r[0-9]+}}] = r6
; CHECK-DAG: st8 {{\[}}{{r[0-9]+}}] = r7
;
; Park gp/sp/rp in callee-saved r4/r6/r7 (not stacked regs) around the call.
; CHECK: mov r4 = r1
; CHECK: mov r6 = r12
; CHECK: mov r7 = rp
; CHECK: br.call.sptk rp = setjmp#
; CHECK: mov r1 = r4
; CHECK: mov r12 = r6
; CHECK: mov rp = r7
entry:
  %r = call i32 @setjmp(ptr %env)
  call void @use(i32 %r)
  ret i32 %r
}
