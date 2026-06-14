// REQUIRES: ia64-registered-target
// RUN: %clang_cc1 -triple ia64 -emit-llvm -o - %s | FileCheck %s

// GCC exposes __float80 on IA-64; clang has no dedicated __float80 builtin and
// aliases it to long double, which on IA-64 is the 80-bit x87 extended format
// (commit 8821fe2f). Both map to the x86_fp80 IR type at 16-byte alignment.

#ifndef __float80
#error __float80 should be defined on IA-64
#endif

_Static_assert(sizeof(__float80) == sizeof(long double), "__float80 == long double");
_Static_assert(__LDBL_MANT_DIG__ == 64, "long double is 80-bit extended");

// CHECK-LABEL: define {{.*}}x86_fp80 @use_float80(x86_fp80
__float80 use_float80(__float80 x) {
  return x + 1;
}

// CHECK-LABEL: define {{.*}}x86_fp80 @use_long_double(x86_fp80
long double use_long_double(long double x) {
  return x + 1;
}
