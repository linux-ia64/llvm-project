// REQUIRES: ia64-registered-target
// RUN: %clang_cc1 -triple ia64 -emit-llvm -o - %s | FileCheck %s

// IA-64 homogeneous floating-point aggregate (HFA) classification. The psABI
// (IA64conventions.pdf 8.5.2) defines an HFA as an aggregate whose elements
// are *all* float, *all* double, or *all* double-extended, never a mix.
// clang's IA64ABIInfo::isHomogeneousAggregateBaseType /
// ABIInfo::isHomogeneousAggregate enforce this by requiring every leaf field
// to share one base type; a struct mixing float and double is therefore not
// an HFA at all, and falls back to the generic aggregate rule (coerced to
// [N x i64], one 64-bit slot per element, Table 8-1's "Aggregates" row).
//
// This matters for the backend's shadow-GR-slot pairing fix
// (IA64ISelLowering.cpp, CC_IA64_FP_Common / functionArgumentNeedsConsecutive
// Registers): it only special-cases a coerced [N x float] argument. The
// checks below confirm each struct shape coerces to the type the backend
// fix assumes; in particular, that a mixed float/double struct is *not*
// seen as a float HFA and so is untouched by that fix, and that pure double
// and long double HFAs keep their own (already-correct, one-slot/two-slot
// per element) coercion instead of the float-pairing path.

struct FloatHFA4 { float a, b, c, d; };
struct DoubleHFA3 { double a, b, c; };
struct LongDoubleHFA2 { long double a, b; };
struct MixedFloatDouble { float a; double b; };

extern long take_float_hfa(struct FloatHFA4 v);
long call_float_hfa(struct FloatHFA4 v) { return take_float_hfa(v); }

extern long take_double_hfa(struct DoubleHFA3 v);
long call_double_hfa(struct DoubleHFA3 v) { return take_double_hfa(v); }

extern long take_long_double_hfa(struct LongDoubleHFA2 v);
long call_long_double_hfa(struct LongDoubleHFA2 v) {
  return take_long_double_hfa(v);
}

extern long take_mixed(struct MixedFloatDouble v);
long call_mixed(struct MixedFloatDouble v) { return take_mixed(v); }

// A pure-float HFA coerces to an array of its own element type: the shape
// the backend's f32-pairing fix looks for.
// CHECK: define {{.*}} @call_float_hfa([4 x float] %v.coerce)
// CHECK: declare {{.*}} @take_float_hfa([4 x float])

// A pure-double HFA likewise coerces to an array of double. Each element
// already occupies one full 64-bit slot, so the backend leaves it on the
// ordinary (non-paired) FP argument path.
// CHECK: define {{.*}} @call_double_hfa([3 x double] %v.coerce)
// CHECK: declare {{.*}} @take_double_hfa([3 x double])

// A pure long-double HFA coerces to an array of x86_fp80 (LLVM's spelling of
// the 80-bit double-extended type); each element already occupies two full
// slots (Next Even), handled entirely by the existing f80 shadow-slot logic.
// CHECK: define {{.*}} @call_long_double_hfa([2 x x86_fp80] %v.coerce)
// CHECK: declare {{.*}} @take_long_double_hfa([2 x x86_fp80])

// A struct mixing float and double is not an HFA at all (no single base
// type), so it falls back to the generic [N x i64] slot coercion; it never
// reaches CC_IA64_FP_Common's float-pairing code at all.
// CHECK: define {{.*}} @call_mixed([2 x i64] %v.coerce)
// CHECK: declare {{.*}} @take_mixed([2 x i64])
