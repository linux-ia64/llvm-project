; RUN: llc -mtriple=ia64 < %s | FileCheck %s

; Inline assembly operand constraints. The backend recognises the GCC IA-64
; register constraints 'r' (general register) and 'f' (floating-point
; register), the immediate constraint 'i', and register clobbers.
;
; IA-64 symbol names carry a '#' suffix in the assembly, so function labels are
; "name#:".

; The 'r' constraint must accept integer values narrower than i64 (e.g. the
; i8/i32 an identity "black box" asm carries, as emitted by Rust's
; core::hint::black_box) -- the GR class only lists i64, so without a custom
; getRegForInlineAsmConstraint hook the generic exact-type search fails with
; "could not allocate input reg for constraint 'r'".

; CHECK-LABEL: black_box_i8#:
; CHECK: //APP
; CHECK: //NO_APP
; CHECK: br.ret
define i8 @black_box_i8(i8 %x) {
  %r = call i8 asm "", "=r,0"(i8 %x)
  ret i8 %r
}

; CHECK-LABEL: black_box_i32#:
; CHECK: br.ret
define i32 @black_box_i32(i32 %x) {
  %r = call i32 asm "", "=r,0"(i32 %x)
  ret i32 %r
}

; CHECK-LABEL: black_box_i64#:
; CHECK: br.ret
define i64 @black_box_i64(i64 %x) {
  %r = call i64 asm "", "=r,0"(i64 %x)
  ret i64 %r
}

; A separate input and output 'r' operand: the add happens in GRs named by asm.
; CHECK-LABEL: add_r#:
; CHECK: //APP
; CHECK: add r{{[0-9]+}} = r{{[0-9]+}}, r{{[0-9]+}}
; CHECK: //NO_APP
; CHECK: br.ret
define i64 @add_r(i64 %a, i64 %b) {
  %r = call i64 asm "add $0 = $1, $2", "=r,r,r"(i64 %a, i64 %b)
  ret i64 %r
}

; The 'i' constraint and an integer immediate operand are printed as a bare
; decimal literal (PrintAsmOperand's MO_Immediate case).
; CHECK-LABEL: add_imm#:
; CHECK: //APP
; CHECK: adds r{{[0-9]+}} = 42, r{{[0-9]+}}
; CHECK: //NO_APP
; CHECK: br.ret
define i64 @add_imm(i64 %a) {
  %r = call i64 asm "adds $0 = $2, $1", "=r,r,i"(i64 %a, i64 42)
  ret i64 %r
}

; An explicit register clobber (~{r14}) must be honoured: the asm body may write
; r14 freely. Just check the asm is emitted and we still return cleanly.
; CHECK-LABEL: clobber#:
; CHECK: //APP
; CHECK: add r{{[0-9]+}} = r{{[0-9]+}}, r{{[0-9]+}}
; CHECK: //NO_APP
; CHECK: br.ret
define i64 @clobber(i64 %a, i64 %b) {
  %r = call i64 asm "add $0 = $1, $2", "=r,r,r,~{r14}"(i64 %a, i64 %b)
  ret i64 %r
}

; The 'f' constraint must accept floating-point values in an FP register, for
; both f32 (an fnorm.s identity here) and f64.
; CHECK-LABEL: black_box_f32#:
; CHECK: //APP
; CHECK: //NO_APP
; CHECK: br.ret
define float @black_box_f32(float %x) {
  %r = call float asm "", "=f,0"(float %x)
  ret float %r
}

; CHECK-LABEL: black_box_f64#:
; CHECK: //APP
; CHECK: //NO_APP
; CHECK: br.ret
define double @black_box_f64(double %x) {
  %r = call double asm "", "=f,0"(double %x)
  ret double %r
}

; f80 ('long double') is wider than the FP class's representative type, so it is
; routed through the dedicated f80-only register class (otherwise the generic
; inline-asm tiling asserts). An identity black box must round-trip it in an FR.
; CHECK-LABEL: black_box_f80#:
; CHECK: //APP
; CHECK: //NO_APP
; CHECK: br.ret
define x86_fp80 @black_box_f80(x86_fp80 %x) {
  %r = call x86_fp80 asm "", "=f,0"(x86_fp80 %x)
  ret x86_fp80 %r
}

; The 'm' constraint is an indirect memory operand: IA-64 dereferences a single
; address register, printed as '[rN]'. A store through an output '=*m' operand:
; CHECK-LABEL: mem_store#:
; CHECK: //APP
; CHECK: st8 [r{{[0-9]+}}] = r{{[0-9]+}}
; CHECK: //NO_APP
; CHECK: br.ret
define void @mem_store(ptr %p, i64 %v) {
  call void asm "st8 $0 = $1", "=*m,r"(ptr elementtype(i64) %p, i64 %v)
  ret void
}

; A load through an input '*m' operand.
; CHECK-LABEL: mem_load#:
; CHECK: //APP
; CHECK: ld8 r{{[0-9]+}} = [r{{[0-9]+}}]
; CHECK: //NO_APP
; CHECK: br.ret
define i64 @mem_load(ptr %p) {
  %r = call i64 asm "ld8 $0 = $1", "=r,*m"(ptr elementtype(i64) %p)
  ret i64 %r
}
