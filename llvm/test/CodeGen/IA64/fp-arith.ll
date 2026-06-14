; RUN: llc -mtriple=ia64 < %s | FileCheck %s

; Floating-point arithmetic and the precision completer.
;
; IA-64 FP registers are 82-bit; the arithmetic opcode carries a completer
; selecting the rounding precision: ".s" for f32, ".d" for f64, and none for
; the native 80-bit (full) precision used by x86_fp80.
;
; Regression a5dbe312 ("Set f64 precision on f64 arithmetic"): f64 ops were
; emitted without the ".d" completer, computing at full precision.
; Regression a33ad26c ("Remove redundant FNORM on FP widening"): fpext emitted
; a bare fnorm even though FR values are already stored widened to 80 bits.

target datalayout = "e-m:e-p:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "ia64"

define double @fadd_f64(double %a, double %b) {
; CHECK-LABEL: fadd_f64#:
; CHECK: fadd.d {{f[0-9]+}} =
  %r = fadd double %a, %b
  ret double %r
}

define double @fsub_f64(double %a, double %b) {
; CHECK-LABEL: fsub_f64#:
; CHECK: fsub.d {{f[0-9]+}} =
  %r = fsub double %a, %b
  ret double %r
}

define double @fmul_f64(double %a, double %b) {
; CHECK-LABEL: fmul_f64#:
; CHECK: fmpy.d {{f[0-9]+}} =
  %r = fmul double %a, %b
  ret double %r
}

define float @fadd_f32(float %a, float %b) {
; CHECK-LABEL: fadd_f32#:
; CHECK: fadd.s {{f[0-9]+}} =
  %r = fadd float %a, %b
  ret float %r
}

define float @fmul_f32(float %a, float %b) {
; CHECK-LABEL: fmul_f32#:
; CHECK: fmpy.s {{f[0-9]+}} =
  %r = fmul float %a, %b
  ret float %r
}

; x86_fp80 is the native 80-bit precision: the opcode carries no completer.
define x86_fp80 @fadd_f80(x86_fp80 %a, x86_fp80 %b) {
; CHECK-LABEL: fadd_f80#:
; CHECK: fadd {{f[0-9]+}} =
; CHECK-NOT: fadd.
  %r = fadd x86_fp80 %a, %b
  ret x86_fp80 %r
}

; Widening f32->f64 is a no-op: the FR value is already 80-bit. No fnorm.
define double @fpext_f32_f64(float %a) {
; CHECK-LABEL: fpext_f32_f64#:
; CHECK-NOT: fnorm
  %r = fpext float %a to double
  ret double %r
}
