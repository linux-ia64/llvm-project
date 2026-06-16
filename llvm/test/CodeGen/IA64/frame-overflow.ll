; RUN: llc -mtriple=ia64 < %s | FileCheck %s

; Regression test for stacked-GPR frame overflow under high register
; pressure. The 'alloc' frame is (locals + outputs) and must be <= 96
; stacked GPRs. A non-leaf function parks both the caller's ar.pfs and the
; return pointer (rp) in one extra local each, and gas places the
; outgoing-argument registers out0-out7 immediately above the locals -- so
; locals + 1 (ar.pfs) + 1 (rp) + 8 (outputs) must fit in 96, i.e. the
; allocator may use at most 86 stacked locals.
;
; getReservedRegs caps this by reserving the top 10 stacked GPRs (r118-r127):
; the 8 outputs plus the rp save (r119) plus the ar.pfs save (r118). The worst
; case 86 + 1 + 1 + 8 = 96 then exactly fits; one more local would push out7
; onto the nonexistent r128, which GNU as rejects with "Size of frame exceeds
; maximum of 96 registers".
;
; The ~120 volatile loads below are all live across the 8-argument call, so
; they must occupy callee-preserved stacked locals (scratch GRs do not
; survive a call), saturating the local frame to its cap.

target datalayout = "e-m:e-p:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "ia64"

@g = external global [120 x i64]

declare i64 @sink8(i64, i64, i64, i64, i64, i64, i64, i64)

; CHECK-LABEL: pressure#:
; The alloc frame must be locals=88, outputs=8: 86 allocator locals + the
; ar.pfs save (r118) + the rp save (r119). 'alloc' is preceded by its
; '.save ar.pfs' directive and followed by '.save rp', so the checks are in
; that order.
; CHECK: .save{{.*}}ar.pfs, r118
; CHECK: alloc r{{[0-9]+}} = ar.pfs,0,88,8,0
; CHECK: .save{{.*}}rp, r119

