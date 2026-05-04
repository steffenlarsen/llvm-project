//===-- OpDescriptor.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/FuzzMutate/OpDescriptor.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/CommandLineV2.h"

using namespace llvm;
using namespace fuzzerop;

static constexpr clv2::OptionInfo<bool> OI_UseUndef{
    "use-undef", "Use undef when generating programs.", clv2::Hidden};
static constexpr clv2::OptionsRegistry<&OI_UseUndef> UseUndefReg;

/// Read the option from the LLVMContext that owns the IR being generated, so
/// that concurrent jobs using different settings do not interfere.
static bool useUndef(const LLVMContext &Ctx) {
  return clv2::getOptValOr<&UseUndefReg, &OI_UseUndef>(Ctx.getOptionsContext(),
                                                       false);
}

// No apply function: the option is read from the OptionsContext at its point
// of use rather than mirrored into a process-wide global.
[[maybe_unused]] static const bool Registered = [] {
  clv2::registerDynamicRegistry<&UseUndefReg>();
  return true;
}();

void fuzzerop::makeConstantsWithType(Type *T, std::vector<Constant *> &Cs) {
  if (auto *IntTy = dyn_cast<IntegerType>(T)) {
    uint64_t W = IntTy->getBitWidth();
    Cs.push_back(ConstantInt::get(IntTy, 0));
    Cs.push_back(ConstantInt::get(IntTy, 1));
    Cs.push_back(ConstantInt::get(IntTy, 42, /*IsSigned=*/false,
                                  /*ImplicitTrunc=*/true));
    Cs.push_back(ConstantInt::get(IntTy, APInt::getMaxValue(W)));
    Cs.push_back(ConstantInt::get(IntTy, APInt::getMinValue(W)));
    Cs.push_back(ConstantInt::get(IntTy, APInt::getSignedMaxValue(W)));
    Cs.push_back(ConstantInt::get(IntTy, APInt::getSignedMinValue(W)));
    Cs.push_back(ConstantInt::get(IntTy, APInt::getOneBitSet(W, W / 2)));
  } else if (T->isFloatingPointTy()) {
    auto &Ctx = T->getContext();
    auto &Sem = T->getFltSemantics();
    Cs.push_back(ConstantFP::get(Ctx, APFloat::getZero(Sem)));
    Cs.push_back(ConstantFP::get(Ctx, APFloat(Sem, 1)));
    Cs.push_back(ConstantFP::get(Ctx, APFloat(Sem, 42)));
    Cs.push_back(ConstantFP::get(Ctx, APFloat::getLargest(Sem)));
    Cs.push_back(ConstantFP::get(Ctx, APFloat::getSmallest(Sem)));
    Cs.push_back(ConstantFP::get(Ctx, APFloat::getInf(Sem)));
    Cs.push_back(ConstantFP::get(Ctx, APFloat::getNaN(Sem)));
  } else if (VectorType *VecTy = dyn_cast<VectorType>(T)) {
    std::vector<Constant *> EleCs;
    Type *EltTy = VecTy->getElementType();
    makeConstantsWithType(EltTy, EleCs);
    ElementCount EC = VecTy->getElementCount();
    for (Constant *Elt : EleCs) {
      Cs.push_back(ConstantVector::getSplat(EC, Elt));
    }
  } else {
    if (useUndef(T->getContext()))
      Cs.push_back(UndefValue::get(T));
    Cs.push_back(PoisonValue::get(T));
  }
}

std::vector<Constant *> fuzzerop::makeConstantsWithType(Type *T) {
  std::vector<Constant *> Result;
  makeConstantsWithType(T, Result);
  return Result;
}
