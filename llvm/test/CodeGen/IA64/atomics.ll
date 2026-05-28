; RUN: llc -mtriple=ia64 < %s | FileCheck %s

; Atomic operations on IA-64.
;
; Acquire/release ordering is provided with a memory fence (mf) around plain
; loads/stores; compare-exchange uses ar.ccv + cmpxchg, and atomicrmw is
; expanded to a cmpxchg loop.
;
; Regression 91c64164 ("Fix narrow atomic load/store"): sub-64-bit atomic
; load/store were marked Custom but not implemented, so they broke. Only i64 is
; Custom; narrower widths are promoted and must still produce a valid sized
; load/store (ld1/ld4/...), not crash or emit garbage.

target datalayout = "e-m:e-p:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "ia64"

define i64 @load_i64_acquire(ptr %p) {
; CHECK-LABEL: load_i64_acquire#:
; CHECK: ld8 {{r[0-9]+}} = {{\[}}{{r[0-9]+}}]
; CHECK: mf
  %v = load atomic i64, ptr %p acquire, align 8
  ret i64 %v
}

define void @store_i64_release(ptr %p, i64 %v) {
; CHECK-LABEL: store_i64_release#:
; CHECK: mf
; CHECK: st8 {{\[}}{{r[0-9]+}}] = {{r[0-9]+}}
  store atomic i64 %v, ptr %p release, align 8
  ret void
}

; Narrow atomic load: promoted, must emit a real 32-bit load (regression).
define i32 @load_i32_acquire(ptr %p) {
; CHECK-LABEL: load_i32_acquire#:
; CHECK: ld4 {{r[0-9]+}} = {{\[}}{{r[0-9]+}}]
; CHECK: mf
  %v = load atomic i32, ptr %p acquire, align 4
  ret i32 %v
}

; Narrow atomic load: promoted, must emit a real 8-bit load (regression).
define i8 @load_i8_acquire(ptr %p) {
; CHECK-LABEL: load_i8_acquire#:
; CHECK: ld1 {{r[0-9]+}} = {{\[}}{{r[0-9]+}}]
; CHECK: mf
  %v = load atomic i8, ptr %p acquire, align 1
  ret i8 %v
}

; cmpxchg uses the compare value register ar.ccv and cmpxchg8.
define i64 @cmpxchg_i64(ptr %p, i64 %c, i64 %n) {
; CHECK-LABEL: cmpxchg_i64#:
; CHECK: mov ar.ccv = {{r[0-9]+}}
; CHECK: cmpxchg8.acq {{r[0-9]+}} = {{\[}}{{r[0-9]+}}], {{r[0-9]+}}, ar.ccv
  %r = cmpxchg ptr %p, i64 %c, i64 %n acq_rel acquire
  %v = extractvalue { i64, i1 } %r, 0
  ret i64 %v
}

; atomicrmw is expanded to a load + cmpxchg loop.
define i64 @rmw_add(ptr %p, i64 %v) {
; CHECK-LABEL: rmw_add#:
; CHECK: cmpxchg8.acq {{r[0-9]+}} = {{\[}}{{r[0-9]+}}], {{r[0-9]+}}, ar.ccv
  %r = atomicrmw add ptr %p, i64 %v acq_rel
  ret i64 %r
}

define void @seq_cst_fence() {
; CHECK-LABEL: seq_cst_fence#:
; CHECK: mf
  fence seq_cst
  ret void
}
