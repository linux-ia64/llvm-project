; RUN: llc -mtriple=ia64 < %s | FileCheck %s

; Floating-point select for f80.

define x86_fp80 @sel_f80(i1 %c, x86_fp80 %a, x86_fp80 %b) {
; CHECK-LABEL:   sel_f80#
; CHECK:         cmp.ne p{{[0-9]+}}, p0 = r{{[0-9]+}}, r0
; CHECK-NEXT:    ;;
; CHECK-NEXT:    (p6) mov f{{[0-9]+}} = f{{[0-9]+}}
  %r = select i1 %c, x86_fp80 %a, x86_fp80 %b
  ret x86_fp80 %r
}
