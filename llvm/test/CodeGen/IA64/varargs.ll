; RUN: llc -mtriple=ia64 < %s | FileCheck %s

; Callee-side varargs lowering on IA-64.
;
; A variadic callee spills its incoming argument registers (out/in window
; r32..r39) into the frame so va_list can walk a contiguous image. Per the
; IA-64 psABI (§8.5.4) parameter slot i lives at offset 8*i-48 from the
; *incoming* sp; the named slot 0 (here %fmt in r32) is not homed, the
; variadic slots r33..r39 are.
;
; Regression test for 94b1587d ("Pass stacked arguments at sp+16 ... not
; sp+80"): the register-home spill offsets used to carry an extra +64, which
; mismatched gcc-built variadic callees. With a 64-byte frame here the
; incoming sp is r12+64, so slot i homes at [r12 + (8*i-48) + 64] = [r12+8*i+16]:
; the first variadic slot (r33, i=1) at [r12+24], the last (r39, i=7) at
; [r12+72]. The buggy +64 scheme would have put them at [r12+88]..[r12+136].

target datalayout = "e-m:e-p:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "ia64"

declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)

define i32 @va_callee(i32 %fmt, ...) {
; CHECK-LABEL: va_callee#:
entry:
; First variadic slot (r33) homes at [r12+24], not [r12+88].
; CHECK-DAG: add [[H1:r[0-9]+]] = 24, r12
; CHECK-DAG: st8 {{\[}}{{r[0-9]+}}] = r33
; Last register slot (r39) homes at [r12+72], not [r12+136]; slots are 8 apart.
; CHECK-DAG: add [[H7:r[0-9]+]] = 72, r12
; CHECK-DAG: st8 {{\[}}{{r[0-9]+}}] = r39
  %ap = alloca ptr, align 8
  call void @llvm.va_start(ptr %ap)
  %a = va_arg ptr %ap, i32
  %b = va_arg ptr %ap, i32
  call void @llvm.va_end(ptr %ap)
  %s = add i32 %a, %b
  ret i32 %s
}
