; RUN: llc -mtriple=ia64 < %s | FileCheck %s

; GNU 'as' for IA-64 only accepts the single-string `.file N "name"` form, not
; LLVM's default two-argument `.file N "dir" "name"` (it rejects the second
; string as "junk at end of line"). With EnableDwarfFileDirectoryDefault=false
; the MCAsmStreamer folds the directory into the filename, emitting one quoted
; string: `.file N "dir/name"`.

target datalayout = "e-m:e-p:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "ia64"

; The numbered .file directive must carry a single folded "dir/name" string and
; never a separate directory operand.
; CHECK: .file 1 "/home/user/src/test.c"
; CHECK-NOT: .file 1 {{.*}}" "

define i64 @f(i64 %x) !dbg !4 {
  ret i64 %x, !dbg !7
}

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3}
!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, producer: "clang", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug)
!1 = !DIFile(filename: "test.c", directory: "/home/user/src")
!3 = !{i32 2, !"Debug Info Version", i32 3}
!4 = distinct !DISubprogram(name: "f", scope: !1, file: !1, line: 1, type: !5, unit: !0)
!5 = !DISubroutineType(types: !6)
!6 = !{null}
!7 = !DILocation(line: 2, column: 1, scope: !4)
