; RUN: llc -mtriple=ia64 < %s | FileCheck %s

; Lowering of ISD::RETURNADDR.

define ptr @retaddr() {
; CHECK-LABEL: retaddr#
; CHECK: mov r8 = rp
  %r = call ptr @llvm.returnaddress(i32 0)
  ret ptr %r
}

declare void @use(ptr)

; The incoming rp must be captured before any call in this function
; clobbers it via br.call.
define ptr @retaddr_with_call() {
; CHECK-LABEL: retaddr_with_call#
; CHECK: mov r{{[0-9]+}} = rp
; CHECK: br.call.sptk rp = use#
; CHECK: mov r8 = r{{[0-9]+}}
  call void @use(ptr null)
  %r = call ptr @llvm.returnaddress(i32 0)
  ret ptr %r
}
