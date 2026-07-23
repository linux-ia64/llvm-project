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

; cmpxchg{1,2,4} read the memory word zero-extended to 64 bits and compare it
; against the *full 64 bits* of ar.ccv, so the comparand must be zero-extended
; into ar.ccv. Here it arrives as an argument, so nothing guarantees its upper
; bits are clear.
;
; Getting this wrong is worse than a missed swap: the success flag is computed
; by comparing the returned old value (always zero-extended by the hardware)
; against the comparand, so an un-extended ar.ccv makes the hardware compare and
; the success flag disagree -- a cmpxchg the hardware rejected, and therefore
; did not store, gets reported as having succeeded. The zxt must feed *both*
; ar.ccv and the compare, hence the same register in all three places.
define i1 @cmpxchg_i32_zext_ccv(ptr %p, i32 %c, i32 %n) {
; CHECK-LABEL: cmpxchg_i32_zext_ccv#:
; CHECK: zxt4 [[C:r[0-9]+]] = {{r[0-9]+}}
; CHECK: mov ar.ccv = [[C]]
; CHECK: cmpxchg4.acq [[OLD:r[0-9]+]] = {{\[}}{{r[0-9]+}}], {{r[0-9]+}}, ar.ccv
; CHECK: cmp.eq {{p[0-9]+}}, {{p[0-9]+}} = [[OLD]], [[C]]
  %r = cmpxchg ptr %p, i32 %c, i32 %n monotonic monotonic
  %ok = extractvalue { i32, i1 } %r, 1
  ret i1 %ok
}

define i1 @cmpxchg_i16_zext_ccv(ptr %p, i16 %c, i16 %n) {
; CHECK-LABEL: cmpxchg_i16_zext_ccv#:
; CHECK: zxt2 [[C:r[0-9]+]] = {{r[0-9]+}}
; CHECK: mov ar.ccv = [[C]]
; CHECK: cmpxchg2.acq [[OLD:r[0-9]+]] = {{\[}}{{r[0-9]+}}], {{r[0-9]+}}, ar.ccv
; CHECK: cmp.eq {{p[0-9]+}}, {{p[0-9]+}} = [[OLD]], [[C]]
  %r = cmpxchg ptr %p, i16 %c, i16 %n monotonic monotonic
  %ok = extractvalue { i16, i1 } %r, 1
  ret i1 %ok
}

define i1 @cmpxchg_i8_zext_ccv(ptr %p, i8 %c, i8 %n) {
; CHECK-LABEL: cmpxchg_i8_zext_ccv#:
; CHECK: zxt1 [[C:r[0-9]+]] = {{r[0-9]+}}
; CHECK: mov ar.ccv = [[C]]
; CHECK: cmpxchg1.acq [[OLD:r[0-9]+]] = {{\[}}{{r[0-9]+}}], {{r[0-9]+}}, ar.ccv
; CHECK: cmp.eq {{p[0-9]+}}, {{p[0-9]+}} = [[OLD]], [[C]]
  %r = cmpxchg ptr %p, i8 %c, i8 %n monotonic monotonic
  %ok = extractvalue { i8, i1 } %r, 1
  ret i1 %ok
}

; i64 is already full width: no extension, ar.ccv takes the comparand directly.
define i1 @cmpxchg_i64_no_zext_ccv(ptr %p, i64 %c, i64 %n) {
; CHECK-LABEL: cmpxchg_i64_no_zext_ccv#:
; CHECK-NOT: zxt
; CHECK: mov ar.ccv = {{r[0-9]+}}
; CHECK: cmpxchg8.acq {{r[0-9]+}} = {{\[}}{{r[0-9]+}}], {{r[0-9]+}}, ar.ccv
  %r = cmpxchg ptr %p, i64 %c, i64 %n monotonic monotonic
  %ok = extractvalue { i64, i1 } %r, 1
  ret i1 %ok
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
