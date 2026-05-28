; RUN: llc -mtriple=ia64 < %s | FileCheck %s

; A function pointer stored in data is the address of the function's descriptor
; { entry, gp }, so it is emitted as @fptr(f). A GlobalAlias of a function,
; however, is just another name for the aliasee's entry-point symbol: it must
; resolve to the bare entry point (`A = B`), not repeat the @fptr wrapping --
; both because the alias should equal the entry point and because GNU as rejects
; an @fptr pseudo-fixup in a symbol-assignment expression.

target datalayout = "e-m:e-p:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "ia64"

define void @target() {
  ret void
}

@falias = alias void (), ptr @target
@fnptr = global ptr @target

; A function pointer in data is wrapped in @fptr (descriptor address)...
; CHECK-LABEL: fnptr#:
; CHECK-NEXT:  data8.ua @fptr(target#)

; ...but the alias is a plain symbol assignment to the entry point, no @fptr.
; CHECK:       .globl falias#
; CHECK:       .type falias#,@function
; CHECK:       falias# = target#
; CHECK-NOT:   @fptr
