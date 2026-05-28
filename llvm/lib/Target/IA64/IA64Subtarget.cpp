//===-- IA64Subtarget.cpp - IA64 Subtarget Information --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the IA64 specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "IA64Subtarget.h"
#include "IA64.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "IA64GenSubtargetInfo.inc"

void IA64Subtarget::anchor() {}

IA64Subtarget::IA64Subtarget(const Triple &TT, StringRef CPU, StringRef TuneCPU,
                             StringRef FS, const TargetMachine &TM)
    : IA64GenSubtargetInfo(TT, CPU, TuneCPU, FS), FrameLowering(),
      InstrInfo(*this), TLInfo(TM, *this) { }
