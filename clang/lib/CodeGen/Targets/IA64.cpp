//===- IA64.cpp -----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "TargetInfo.h"

using namespace clang;
using namespace clang::CodeGen;

//===----------------------------------------------------------------------===//
// IA-64 (Itanium) SysV ABI Implementation.
//
// Aggregates are passed and returned *by value*, flattened into 64-bit
// parameter slots per the psABI (IA64conventions.pdf §8.5/§8.6):
//
//  * A non-homogeneous aggregate is mapped to (size+63)/64 consecutive 64-bit
//    slots ("Byte 0" alignment). For arguments the first eight slots land in
//    out0-out7 and the rest on the stack; for return values 1-256 bits land in
//    r8-r11. We express this by coercing to an [N x i64] (or i64) "direct" type
//    and letting the backend's CCAssignToReg/Stack split it.
//  * A homogeneous floating-point aggregate (HFA: all float, all double, or all
//    long double; up to eight elements) is passed/returned in f8-f15. We coerce
//    to [N x base] so each element gets its own FP register.
//  * Aggregate return values larger than 256 bits are returned in a caller-
//    allocated buffer whose address is passed in r8 (sret); the backend's
//    CCIfSRet rule routes it there.
//
// TODO: 16-byte-aligned aggregates should use the "Next Even" slot policy (skip
// an odd slot); the [N x i64] coercion does not yet convey that to the backend.
//===----------------------------------------------------------------------===//

namespace {
class IA64ABIInfo : public ABIInfo {
public:
  IA64ABIInfo(CodeGenTypes &CGT) : ABIInfo(CGT) {}

private:
  /// Aggregate return values strictly larger than this many bits go in memory.
  static constexpr uint64_t MaxReturnRegBits = 256;

  ABIArgInfo classifyReturnType(QualType RetTy) const;
  ABIArgInfo classifyArgumentType(QualType Ty) const;

  /// Coerce an aggregate to the [N x i64] (or plain i64) "direct" type that the
  /// backend splits across the integer parameter / return registers.
  llvm::Type *coerceToIntSlots(uint64_t SizeBits) const;
  /// Coerce a homogeneous FP aggregate to [Members x Base].
  ABIArgInfo coerceHFA(const Type *Base, uint64_t Members) const;

  bool isHomogeneousAggregateBaseType(QualType Ty) const override;
  bool isHomogeneousAggregateSmallEnough(const Type *Base,
                                         uint64_t Members) const override;

  void computeInfo(CGFunctionInfo &FI) const override {
    if (!getCXXABI().classifyReturnType(FI))
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
    for (auto &Arg : FI.arguments())
      Arg.info = classifyArgumentType(Arg.type);
  }

  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override;
};

class IA64TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  IA64TargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<IA64ABIInfo>(CGT)) {}
};
} // end anonymous namespace

bool IA64ABIInfo::isHomogeneousAggregateBaseType(QualType Ty) const {
  // The psABI HFA base types are single-, double-, and double-extended-
  // precision floating point (not quad). Each element is passed in one FR.
  if (const BuiltinType *BT = Ty->getAs<BuiltinType>()) {
    switch (BT->getKind()) {
    case BuiltinType::Float:
    case BuiltinType::Double:
    case BuiltinType::LongDouble:
      return true;
    default:
      break;
    }
  }
  return false;
}

bool IA64ABIInfo::isHomogeneousAggregateSmallEnough(const Type *Base,
                                                    uint64_t Members) const {
  // f8-f15: at most eight individual values.
  return Members <= 8;
}

llvm::Type *IA64ABIInfo::coerceToIntSlots(uint64_t SizeBits) const {
  llvm::Type *I64 = llvm::Type::getInt64Ty(getVMContext());
  uint64_t NumSlots = (SizeBits + 63) / 64;
  if (NumSlots <= 1)
    return I64;
  return llvm::ArrayType::get(I64, NumSlots);
}

ABIArgInfo IA64ABIInfo::coerceHFA(const Type *Base, uint64_t Members) const {
  llvm::Type *EltTy = CGT.ConvertType(QualType(Base, 0));
  llvm::Type *CoerceTy =
      Members == 1 ? EltTy : llvm::ArrayType::get(EltTy, Members);
  return ABIArgInfo::getDirect(CoerceTy);
}

ABIArgInfo IA64ABIInfo::classifyArgumentType(QualType Ty) const {
  Ty = useFirstFieldIfTransparentUnion(Ty);

  if (!isAggregateTypeForABI(Ty)) {
    // Treat an enum as its underlying integer type.
    if (const EnumType *ET = Ty->getAs<EnumType>())
      Ty = ET->getDecl()->getIntegerType();

    if (isPromotableIntegerTypeForABI(Ty))
      return ABIArgInfo::getExtend(Ty);
    return ABIArgInfo::getDirect();
  }

  // Aggregates are passed by value. Empty aggregates take no slot.
  uint64_t Size = getContext().getTypeSize(Ty);
  if (Size == 0)
    return ABIArgInfo::getIgnore();

  // Homogeneous FP aggregate -> f8-f15.
  const Type *Base = nullptr;
  uint64_t Members = 0;
  if (isHomogeneousAggregate(Ty, Base, Members))
    return coerceHFA(Base, Members);

  // Everything else: flatten into 64-bit integer slots.
  return ABIArgInfo::getDirect(coerceToIntSlots(Size));
}

ABIArgInfo IA64ABIInfo::classifyReturnType(QualType RetTy) const {
  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();

  if (!isAggregateTypeForABI(RetTy)) {
    if (const EnumType *ET = RetTy->getAs<EnumType>())
      RetTy = ET->getDecl()->getIntegerType();

    if (isPromotableIntegerTypeForABI(RetTy))
      return ABIArgInfo::getExtend(RetTy);
    return ABIArgInfo::getDirect();
  }

  uint64_t Size = getContext().getTypeSize(RetTy);
  if (Size == 0)
    return ABIArgInfo::getIgnore();

  // Homogeneous FP aggregate -> f8-f15.
  const Type *Base = nullptr;
  uint64_t Members = 0;
  if (isHomogeneousAggregate(RetTy, Base, Members))
    return coerceHFA(Base, Members);

  // Aggregates up to 256 bits return in r8-r11; larger ones in a caller buffer
  // whose address is passed in r8 (sret, routed by the backend's CCIfSRet rule).
  if (Size > MaxReturnRegBits)
    return getNaturalAlignIndirect(RetTy, getDataLayout().getAllocaAddrSpace(),
                                   /*ByVal=*/false);

  return ABIArgInfo::getDirect(coerceToIntSlots(Size));
}

RValue IA64ABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                              QualType Ty, AggValueSlot Slot) const {
  // The va_list is a single pointer walking a contiguous image of 64-bit slots
  // (see the backend's va_start lowering). Each argument occupies a whole
  // number of 8-byte slots in that image; aggregates are read in place.
  ABIArgInfo AI = classifyArgumentType(Ty);
  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, /*IsIndirect=*/AI.isIndirect(),
                          getContext().getTypeInfoInChars(Ty),
                          CharUnits::fromQuantity(8),
                          /*AllowHigherAlign=*/false, Slot);
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createIA64TargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<IA64TargetCodeGenInfo>(CGM.getTypes());
}
