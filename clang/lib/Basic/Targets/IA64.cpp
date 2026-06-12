//===--- IA64.cpp - Implement IA-64 (Itanium) target feature support ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the IA-64 (Itanium) TargetInfo object.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "Targets.h"
#include "clang/Basic/MacroBuilder.h"
#include "llvm/ADT/StringSwitch.h"

using namespace clang;
using namespace clang::targets;

IA64TargetInfo::IA64TargetInfo(const llvm::Triple &Triple,
                               const TargetOptions &)
    : TargetInfo(Triple) {
  // Byte-identical to TargetDataLayout.cpp's Triple::ia64 case. Little-endian,
  // LP64, 80-bit long double in 128 bits, 128-bit stack alignment.
  resetDataLayout("e-m:e-p:64:64-i64:64-f80:128-n8:16:32:64-S128");

  // IA-64 is an LP64 platform.
  LongWidth = LongAlign = PointerWidth = PointerAlign = 64;
  IntMaxType = SignedLong;
  Int64Type = SignedLong;
  SizeType = UnsignedLong;
  PtrDiffType = SignedLong;
  IntPtrType = SignedLong;

  // long double is the 80-bit x87 extended format, but -- unlike x86 -- it is
  // stored and aligned in 16 bytes on IA-64 (psABI). This must match the
  // datalayout's "f80:128" and the cross glibc headers; do NOT inherit x86's
  // 96-bit/32-byte settings.
  LongDoubleWidth = 128;
  LongDoubleAlign = 128;
  LongDoubleFormat = &llvm::APFloat::x87DoubleExtended();
  SuitableAlign = 128;

  // Up to 64-bit accesses are lock-free.
  MaxAtomicPromoteWidth = MaxAtomicInlineWidth = 64;
}

IA64TargetInfo::CPUKind IA64TargetInfo::getCPUKind(StringRef Name) const {
  return llvm::StringSwitch<CPUKind>(Name)
      .Cases({"itanium", "merced"}, CK_ITANIUM)
      .Cases({"itanium2", "mckinley", "montecito"}, CK_ITANIUM2)
      .Default(CK_GENERIC);
}

void IA64TargetInfo::fillValidCPUList(
    SmallVectorImpl<StringRef> &Values) const {
  static const StringRef CPUs[] = {"generic", "itanium", "itanium2"};
  Values.append(std::begin(CPUs), std::end(CPUs));
}

void IA64TargetInfo::getTargetDefines(const LangOptions &Opts,
                                      MacroBuilder &Builder) const {
  // DefineStd emits __ia64__, __ia64, and (in GNU mode) ia64.
  DefineStd(Builder, "ia64", Opts);
  Builder.defineMacro("__itanium__");
  Builder.defineMacro("__REGISTER_PREFIX__", "");
  // __LP64__ / _LP64 are emitted generically for LP64 targets by the
  // preprocessor; long-double width follows from LongDoubleFormat above.

  // GCC exposes __float80 as a builtin type on i386/x86_64/IA-64, where it
  // "means the same thing as long double" -- on IA-64 long double is already
  // the 80-bit double-extended format, so the two are bit- and ABI-identical
  // (both mangle as 'e'). clang has no __float80 builtin type, so alias it to
  // long double; this is what GCC code such as libffi's ia64 port expects.
  Builder.defineMacro("__float80", "long double");
}

bool IA64TargetInfo::hasFeature(StringRef Feature) const {
  return Feature == "ia64";
}

