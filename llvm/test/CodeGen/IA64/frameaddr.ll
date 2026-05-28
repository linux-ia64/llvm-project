; RUN: llc -mtriple=ia64 < %s | FileCheck %s

; Regression e1222d38 ("Implement FRAMEADDR lowering for zero depth").
;
; FRAMEADDR/RETURNADDR "lie about being legal", so they must be forced Custom
; and actually lowered. __builtin_frame_address(0) is the current frame pointer,
; which on IA-64 is the stack pointer r12.

target datalayout = "e-m:e-p:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "ia64"

declare ptr @llvm.frameaddress(i32)

define ptr @frameaddr_0() {
; CHECK-LABEL: frameaddr_0#:
; CHECK: mov {{r[0-9]+}} = r12
  %r = call ptr @llvm.frameaddress(i32 0)
  ret ptr %r
}
