; RUN: llc -mtriple=ia64 < %s | FileCheck %s

; Regression 1f31fec9 ("Fix truncating FP stores").
;
; Itanium FP stores (stfs/stfd) do NOT round their FR operand; they just write
; the requested precision's bit pattern. A truncating store of a wider FP value
; into narrower memory must therefore first round the value with fnorm, then
; store. Selecting the truncstore directly to stfs/stfd stores an unrounded
; value and produces garbage. The fix marks these truncstores Expand
; (IA64ISelLowering.cpp setTruncStoreAction), so each emits fnorm-then-store.

target datalayout = "e-m:e-p:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "ia64"

; f64 -> f32: round to single, then single store.
define void @f64_to_f32(double %a, ptr %p) {
; CHECK-LABEL: f64_to_f32#:
; CHECK: fnorm.s [[V:f[0-9]+]] =
; CHECK: stfs {{\[}}{{r[0-9]+}}] = [[V]]
  %t = fptrunc double %a to float
  store float %t, ptr %p
  ret void
}

; f80 -> f32: round to single, then single store.
define void @f80_to_f32(x86_fp80 %a, ptr %p) {
; CHECK-LABEL: f80_to_f32#:
; CHECK: fnorm.s [[V:f[0-9]+]] =
; CHECK: stfs {{\[}}{{r[0-9]+}}] = [[V]]
  %t = fptrunc x86_fp80 %a to float
  store float %t, ptr %p
  ret void
}

; f80 -> f64: round to double, then double store.
define void @f80_to_f64(x86_fp80 %a, ptr %p) {
; CHECK-LABEL: f80_to_f64#:
; CHECK: fnorm.d [[V:f[0-9]+]] =
; CHECK: stfd {{\[}}{{r[0-9]+}}] = [[V]]
  %t = fptrunc x86_fp80 %a to double
  store double %t, ptr %p
  ret void
}
