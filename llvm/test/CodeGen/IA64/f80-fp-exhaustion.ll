; RUN: llc -mtriple=ia64 < %s | FileCheck %s

; A named long double (f80) rides in one FP register while shadowing two
; parameter slots. If F8-F15 are already gone it cannot, and it falls back to
; the two general registers those slots name, in memory format - the same way a
; variadic long double is passed (psABI 8.5), and the same fallback gcc's
; ia64_function_arg_1 takes once cum->fp_regs is spent.
;
; That case used to be unreachable: every FP argument took one FP register per
; parameter slot, and an integer argument took a slot but no FP register, so
; F8-F15 could never run dry while slots remained. A single-precision HFA breaks
; the invariant - it takes two FP registers per slot (see hfa-fp-exhaustion.ll)
; - so it can drain F8-F15 with slots to spare, and a long double after one
; lands in this path.

target datalayout = "e-m:e-p:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "ia64"

; Two 4x-float HFAs take parameter slots 0-3 and all eight of F8-F15, leaving
; slots 4-7 free. The long double owns slots 4-5 (r36, r37), so the trailing
; integer keeps slot 6 (r38). Getting this wrong is not merely a misplaced long
; double: leaving slots 4-7 unallocated slides every later argument down a slot.

; CHECK-LABEL: ld_after_hfa#:
; CHECK: mov r8 = r38
define i64 @ld_after_hfa([4 x float] %a, [4 x float] %b, x86_fp80 %c, i64 %d) {
  ret i64 %d
}

; The same argument list seen from the long double's side. There is no
; instruction that rebuilds the 80-bit register format straight out of two
; general registers, so the halves go through a 16-byte temporary: st8 both,
; then ldfe. r36 holds the low 8 bytes of the memory image.

; CHECK-LABEL: ld_after_hfa_value#:
; CHECK-DAG: st8 [[[LO:r[0-9]+]]] = r36
; CHECK-DAG: st8 [{{r[0-9]+}}] = r37
; CHECK: ldfe f8 = [[[LO]]]
define x86_fp80 @ld_after_hfa_value([4 x float] %a, [4 x float] %b,
                                    x86_fp80 %c, i64 %d) {
  ret x86_fp80 %c
}

; Caller side, matching ld_after_hfa: stfe the long double to a temporary and
; reload its two memory-format halves into out4/out5, leaving out6 for the
; integer.

; CHECK-LABEL: call_ld_after_hfa#:
; CHECK: mov out6 = {{r[0-9]+}}
; CHECK: stfe [[[TMP:r[0-9]+]]] = f8
; CHECK-DAG: ld8 out4 = [[[TMP]]]
; CHECK-DAG: ld8 out5 = [{{r[0-9]+}}]
declare i64 @ld_callee([4 x float], [4 x float], x86_fp80, i64)
define i64 @call_ld_after_hfa(x86_fp80 %c, i64 %d) {
  %r = call i64 @ld_callee([4 x float] zeroinitializer,
                           [4 x float] zeroinitializer, x86_fp80 %c, i64 %d)
  ret i64 %r
}

; When the parameter slots really are exhausted there is no register pair to
; fall back to and the long double goes on the stack, as it always did. Seven
; doubles take slots 0-6 and F8-F14; the 2x-float HFA takes slot 7 (F15 for its
; first element, the low half of r39 for its second). Nothing is left, so the
; long double occupies two 16-byte-aligned stack slots at sp+16 and the trailing
; integer follows at sp+32.

; CHECK-LABEL: ld_after_full_slots#:
; CHECK: add {{r[0-9]+}} = 32, r12
; CHECK: ld8 r8 = [{{r[0-9]+}}]
define i64 @ld_after_full_slots(double %a, double %b, double %c, double %d,
                                double %e, double %f, double %g,
                                [2 x float] %v, x86_fp80 %ld, i64 %i) {
  ret i64 %i
}
