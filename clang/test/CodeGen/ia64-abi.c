// REQUIRES: ia64-registered-target
// RUN: %clang_cc1 -triple ia64 -emit-llvm -o - %s | FileCheck %s

// IA-64 aggregate by-value passing. Small aggregates are coerced to an array of
// i64 argument slots. A 16-byte-aligned aggregate must begin on an even slot
// ("Next Even"): if the preceding arguments leave the next slot odd, clang
// inserts an i64 padding argument so the aggregate starts even (commit
// 9a689f1e, IA64ABIInfo::classifyArgumentType).

struct S16 { long a; long b; } __attribute__((aligned(16)));

// %x occupies slot 0, so the 16-byte-aligned aggregate would land on odd slot
// 1; an i64 padding argument is inserted to push it to even slot 2.
// CHECK-LABEL: define {{.*}} @f_pad(
// CHECK-SAME: i64 noundef %x, i64 %{{[0-9]+}}, [2 x i64] %s.coerce)
long f_pad(long x, struct S16 s) {
  return x + s.a + s.b;
}

// With no preceding argument the aggregate already starts on even slot 0, so no
// padding argument is inserted.
// CHECK-LABEL: define {{.*}} @f_nopad(
// CHECK-SAME: [2 x i64] %s.coerce)
long f_nopad(struct S16 s) {
  return s.a + s.b;
}
