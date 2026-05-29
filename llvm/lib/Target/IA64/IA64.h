//===-- IA64.h - Top-level interface for IA64 representation ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the LLVM
// IA64 back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_IA64_H
#define LLVM_LIB_TARGET_IA64_IA64_H

namespace llvm {

class FunctionPass;
class PassRegistry;
class TargetMachine;

FunctionPass *createIA64ISelDag(TargetMachine &TM);
FunctionPass *createIA64BundlingPass();
void initializeIA64DAGToDAGISelLegacyPass(PassRegistry &);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_IA64_H
