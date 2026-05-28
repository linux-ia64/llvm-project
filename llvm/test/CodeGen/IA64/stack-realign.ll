; RUN: llc -mtriple=ia64 < %s | FileCheck %s

; The backend reports StackRealignable=false: sp (r12) is only 16-byte aligned
; and the prologue never emits an 'and sp, -N'. A local whose alignment exceeds
; 16 therefore cannot be honoured at a static sp+offset slot. Such an alloca is
; demoted to a dynamically-sized object lowered via DYNAMIC_STACKALLOC, whose
; Expand emits 'sp -= size; sp &= -align' -- so the pointer is genuinely
; aligned. (Before this, the over-aligned alloca was folded into the static
; frame and computeKnownBits rewrote field GEPs 'add base,k' into a colliding
; 'or base,k', corrupting fields once sp turned out to be merely 16-aligned.)

target datalayout = "e-m:e-p:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "ia64"

declare void @use(ptr)

; An alloca aligned to 64 must be dynamically realigned: sp is masked with -64.
; CHECK-LABEL: overaligned#:
; CHECK: adds [[NEG:r[0-9]+]] = -64, r0
; CHECK: and {{r[0-9]+}} = {{r[0-9]+}}, [[NEG]]
define void @overaligned() {
  %p = alloca [8 x i64], align 64
  call void @use(ptr %p)
  ret void
}

; A 16-aligned alloca already satisfies the stack alignment, so it stays a
; static frame slot: sp is adjusted by a plain add, never masked.
; CHECK-LABEL: aligned16#:
; CHECK-NOT: and {{r[0-9]+}} = {{r[0-9]+}}, {{r[0-9]+}}
define void @aligned16() {
  %p = alloca [8 x i64], align 16
  call void @use(ptr %p)
  ret void
}
