//===-- IA64MCAsmInfo.h - IA64 asm properties ------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the IA64MCAsmInfo class. It is the
// modern (MC-layer) replacement for the pre-removal IA64TargetAsmInfo, which
// subclassed the long-deleted ELFTargetAsmInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64MCASMINFO_H
#define LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64MCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {

class MCTargetOptions;
class Triple;

class IA64MCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit IA64MCAsmInfo(const Triple &TheTriple,
                         const MCTargetOptions &Options);
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_IA64_MCTARGETDESC_IA64MCASMINFO_H
