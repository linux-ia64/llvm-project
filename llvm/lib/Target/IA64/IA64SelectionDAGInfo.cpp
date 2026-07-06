//===-- IA64SelectionDAGInfo.cpp - IA64 SelectionDAG Info -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the IA64 specific subclass of SelectionDAGTargetInfo.
//
//===----------------------------------------------------------------------===//

#include "IA64SelectionDAGInfo.h"

#define GET_SDNODE_DESC
#include "IA64GenSDNodeInfo.inc"

using namespace llvm;

IA64SelectionDAGInfo::IA64SelectionDAGInfo()
    : SelectionDAGGenTargetInfo(IA64GenSDNodeInfo) {}

IA64SelectionDAGInfo::~IA64SelectionDAGInfo() = default;
