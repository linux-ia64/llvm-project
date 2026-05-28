; RUN: llc -mtriple=ia64 < %s | FileCheck %s
; RUN: llc -mtriple=ia64 < %s | FileCheck %s --check-prefix=NODIRECT

; The return pointer (rp) is really branch register b0. It is modeled as a member
; of the GR class so that 'mov rN = rp' / 'mov rp = rN' assemble, but it can never
; be the operand of a plain st8/ld8: those require a *general* register, and gas
; rejects 'st8 [rX] = rp' / 'ld8 rp = [rX]' ("Operand N should be a general
; register").
;
; A non-leaf function preserves its own return pointer by parking the incoming rp
; once in a stacked local in the prologue (mov rN = rp) and restoring it in the
; epilogue (mov rp = rN). It must NOT save/restore rp around each individual call:
; LowerCall used to do that, and the per-call save value -- live across the call,
; which clobbers b0 -- coalesced into the physical rp and was then spilled by the
; register allocator as the illegal 'st8 [..] = rp' / 'ld8 rp = [..]'. This broke
; real builds (e.g. fish-shell) at the external-assembler step.

target datalayout = "e-m:e-p:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "ia64"

declare void @callee(i64)

; Two back-to-back calls so b0 is clobbered twice while the function's own return
; pointer must survive to the epilogue.
define void @twocalls(i64 %x) {
; CHECK-LABEL: twocalls#:
; The incoming rp is parked once in a stacked local in the prologue...
; CHECK: mov [[RPSAVE:r[0-9]+]] = rp
; ...both calls clobber b0, with no per-call rp save/restore around either...
; CHECK: br.call.sptk rp = callee#
; CHECK: br.call.sptk rp = callee#
; ...and the epilogue restores b0 from that one local.
; CHECK: mov rp = [[RPSAVE]]
;
; Crucially, rp is never stored to / reloaded from the stack directly: neither of
; these illegal forms may appear anywhere in the output.
; NODIRECT-NOT: st8{{.*}}= rp
; NODIRECT-NOT: ld8 rp =
entry:
  call void @callee(i64 %x)
  call void @callee(i64 %x)
  ret void
}
