; RUN: llc -mtriple=ia64 < %s | FileCheck %s

; Floating-point argument and return passing.
;
; FP registers legally hold both f32 and f64, so f32 args/returns are passed
; as-is in FRs without promotion to f64.
;   Regression bce6f5b2: f32 args were promoted to f64, forcing a spurious
;     TRUNCATE on a value already known to be 32-bit.
;   Regression a4a398ee: f32 returns were promoted to f64, emitting a spurious
;     fnorm.s on a value already f32.
;
; On IA-64, FP args do NOT consume a GR; the GR file is shadowed by *slot*, so
; an integer following an FP arg takes the next GR slot, not a slot keyed off
; the FP register number.
;   Regression 4330a190: shadowing keyed off the FP register index put the
;     trailing integer in the wrong GR.

target datalayout = "e-m:e-p:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "ia64"

; f32 returned directly in f8, no promotion/fnorm.
define float @f32_passthrough(float %a) {
; CHECK-LABEL: f32_passthrough#:
; CHECK-NOT: fnorm
; CHECK-NOT: trunc
  ret float %a
}

; f32 args in f8/f9, result returned directly; no extra rounding on the result.
define float @f32_add(float %a, float %b) {
; CHECK-LABEL: f32_add#:
; CHECK: fadd.s f8 = f8, f9
; CHECK-NOT: fnorm
  %r = fadd float %a, %b
  ret float %r
}

declare void @sink(double, i32)

; Callee: incoming i32 %b occupies GR slot 1 (r33), shadowed past the double.
define void @int_after_fp(double %a, i32 %b) {
; CHECK-LABEL: int_after_fp#:
; CHECK: mov out1 = r33
  call void @sink(double %a, i32 %b)
  ret void
}

; Caller: double in f8, the trailing i32 goes to GR out slot 1 (out1), not out2.
define void @call_int_after_fp() {
; CHECK-LABEL: call_int_after_fp#:
; CHECK-DAG: setf.d f8 =
; CHECK-DAG: out1 = 7,
; CHECK-NOT: out2 = 7,
  call void @sink(double 1.0, i32 7)
  ret void
}
