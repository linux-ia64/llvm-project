; RUN: llc -mtriple=ia64 < %s | FileCheck %s

; IA-64's output registers out0-out7 are symbolic: gas resolves 'out0' to the
; real stacked register r(32+inputs+locals) from the 'alloc', but the .td gives
; them the fixed DwarfRegNum 120-127. So a variable that lives in an output
; register at some PC -- here, parameter 'x' after it is moved into out0 to be
; passed to g() -- must be described in debug info by the actual stacked
; register, or gdb reads the wrong register (test_gdb.test_pretty_print saw a
; bogus '0x0'). IA64FixupDebugOutRegs rewrites out0-out7 in debug values to that
; register. Check that 'x' is reported in a stacked GPR, never in an out reg.

; CHECK-LABEL: f#:
; CHECK:       //DEBUG_VALUE: f:x <- $r32
; CHECK:       mov out0 = r32
; CHECK:       //DEBUG_VALUE: f:x <- $r{{[0-9]+}}
; CHECK-NOT:   //DEBUG_VALUE: {{.*}} <- $out

target datalayout = "e-m:e-p:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "ia64"

define dso_local i64 @f(i64 %x, i64 %y) local_unnamed_addr !dbg !11 {
entry:
    #dbg_value(i64 %x, !16, !DIExpression(), !18)
    #dbg_value(i64 %y, !17, !DIExpression(), !18)
  %call = tail call i64 @g(i64 %x), !dbg !19
  %add = add nsw i64 %call, %y, !dbg !20
  ret i64 %add, !dbg !21
}

declare !dbg !22 dso_local i64 @g(i64) local_unnamed_addr

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}

!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, producer: "clang", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "arg.c", directory: "/tmp")
!2 = !{i32 7, !"Dwarf Version", i32 5}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!11 = distinct !DISubprogram(name: "f", scope: !1, file: !1, line: 2, type: !12, scopeLine: 2, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !15)
!12 = !DISubroutineType(types: !13)
!13 = !{!14, !14, !14}
!14 = !DIBasicType(name: "long", size: 64, encoding: DW_ATE_signed)
!15 = !{!16, !17}
!16 = !DILocalVariable(name: "x", arg: 1, scope: !11, file: !1, line: 2, type: !14)
!17 = !DILocalVariable(name: "y", arg: 2, scope: !11, file: !1, line: 2, type: !14)
!18 = !DILocation(line: 0, scope: !11)
!19 = !DILocation(line: 2, column: 33, scope: !11)
!20 = !DILocation(line: 2, column: 38, scope: !11)
!21 = !DILocation(line: 2, column: 26, scope: !11)
!22 = !DISubprogram(name: "g", scope: !1, file: !1, line: 1, type: !23, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!23 = !DISubroutineType(types: !24)
!24 = !{!14, !14}
