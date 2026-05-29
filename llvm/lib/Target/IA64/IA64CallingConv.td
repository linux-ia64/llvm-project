//===-- IA64CallingConv.td - Calling conventions for IA64 --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This describes the calling conventions for the IA-64 architecture. The
// pre-removal backend hand-coded this logic in IA64ISelLowering; modern LLVM
// expresses it as CCState-driven tables, so the equivalent mapping is captured
// here and consumed via -gen-callingconv.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// IA-64 C argument calling convention.
//===----------------------------------------------------------------------===//
def CC_IA64 : CallingConv<[
  // Integer types smaller than a register are passed as i64.
  CCIfType<[i1, i8, i16, i32], CCPromoteToType<i64>>,

  // f32 is passed as f64.
  CCIfType<[f32], CCPromoteToType<f64>>,

  // The first eight integer arguments arrive in the incoming stacked GP
  // registers r32-r39.
  CCIfType<[i64], CCAssignToReg<[r32, r33, r34, r35, r36, r37, r38, r39]>>,

  // The first eight FP arguments arrive in F8-F15. Each one shadows (consumes)
  // the parallel integer parameter slot, matching IA-64's positional parameter
  // model.
  CCIfType<[f64],
           CCAssignToRegWithShadow<[F8, F9, F10, F11, F12, F13, F14, F15],
                                   [r32, r33, r34, r35, r36, r37, r38, r39]>>,

  // Everything beyond the registers is passed in 8-byte stack slots.
  CCIfType<[i64, f64], CCAssignToStack<8, 8>>
]>;

//===----------------------------------------------------------------------===//
// IA-64 C return-value convention.
//===----------------------------------------------------------------------===//
def RetCC_IA64 : CallingConv<[
  CCIfType<[i1, i8, i16, i32], CCPromoteToType<i64>>,
  CCIfType<[f32], CCPromoteToType<f64>>,

  // Integers return in r8, floating-point in F8.
  CCIfType<[i64], CCAssignToReg<[r8]>>,
  CCIfType<[f64], CCAssignToReg<[F8]>>
]>;
