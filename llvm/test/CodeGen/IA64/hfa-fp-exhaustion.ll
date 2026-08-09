; RUN: llc -mtriple=ia64 < %s | FileCheck %s

; An f32 homogeneous floating-point aggregate (HFA) packs two elements into
; each 64-bit GR shadow slot but still gives each element its own FP register
; (see hfa-shadow-slots.ll). So, unlike every other FP argument, it consumes
; F8-F15 twice as fast as it consumes parameter slots: the FP registers can run
; out while slots remain. The psABI then passes the leftover elements in the GR
; parameter slot itself, two f32 per 64-bit slot.
;
; Regression test: CC_IA64_FP_Common used to assume "a free slot implies a free
; FP register" and fed the result of AllocateReg(FPRegs) to
; CCValAssign::getReg() unchecked. Once F8-F15 were gone that was NoRegister,
; producing `%vreg:fp = COPY $noreg`; PeepholeOptimizer's ValueTracker then
; treated Register(0) as virtual (it is neither virtual nor physical) and
; dereferenced a use-def chain that does not exist -- crashing llc/rustc.
; Reduced from Firefox's wr_dp_define_scroll_layer (gfx/webrender_bindings).

target datalayout = "e-m:e-p:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "ia64"

; Two 4x-float HFAs shadow slots 0-3 and use up F8-F15. The 2x-float HFA then
; still has a slot (slot 4 = r36) but no FP register, so both its elements ride
; in r36: element 0 in the low half, element 1 in the high half -- the
; aggregate's plain little-endian image, matching gcc's DImode chunk.

; CHECK-LABEL: hfa_gr_low#:
; CHECK-NOT: shr.u
; CHECK: st4 [{{r[0-9]+}}] = r36
define float @hfa_gr_low([4 x float] %a, [4 x float] %b, [2 x float] %c) {
  %e = extractvalue [2 x float] %c, 0
  ret float %e
}

; CHECK-LABEL: hfa_gr_high#:
; CHECK: shr.u {{r[0-9]+}} = r36,
define float @hfa_gr_high([4 x float] %a, [4 x float] %b, [2 x float] %c) {
  %e = extractvalue [2 x float] %c, 1
  ret float %e
}

; Caller side: the pair is assembled into out4 (low | high << 32) -- the same
; parameter slot gcc fills with fpack/stf8/ld8.

; CHECK-LABEL: call_packed#:
; CHECK: shl {{r[0-9]+}} = {{r[0-9]+}},
; CHECK: or out4 = {{r[0-9]+}}, {{r[0-9]+}}
declare float @callee([4 x float], [4 x float], [2 x float])
define float @call_packed(float %p, float %q) {
  %v0 = insertvalue [2 x float] poison, float %p, 0
  %v1 = insertvalue [2 x float] %v0, float %q, 1
  %r = call float @callee([4 x float] zeroinitializer,
                          [4 x float] zeroinitializer, [2 x float] %v1)
  ret float %r
}

; The FP registers can also run out *mid-aggregate*: seven doubles take F8-F14
; and slots 0-6, so the 2x-float HFA in slot 7 (r39) gets F15 for element 0 and
; nothing for element 1.
;
; Element 1 goes in the LOW half of r39. The psABI wants an odd 4-byte hunk
; left-adjusted (the high half), but gcc has always emitted it right-adjusted
; -- ia64_function_arg_1 in gcc/config/ia64/ia64.cc:
;
;     /* If we have an odd 4 byte hunk because we ran out of FR regs, then this
;        goes in a GR reg left adjusted/little endian, right adjusted/big
;        endian.  */
;     /* ??? Currently this is handled wrong, because 4-byte hunks are always
;        right adjusted/little endian.  */
;     if (offset & 0x4)
;       gr_mode = SImode;
;
; Match gcc bug-for-bug: it is the ABI every ia64 object was built against.

; CHECK-LABEL: mixed_fpreg#:
; CHECK: mov f8 = f15
define float @mixed_fpreg(double %a, double %b, double %c, double %d, double %e,
                          double %f, double %g, [2 x float] %v) {
  %r = extractvalue [2 x float] %v, 0
  ret float %r
}

; CHECK-LABEL: mixed_gr_low#:
; CHECK-NOT: shr.u
; CHECK: st4 [{{r[0-9]+}}] = r39
define float @mixed_gr_low(double %a, double %b, double %c, double %d,
                          double %e, double %f, double %g, [2 x float] %v) {
  %r = extractvalue [2 x float] %v, 1
  ret float %r
}

; Once an f32 HFA has desynced the two pools, a plain FP argument after it can
; find a free slot but no FP register too. An f64 is then passed as its i64 bit
; pattern in the slot (gcc's DImode chunk), read back out with setf.d.

; CHECK-LABEL: plain_f64_after_hfa#:
; CHECK: setf.d {{f[0-9]+}} = r36
define double @plain_f64_after_hfa([4 x float] %a, [4 x float] %b, double %d) {
  ret double %d
}

; The other way for a pair to miss an FP register is to run out of *slots*:
; eight integers take slots 0-7, so the HFA is passed packed in one 8-byte
; stack slot (sp+16) with element 0 in its low 4 bytes and element 1 in its
; high ones. F8-F15 are all still free here, but an FP argument only rides in
; one while it has a register slot, so neither element may be routed there.

; CHECK-LABEL: stack_pair_low#:
; CHECK: add {{r[0-9]+}} = 16, r12
; CHECK: ldfs f8 = [{{r[0-9]+}}]
define float @stack_pair_low(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e, i64 %f,
                             i64 %g, i64 %h, [2 x float] %v) {
  %r = extractvalue [2 x float] %v, 0
  ret float %r
}

; CHECK-LABEL: stack_pair_high#:
; CHECK: add {{r[0-9]+}} = 20, r12
; CHECK: ldfs f8 = [{{r[0-9]+}}]
define float @stack_pair_high(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e, i64 %f,
                              i64 %g, i64 %h, [2 x float] %v) {
  %r = extractvalue [2 x float] %v, 1
  ret float %r
}

; CHECK-LABEL: call_stack_pair#:
; CHECK-DAG: adds [[LO:r[0-9]+]] = 16, r12
; CHECK-DAG: adds [[HI:r[0-9]+]] = 20, r12
; CHECK-DAG: stfs [[[LO]]] = f8
; CHECK-DAG: stfs [[[HI]]] = f9
declare float @stack_callee(i64, i64, i64, i64, i64, i64, i64, i64, [2 x float])
define float @call_stack_pair(float %p, float %q) {
  %v0 = insertvalue [2 x float] poison, float %p, 0
  %v1 = insertvalue [2 x float] %v0, float %q, 1
  %r = call float @stack_callee(i64 0, i64 0, i64 0, i64 0, i64 0, i64 0,
                                i64 0, i64 0, [2 x float] %v1)
  ret float %r
}
