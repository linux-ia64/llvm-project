// REQUIRES: ia64-registered-target
// RUN: %clang_cc1 -triple ia64 -emit-llvm -o - %s | FileCheck %s

// IA-64 argument vs. return-value extension. Per the psABI, a sub-64-bit
// argument slot is padded on the left with undefined bits (§8.5.1), so
// arguments must not be marked signext/zeroext; a sub-32-bit return value
// must be sign/zero-extended by the callee (§8.6), so returns are marked
// signext/zeroext (IA64ABIInfo::classifyArgumentType / classifyReturnType).

// CHECK-LABEL: define {{.*}} @arg_char(i8 noundef %x)
void arg_char(char x) {}

// CHECK-LABEL: define {{.*}} @arg_uchar(i8 noundef %x)
void arg_uchar(unsigned char x) {}

// CHECK-LABEL: define {{.*}} @arg_short(i16 noundef %x)
void arg_short(short x) {}

// CHECK-LABEL: define {{.*}} @arg_ushort(i16 noundef %x)
void arg_ushort(unsigned short x) {}

// CHECK-LABEL: define {{.*}} @arg_int(i32 noundef %x)
void arg_int(int x) {}

// CHECK: define dso_local signext i8 @ret_char
char ret_char(char x) { return x; }

// CHECK: define dso_local zeroext i8 @ret_uchar
unsigned char ret_uchar(unsigned char x) { return x; }

// CHECK: define dso_local signext i16 @ret_short
short ret_short(short x) { return x; }

// CHECK: define dso_local zeroext i16 @ret_ushort
unsigned short ret_ushort(unsigned short x) { return x; }

// int is already 32 bits wide, so it is not "promotable" and gets no
// extension attribute even on return.
// CHECK: define dso_local i32 @ret_int
int ret_int(int x) { return x; }