define i64 @pressure() {
entry:
  %p0 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 0)
  %p1 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 1)
  %p2 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 2)
  %p3 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 3)
  %p4 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 4)
  %p5 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 5)
  %p6 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 6)
  %p7 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 7)
  %p8 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 8)
  %p9 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 9)
  %p10 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 10)
  %p11 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 11)
  %p12 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 12)
  %p13 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 13)
  %p14 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 14)
  %p15 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 15)
  %p16 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 16)
  %p17 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 17)
  %p18 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 18)
  %p19 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 19)
  %p20 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 20)
  %p21 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 21)
  %p22 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 22)
  %p23 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 23)
  %p24 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 24)
  %p25 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 25)
  %p26 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 26)
  %p27 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 27)
  %p28 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 28)
  %p29 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 29)
  %p30 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 30)
  %p31 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 31)
  %p32 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 32)
  %p33 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 33)
  %p34 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 34)
  %p35 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 35)
  %p36 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 36)
  %p37 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 37)
  %p38 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 38)
  %p39 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 39)
  %p40 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 40)
  %p41 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 41)
  %p42 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 42)
  %p43 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 43)
  %p44 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 44)
  %p45 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 45)
  %p46 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 46)
  %p47 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 47)
  %p48 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 48)
  %p49 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 49)
  %p50 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 50)
  %p51 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 51)
  %p52 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 52)
  %p53 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 53)
  %p54 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 54)
  %p55 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 55)
  %p56 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 56)
  %p57 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 57)
  %p58 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 58)
  %p59 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 59)
  %p60 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 60)
  %p61 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 61)
  %p62 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 62)
  %p63 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 63)
  %p64 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 64)
  %p65 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 65)
  %p66 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 66)
  %p67 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 67)
  %p68 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 68)
  %p69 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 69)
  %p70 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 70)
  %p71 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 71)
  %p72 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 72)
  %p73 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 73)
  %p74 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 74)
  %p75 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 75)
  %p76 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 76)
  %p77 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 77)
  %p78 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 78)
  %p79 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 79)
  %p80 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 80)
  %p81 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 81)
  %p82 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 82)
  %p83 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 83)
  %p84 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 84)
  %p85 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 85)
  %p86 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 86)
  %p87 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 87)
  %p88 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 88)
  %p89 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 89)
  %p90 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 90)
  %p91 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 91)
  %p92 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 92)
  %p93 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 93)
  %p94 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 94)
  %p95 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 95)
  %p96 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 96)
  %p97 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 97)
  %p98 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 98)
  %p99 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 99)
  %p100 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 100)
  %p101 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 101)
  %p102 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 102)
  %p103 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 103)
  %p104 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 104)
  %p105 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 105)
  %p106 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 106)
  %p107 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 107)
  %p108 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 108)
  %p109 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 109)
  %p110 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 110)
  %p111 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 111)
  %p112 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 112)
  %p113 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 113)
  %p114 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 114)
  %p115 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 115)
  %p116 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 116)
  %p117 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 117)
  %p118 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 118)
  %p119 = load volatile i64, ptr getelementptr inbounds ([120 x i64], ptr @g, i64 0, i64 119)
  %c = call i64 @sink8(i64 1, i64 2, i64 3, i64 4, i64 5, i64 6, i64 7, i64 8)
  %a0 = add i64 %c, %p0
  %a1 = add i64 %a0, %p1
  %a2 = add i64 %a1, %p2
  %a3 = add i64 %a2, %p3
  %a4 = add i64 %a3, %p4
  %a5 = add i64 %a4, %p5
  %a6 = add i64 %a5, %p6
  %a7 = add i64 %a6, %p7
  %a8 = add i64 %a7, %p8
  %a9 = add i64 %a8, %p9
  %a10 = add i64 %a9, %p10
  %a11 = add i64 %a10, %p11
  %a12 = add i64 %a11, %p12
  %a13 = add i64 %a12, %p13
  %a14 = add i64 %a13, %p14
  %a15 = add i64 %a14, %p15
  %a16 = add i64 %a15, %p16
  %a17 = add i64 %a16, %p17
  %a18 = add i64 %a17, %p18
  %a19 = add i64 %a18, %p19
  %a20 = add i64 %a19, %p20
  %a21 = add i64 %a20, %p21
  %a22 = add i64 %a21, %p22
  %a23 = add i64 %a22, %p23
  %a24 = add i64 %a23, %p24
  %a25 = add i64 %a24, %p25
  %a26 = add i64 %a25, %p26
  %a27 = add i64 %a26, %p27
  %a28 = add i64 %a27, %p28
  %a29 = add i64 %a28, %p29
  %a30 = add i64 %a29, %p30
  %a31 = add i64 %a30, %p31
  %a32 = add i64 %a31, %p32
  %a33 = add i64 %a32, %p33
  %a34 = add i64 %a33, %p34
  %a35 = add i64 %a34, %p35
  %a36 = add i64 %a35, %p36
  %a37 = add i64 %a36, %p37
  %a38 = add i64 %a37, %p38
  %a39 = add i64 %a38, %p39
  %a40 = add i64 %a39, %p40
  %a41 = add i64 %a40, %p41
  %a42 = add i64 %a41, %p42
  %a43 = add i64 %a42, %p43
  %a44 = add i64 %a43, %p44
  %a45 = add i64 %a44, %p45
  %a46 = add i64 %a45, %p46
  %a47 = add i64 %a46, %p47
  %a48 = add i64 %a47, %p48
  %a49 = add i64 %a48, %p49
  %a50 = add i64 %a49, %p50
  %a51 = add i64 %a50, %p51
  %a52 = add i64 %a51, %p52
  %a53 = add i64 %a52, %p53
  %a54 = add i64 %a53, %p54
  %a55 = add i64 %a54, %p55
  %a56 = add i64 %a55, %p56
  %a57 = add i64 %a56, %p57
  %a58 = add i64 %a57, %p58
  %a59 = add i64 %a58, %p59
  %a60 = add i64 %a59, %p60
  %a61 = add i64 %a60, %p61
  %a62 = add i64 %a61, %p62
  %a63 = add i64 %a62, %p63
  %a64 = add i64 %a63, %p64
  %a65 = add i64 %a64, %p65
  %a66 = add i64 %a65, %p66
  %a67 = add i64 %a66, %p67
  %a68 = add i64 %a67, %p68
  %a69 = add i64 %a68, %p69
  %a70 = add i64 %a69, %p70
  %a71 = add i64 %a70, %p71
  %a72 = add i64 %a71, %p72
  %a73 = add i64 %a72, %p73
  %a74 = add i64 %a73, %p74
  %a75 = add i64 %a74, %p75
  %a76 = add i64 %a75, %p76
  %a77 = add i64 %a76, %p77
  %a78 = add i64 %a77, %p78
  %a79 = add i64 %a78, %p79
  %a80 = add i64 %a79, %p80
  %a81 = add i64 %a80, %p81
  %a82 = add i64 %a81, %p82
  %a83 = add i64 %a82, %p83
  %a84 = add i64 %a83, %p84
  %a85 = add i64 %a84, %p85
  %a86 = add i64 %a85, %p86
  %a87 = add i64 %a86, %p87
  %a88 = add i64 %a87, %p88
  %a89 = add i64 %a88, %p89
  %a90 = add i64 %a89, %p90
  %a91 = add i64 %a90, %p91
  %a92 = add i64 %a91, %p92
  %a93 = add i64 %a92, %p93
  %a94 = add i64 %a93, %p94
  %a95 = add i64 %a94, %p95
  %a96 = add i64 %a95, %p96
  %a97 = add i64 %a96, %p97
  %a98 = add i64 %a97, %p98
  %a99 = add i64 %a98, %p99
  %a100 = add i64 %a99, %p100
  %a101 = add i64 %a100, %p101
  %a102 = add i64 %a101, %p102
  %a103 = add i64 %a102, %p103
  %a104 = add i64 %a103, %p104
  %a105 = add i64 %a104, %p105
  %a106 = add i64 %a105, %p106
  %a107 = add i64 %a106, %p107
  %a108 = add i64 %a107, %p108
  %a109 = add i64 %a108, %p109
  %a110 = add i64 %a109, %p110
  %a111 = add i64 %a110, %p111
  %a112 = add i64 %a111, %p112
  %a113 = add i64 %a112, %p113
  %a114 = add i64 %a113, %p114
  %a115 = add i64 %a114, %p115
  %a116 = add i64 %a115, %p116
  %a117 = add i64 %a116, %p117
  %a118 = add i64 %a117, %p118
  %a119 = add i64 %a118, %p119
  ret i64 %a119
}
