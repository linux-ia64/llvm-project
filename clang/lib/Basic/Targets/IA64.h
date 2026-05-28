//===--- IA64.h - declare IA-64 (Itanium) target feature support *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the IA-64 (Itanium) TargetInfo object. The type model is
// the IA-64 SysV psABI's LP64: 64-bit long/pointer, 80-bit extended long
// double stored/aligned in 16 bytes (not x86's 96-bit quirk). The datalayout
// is byte-identical to the backend's (TargetDataLayout.cpp, Triple::ia64) --
// that string is the contract between frontend struct layout and backend
// lowering, so the two must never diverge.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_IA64_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_IA64_H
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY IA64TargetInfo : public TargetInfo {
  static const char *const GCCRegNames[];
  static const TargetInfo::GCCRegAlias GCCRegAliases[];

  enum CPUKind { CK_GENERIC, CK_ITANIUM, CK_ITANIUM2 } CPU = CK_GENERIC;

public:
  IA64TargetInfo(const llvm::Triple &Triple, const TargetOptions &);

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  bool hasFeature(StringRef Feature) const override;

  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override {
    // No target builtins yet.
    return {};
  }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    // The IA-64 __gnuc_va_list is a plain pointer into the argument save area.
    return TargetInfo::VoidPtrBuiltinVaList;
  }

  ArrayRef<const char *> getGCCRegNames() const override;
  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override;

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override {
    // Inline-asm constraints are not modelled yet (no inline asm in scope).
    return false;
  }

  std::string_view getClobbers() const override { return ""; }

  bool isValidCPUName(StringRef Name) const override {
    return getCPUKind(Name) != CK_GENERIC || Name == "generic";
  }

  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override;

  bool setCPU(const std::string &Name) override {
    CPU = getCPUKind(Name);
    return CPU != CK_GENERIC || Name == "generic";
  }

  bool hasBitIntType() const override { return true; }

  // IA-64 has no native 128-bit integer and the backend does not lower i128;
  // don't offer __int128 (which would also require an i128 datalayout entry).
  bool hasInt128Type() const override { return false; }

private:
  CPUKind getCPUKind(StringRef Name) const;
};

} // namespace targets
} // namespace clang
#endif // LLVM_CLANG_LIB_BASIC_TARGETS_IA64_H