// IA-64 register file: 128 general (r0-r127), 128 floating (f0-f127),
// 64 predicate (p0-p63), and 8 branch (b0-b7) registers.
const char *const IA64TargetInfo::GCCRegNames[] = {
    // clang-format off
    // General registers
    "r0",   "r1",   "r2",   "r3",   "r4",   "r5",   "r6",   "r7",
    "r8",   "r9",   "r10",  "r11",  "r12",  "r13",  "r14",  "r15",
    "r16",  "r17",  "r18",  "r19",  "r20",  "r21",  "r22",  "r23",
    "r24",  "r25",  "r26",  "r27",  "r28",  "r29",  "r30",  "r31",
    "r32",  "r33",  "r34",  "r35",  "r36",  "r37",  "r38",  "r39",
    "r40",  "r41",  "r42",  "r43",  "r44",  "r45",  "r46",  "r47",
    "r48",  "r49",  "r50",  "r51",  "r52",  "r53",  "r54",  "r55",
    "r56",  "r57",  "r58",  "r59",  "r60",  "r61",  "r62",  "r63",
    "r64",  "r65",  "r66",  "r67",  "r68",  "r69",  "r70",  "r71",
    "r72",  "r73",  "r74",  "r75",  "r76",  "r77",  "r78",  "r79",
    "r80",  "r81",  "r82",  "r83",  "r84",  "r85",  "r86",  "r87",
    "r88",  "r89",  "r90",  "r91",  "r92",  "r93",  "r94",  "r95",
    "r96",  "r97",  "r98",  "r99",  "r100", "r101", "r102", "r103",
    "r104", "r105", "r106", "r107", "r108", "r109", "r110", "r111",
    "r112", "r113", "r114", "r115", "r116", "r117", "r118", "r119",
    "r120", "r121", "r122", "r123", "r124", "r125", "r126", "r127",
    // Floating-point registers
    "f0",   "f1",   "f2",   "f3",   "f4",   "f5",   "f6",   "f7",
    "f8",   "f9",   "f10",  "f11",  "f12",  "f13",  "f14",  "f15",
    "f16",  "f17",  "f18",  "f19",  "f20",  "f21",  "f22",  "f23",
    "f24",  "f25",  "f26",  "f27",  "f28",  "f29",  "f30",  "f31",
    "f32",  "f33",  "f34",  "f35",  "f36",  "f37",  "f38",  "f39",
    "f40",  "f41",  "f42",  "f43",  "f44",  "f45",  "f46",  "f47",
    "f48",  "f49",  "f50",  "f51",  "f52",  "f53",  "f54",  "f55",
    "f56",  "f57",  "f58",  "f59",  "f60",  "f61",  "f62",  "f63",
    "f64",  "f65",  "f66",  "f67",  "f68",  "f69",  "f70",  "f71",
    "f72",  "f73",  "f74",  "f75",  "f76",  "f77",  "f78",  "f79",
    "f80",  "f81",  "f82",  "f83",  "f84",  "f85",  "f86",  "f87",
    "f88",  "f89",  "f90",  "f91",  "f92",  "f93",  "f94",  "f95",
    "f96",  "f97",  "f98",  "f99",  "f100", "f101", "f102", "f103",
    "f104", "f105", "f106", "f107", "f108", "f109", "f110", "f111",
    "f112", "f113", "f114", "f115", "f116", "f117", "f118", "f119",
    "f120", "f121", "f122", "f123", "f124", "f125", "f126", "f127",
    // Predicate registers
    "p0",   "p1",   "p2",   "p3",   "p4",   "p5",   "p6",   "p7",
    "p8",   "p9",   "p10",  "p11",  "p12",  "p13",  "p14",  "p15",
    "p16",  "p17",  "p18",  "p19",  "p20",  "p21",  "p22",  "p23",
    "p24",  "p25",  "p26",  "p27",  "p28",  "p29",  "p30",  "p31",
    "p32",  "p33",  "p34",  "p35",  "p36",  "p37",  "p38",  "p39",
    "p40",  "p41",  "p42",  "p43",  "p44",  "p45",  "p46",  "p47",
    "p48",  "p49",  "p50",  "p51",  "p52",  "p53",  "p54",  "p55",
    "p56",  "p57",  "p58",  "p59",  "p60",  "p61",  "p62",  "p63",
    // Branch registers
    "b0",   "b1",   "b2",   "b3",   "b4",   "b5",   "b6",   "b7",
    // clang-format on
};

ArrayRef<const char *> IA64TargetInfo::getGCCRegNames() const {
  return llvm::ArrayRef(GCCRegNames);
}

// The ABI register aliases: gp=r1, sp=r12, rp=b0 (the assembler also accepts
// these names, and gcc exposes them as register aliases).
const TargetInfo::GCCRegAlias IA64TargetInfo::GCCRegAliases[] = {
    {{"gp"}, "r1"},
    {{"sp"}, "r12"},
    {{"rp"}, "b0"},
};

ArrayRef<TargetInfo::GCCRegAlias> IA64TargetInfo::getGCCRegAliases() const {
  return llvm::ArrayRef(GCCRegAliases);
}
