; RUN: llc -mtriple=ia64 < %s | FileCheck %s

; Fused multiply-add. IA-64 has a single-rounding a*b+c F-unit op whose
; completer picks the precision: ".s" (f32), ".d" (f64), none (native 80-bit).
; The fnma/fms variants negate an operand. An explicit llvm.fma.fN intrinsic
; (as compiler_builtins' libm / rustc emit) must select the matching opcode for
; all three widths; f32 in particular needs its own pattern because an
; ISD::FMA(f32) node is produced regardless of contraction.

target datalayout = "e-m:e-p:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "ia64"

declare float @llvm.fma.f32(float, float, float)
declare double @llvm.fma.f64(double, double, double)
declare x86_fp80 @llvm.fma.f80(x86_fp80, x86_fp80, x86_fp80)

; f32: a*b+c
define float @fma_f32(float %a, float %b, float %c) {
; CHECK-LABEL: fma_f32#:
; CHECK: fma.s {{f[0-9]+}} = {{f[0-9]+}}, {{f[0-9]+}}, {{f[0-9]+}}
  %r = call float @llvm.fma.f32(float %a, float %b, float %c)
  ret float %r
}

; f32: a*b-c selects fms.s
define float @fms_f32(float %a, float %b, float %c) {
; CHECK-LABEL: fms_f32#:
; CHECK: fms.s {{f[0-9]+}} = {{f[0-9]+}}, {{f[0-9]+}}, {{f[0-9]+}}
  %nc = fneg float %c
  %r = call float @llvm.fma.f32(float %a, float %b, float %nc)
  ret float %r
}

; f32: -a*b+c selects fnma.s
define float @fnma_f32(float %a, float %b, float %c) {
; CHECK-LABEL: fnma_f32#:
; CHECK: fnma.s {{f[0-9]+}} = {{f[0-9]+}}, {{f[0-9]+}}, {{f[0-9]+}}
  %na = fneg float %a
  %r = call float @llvm.fma.f32(float %na, float %b, float %c)
  ret float %r
}

; A contractable fmul+fadd over f32 is now fused (isFMAFasterThanFMulAndFAdd
; returns true for f32), so it collapses to a single fma.s.
define float @contract_f32(float %a, float %b, float %c) {
; CHECK-LABEL: contract_f32#:
; CHECK: fma.s {{f[0-9]+}} = {{f[0-9]+}}, {{f[0-9]+}}, {{f[0-9]+}}
; CHECK-NOT: fmpy
; CHECK-NOT: fadd
  %m = fmul contract float %a, %b
  %r = fadd contract float %m, %c
  ret float %r
}

; Without a contract/fast flag, the multiply and add stay separate roundings.
define float @nocontract_f32(float %a, float %b, float %c) {
; CHECK-LABEL: nocontract_f32#:
; CHECK: fmpy.s {{f[0-9]+}} =
; CHECK: fadd.s {{f[0-9]+}} =
; CHECK-NOT: fma.s
  %m = fmul float %a, %b
  %r = fadd float %m, %c
  ret float %r
}

; f64 still selects fma.d.
define double @fma_f64(double %a, double %b, double %c) {
; CHECK-LABEL: fma_f64#:
; CHECK: fma.d {{f[0-9]+}} = {{f[0-9]+}}, {{f[0-9]+}}, {{f[0-9]+}}
  %r = call double @llvm.fma.f64(double %a, double %b, double %c)
  ret double %r
}

; x86_fp80 selects the no-completer native fma.
define x86_fp80 @fma_f80(x86_fp80 %a, x86_fp80 %b, x86_fp80 %c) {
; CHECK-LABEL: fma_f80#:
; CHECK: fma {{f[0-9]+}} = {{f[0-9]+}}, {{f[0-9]+}}, {{f[0-9]+}}
; CHECK-NOT: fma.
  %r = call x86_fp80 @llvm.fma.f80(x86_fp80 %a, x86_fp80 %b, x86_fp80 %c)
  ret x86_fp80 %r
}
