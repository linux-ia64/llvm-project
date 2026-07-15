; RUN: llc -mtriple=ia64 < %s | FileCheck %s

; A homogeneous floating-point aggregate (HFA) of N single-precision floats
; shadows ceil(N*32/64) = ceil(N/2) general-register parameter slots, not N:
; the psABI packs two f32 elements per 64-bit slot, one value per FP register
; (Note under Figure 8-5, IA64conventions.pdf p.8-9; Table 8-1's "Aggregates"
; row gives the slot count as (size+63)/64 for the aggregate as a whole,
; IA64conventions.pdf p.8-6). A 4x-float HFA is 128 bits, so it shadows 2 GR
; slots, not 4 - each element still lands in its own F8-F15 register.
;
; Regression test: the backend previously called AllocateReg on the shadow
; slot once per flattened f32 element (CC_IA64_FP_Common), burning 4 slots
; for a 4x-float HFA instead of 2 and shifting every later argument's slot by
; 2 relative to a GCC-compiled caller/callee.

target datalayout = "e-m:e-p:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "ia64"

declare void @sink(i64, [4 x float], i64, i64)

; slot 0: %a (i64)   -> out0
; slots 1-2: the HFA -> f8-f11, shadowing 2 GR slots
; slot 3: c (i64)    -> out3 (out5 with the shadow-slot-per-element bug)
; slot 4: d (i64)    -> out4 (out6 with the bug)
define void @call_after_hfa() {
; CHECK-LABEL: call_after_hfa#:
; CHECK-DAG: out0 = 1,
; CHECK-DAG: out3 = 3,
; CHECK-DAG: out4 = 4,
; CHECK-NOT: out5 = 3,
; CHECK-NOT: out6 = 4,
  call void @sink(i64 1, [4 x float] [float 1.0, float 2.0, float 3.0, float 4.0], i64 3, i64 4)
  ret void
}

declare void @sink2(i64, i64)

; Incoming c/d must arrive in r35/r36 (slots 3/4, shadowing the 2 slots the
; HFA actually occupies); the buggy backend put them in r37/r38 (slots 5/6).
define void @use_after_hfa(i64 %a, [4 x float] %hfa, i64 %c, i64 %d) {
; CHECK-LABEL: use_after_hfa#:
; CHECK-DAG: mov out0 = r35
; CHECK-DAG: mov out1 = r36
  call void @sink2(i64 %c, i64 %d)
  ret void
}

; A 3-element single-precision HFA (96 bits) shadows ceil(96/64) = 2 GR slots:
; the pair (elements 0-1) shares one slot, and the odd trailing element (2)
; gets a slot of its own with the other half undefined (Note under Figure
; 8-5). Exercises the "no partner" branch of the pairing logic
; (isInConsecutiveRegsLast() without a preceding pending half).
declare void @sink3(i64, [3 x float], i64)

; slot 0: %a -> out0; slots 1-2: the HFA; slot 3: c -> out3 (out4 with the bug)
define void @call_after_odd_hfa() {
; CHECK-LABEL: call_after_odd_hfa#:
; CHECK-DAG: out0 = 1,
; CHECK-DAG: out3 = 9,
; CHECK-NOT: out4 = 9,
  call void @sink3(i64 1, [3 x float] [float 1.0, float 2.0, float 3.0], i64 9)
  ret void
}

; Pure double and long-double HFAs are unaffected by the f32-pairing fix:
; functionArgumentNeedsConsecutiveRegisters only fires for a coerced
; [N x float] argument (see clang/test/CodeGen/ia64-hfa-classification.c for
; why a double/long-double HFA never collapses two elements into one slot),
; so each element keeps consuming its own full slot exactly as before.

; A double already occupies one whole 64-bit slot (Table 8-1), so a 3-element
; double HFA shadows exactly 3 GR slots, one per element -- no pairing.
declare void @sinkd(i64, [3 x double], i64)

; slot 0: %a -> out0; slots 1-3: the HFA (one slot per double); slot 4: c -> out4
define void @call_after_double_hfa() {
; CHECK-LABEL: call_after_double_hfa#:
; CHECK-DAG: out0 = 1,
; CHECK-DAG: out4 = 9,
  call void @sinkd(i64 1, [3 x double] [double 1.0, double 2.0, double 3.0], i64 9)
  ret void
}

; A long double (x86_fp80) is 128 bits and uses "Next Even" (Table 8-1): two
; slots per element, starting on an even slot. A 2-element long-double HFA is
; 256 bits, so it shadows ceil(256/64) = 4 slots -- which the existing
; per-element f80 Next-Even logic (CC_IA64_F80_Common) already produces
; without any pairing help, because each element is already exactly
; slot-sized.
declare void @sinkld(i64, [2 x x86_fp80], i64)

; slot 0: %a -> out0; slot 1: Next-Even padding; slots 2-5: the HFA (2 slots
; per element); slot 6: c -> out6
define void @call_after_long_double_hfa() {
; CHECK-LABEL: call_after_long_double_hfa#:
; CHECK-DAG: out0 = 1,
; CHECK-DAG: out6 = 9,
  call void @sinkld(i64 1,
                    [2 x x86_fp80] [x86_fp80 0xK3FFF8000000000000000,
                                    x86_fp80 0xK40008000000000000000],
                    i64 9)
  ret void
}

; A struct mixing float and double fields is not an HFA at all (the psABI
; and clang's isHomogeneousAggregate both require every leaf to share one
; base type), so clang coerces it to a plain [N x i64] -- the generic
; aggregate path, entirely untouched by CC_IA64_FP_Common's float pairing.
; Each i64 element keeps consuming one full slot, as it always did.
declare void @sinkm(i64, [2 x i64], i64)

; slot 0: %a -> out0; slots 1-2: the coerced struct; slot 3: c -> out3
define void @call_after_mixed_nonhfa() {
; CHECK-LABEL: call_after_mixed_nonhfa#:
; CHECK-DAG: out0 = 1,
; CHECK-DAG: out3 = 9,
  call void @sinkm(i64 1, [2 x i64] [i64 4607182418800017408, i64 2], i64 9)
  ret void
}
