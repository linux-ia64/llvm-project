; RUN: llc -mtriple=ia64 < %s | FileCheck %s

; Floating-point comparison, select, and the sign-manipulation ops.
; fneg/fabs/fnegabs are precision-agnostic (no completer); fcmp sets a predicate
; pair; an FP select is a predicated FR move.
;
; Regression b85d0d0a ("Fix select of f32 FNEG/FABS/FABSNEG"): these ops were
; implemented for f64/f80 but accidentally omitted for f32.

target datalayout = "e-m:e-p:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "ia64"

declare float @llvm.fabs.f32(float)

define float @fneg_f32(float %a) {
; CHECK-LABEL: fneg_f32#:
; CHECK: fneg f8 = f8
  %r = fneg float %a
  ret float %r
}

define float @fabs_f32(float %a) {
; CHECK-LABEL: fabs_f32#:
; CHECK: fabs f8 = f8
  %r = call float @llvm.fabs.f32(float %a)
  ret float %r
}

define float @fnegabs_f32(float %a) {
; CHECK-LABEL: fnegabs_f32#:
; CHECK: fnegabs f8 = f8
  %t = call float @llvm.fabs.f32(float %a)
  %r = fneg float %t
  ret float %r
}

define double @fneg_f64(double %a) {
; CHECK-LABEL: fneg_f64#:
; CHECK: fneg f8 = f8
  %r = fneg double %a
  ret double %r
}

define i1 @fcmp_olt(float %a, float %b) {
; CHECK-LABEL: fcmp_olt#:
; CHECK: fcmp.lt p{{[0-9]+}}, p{{[0-9]+}} = f8, f9
  %r = fcmp olt float %a, %b
  ret i1 %r
}

; An FP select is lowered to a predicated FR move.
define float @fselect(float %a, float %b, i1 %c) {
; CHECK-LABEL: fselect#:
; CHECK: ({{p[0-9]+}}) mov {{f[0-9]+}} = {{f[0-9]+}}
  %r = select i1 %c, float %a, float %b
  ret float %r
}
