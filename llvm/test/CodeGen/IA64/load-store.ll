; RUN: llc -mtriple=ia64 < %s | FileCheck %s

; Integer loads and stores. ld1/ld2/ld4/ld8 zero-extend the loaded value into
; the 64-bit GR; a signed sub-word load is sign-extended afterwards with sxt.
;
; Regression b9d6b47e ("Sign-extend before load if value is signed"): a signed
; narrow load must produce the sxt so the high bits are correct; a zero-extended
; load needs no sxt because the ld already zero-fills.

target datalayout = "e-m:e-p:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "ia64"

define i64 @sextload_i8(ptr %p) {
; CHECK-LABEL: sextload_i8#:
; CHECK: ld1 [[R:r[0-9]+]] = {{\[}}{{r[0-9]+}}]
; CHECK: sxt1 {{r[0-9]+}} = [[R]]
  %v = load i8, ptr %p
  %e = sext i8 %v to i64
  ret i64 %e
}

define i64 @zextload_i8(ptr %p) {
; CHECK-LABEL: zextload_i8#:
; CHECK: ld1 {{r[0-9]+}} = {{\[}}{{r[0-9]+}}]
; CHECK-NOT: sxt
  %v = load i8, ptr %p
  %e = zext i8 %v to i64
  ret i64 %e
}

define i64 @sextload_i16(ptr %p) {
; CHECK-LABEL: sextload_i16#:
; CHECK: ld2 [[R:r[0-9]+]] = {{\[}}{{r[0-9]+}}]
; CHECK: sxt2 {{r[0-9]+}} = [[R]]
  %v = load i16, ptr %p
  %e = sext i16 %v to i64
  ret i64 %e
}

define i64 @sextload_i32(ptr %p) {
; CHECK-LABEL: sextload_i32#:
; CHECK: ld4 [[R:r[0-9]+]] = {{\[}}{{r[0-9]+}}]
; CHECK: sxt4 {{r[0-9]+}} = [[R]]
  %v = load i32, ptr %p
  %e = sext i32 %v to i64
  ret i64 %e
}

define i64 @load_i64(ptr %p) {
; CHECK-LABEL: load_i64#:
; CHECK: ld8 {{r[0-9]+}} = {{\[}}{{r[0-9]+}}]
  %v = load i64, ptr %p
  ret i64 %v
}

define void @store_i8(ptr %p, i8 %v) {
; CHECK-LABEL: store_i8#:
; CHECK: st1 {{\[}}{{r[0-9]+}}] = {{r[0-9]+}}
  store i8 %v, ptr %p
  ret void
}

define void @store_i32(ptr %p, i32 %v) {
; CHECK-LABEL: store_i32#:
; CHECK: st4 {{\[}}{{r[0-9]+}}] = {{r[0-9]+}}
  store i32 %v, ptr %p
  ret void
}

define void @store_i64(ptr %p, i64 %v) {
; CHECK-LABEL: store_i64#:
; CHECK: st8 {{\[}}{{r[0-9]+}}] = {{r[0-9]+}}
  store i64 %v, ptr %p
  ret void
}
