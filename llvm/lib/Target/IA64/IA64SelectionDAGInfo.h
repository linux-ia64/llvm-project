//===-- IA64SelectionDAGInfo.h - IA64 SelectionDAG Info ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the IA64 subclass for SelectionDAGTargetInfo. Its main job
// is to carry the TableGen-generated IA64ISD node descriptions (from the
// SDNode<"IA64ISD::..."> definitions in IA64InstrInfo.td), which supply the
// IA64ISD enum, getTargetNodeName and debug-time node verification.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_IA64SELECTIONDAGINFO_H
#define LLVM_LIB_TARGET_IA64_IA64SELECTIONDAGINFO_H

#include "llvm/CodeGen/SelectionDAGTargetInfo.h"

#define GET_SDNODE_ENUM
#include "IA64GenSDNodeInfo.inc"

namespace llvm {

class IA64SelectionDAGInfo : public SelectionDAGGenTargetInfo {
public:
  IA64SelectionDAGInfo();

  ~IA64SelectionDAGInfo() override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_IA64_IA64SELECTIONDAGINFO_H
