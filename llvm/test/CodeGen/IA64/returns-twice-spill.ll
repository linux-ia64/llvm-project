; RUN: llc -mtriple=ia64 < %s | FileCheck %s

; A call to a returns_twice function may modify the Register Stack Engine
; backing store: vfork() in particular runs the child in the parent's address
; space (CLONE_VM) on the *shared* backing store, so the child's use of stacked
; registers overwrites the parent's. Values live across such a call must
; therefore not be kept in stacked registers (r32-r127); the backend models this
; by clobbering all stacked registers with a regmask on the call, forcing such
; values out to the static callee-saved registers or to the frame.
;
; (Companion to setjmp-doublereturn.ll, which covers the gp/sp/rp parking.)

target datalayout = "e-m:e-p:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "ia64"

declare i32 @setjmp(ptr) returns_twice
declare void @use(i64, i32)

define i32 @liveacross(ptr %env, i64 %keep) {
; CHECK-LABEL: liveacross#:
;
; The frame is sized to the registers actually allocated (here a handful), not
; ballooned to the full 96-register stack: the stacked-register regmask on the
; call must not be counted as register usage by the alloc-sizing scan.
; CHECK: alloc {{r[0-9]+}} = ar.pfs,0,4,2,0
;
; %keep arrives in a stacked register (r33) and is live across the call. It is
; spilled to the frame before the call...
; CHECK: st8 [{{r[0-9]+}}] = r33
; CHECK: br.call.sptk rp = setjmp#
; ...and reloaded afterwards to be passed on, rather than read back out of the
; (now-unreliable) stacked register.
; CHECK: ld8 out0 = [{{r[0-9]+}}]
; CHECK: br.call.sptk rp = use#
entry:
  %r = call i32 @setjmp(ptr %env)
  call void @use(i64 %keep, i32 %r)
  ret i32 %r
}
