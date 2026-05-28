//===-- IA64TargetInfo.cpp - IA64 Target Implementation -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/IA64TargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
using namespace llvm;

Target &llvm::getTheIA64Target() {
  static Target TheIA64Target;
  return TheIA64Target;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeIA64TargetInfo() {
  RegisterTarget<Triple::ia64, /*HasJIT=*/false> X(
      getTheIA64Target(), "ia64", "IA-64 (Itanium)", "IA64");
}
