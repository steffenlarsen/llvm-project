//===- TargetSet.h - The targets a declaration belongs to -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A compilation for several targets at once numbers its targets by variant
// (see ASTContext::getTargetForVariant): 1 is the primary, 2 and up the aux
// targets. A TargetSet is a set of them, e.g. the targets a declaration
// belongs to.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_TARGETSET_H
#define LLVM_CLANG_AST_TARGETSET_H

#include "llvm/ADT/bit.h"
#include <cassert>
#include <cstdint>

namespace clang {

class TargetSet {
  uint64_t Bits = 0;

  explicit constexpr TargetSet(uint64_t Bits) : Bits(Bits) {}

public:
  /// The largest variant a set can hold.
  static constexpr unsigned MaxVariant = 62;

  constexpr TargetSet() = default;

  /// Every target, however many there are.
  static constexpr TargetSet all() { return TargetSet(~uint64_t(0)); }

  static TargetSet single(unsigned Variant) {
    assert(Variant >= 1 && Variant <= MaxVariant && "not a target variant");
    return TargetSet(uint64_t(1) << Variant);
  }

  bool contains(unsigned Variant) const {
    return Variant <= MaxVariant && (Bits >> Variant & 1);
  }
  void insert(unsigned Variant) {
    assert(Variant >= 1 && Variant <= MaxVariant && "not a target variant");
    Bits |= uint64_t(1) << Variant;
  }
  void erase(unsigned Variant) {
    assert(Variant >= 1 && Variant <= MaxVariant && "not a target variant");
    Bits &= ~(uint64_t(1) << Variant);
  }

  /// The lowest target in a set of some targets, or 0 if it is empty.
  unsigned first() const {
    assert(Bits != ~uint64_t(0) && "every target has no first one");
    return Bits ? llvm::countr_zero(Bits) : 0;
  }
  bool isSingle() const { return llvm::has_single_bit(Bits); }
  bool empty() const { return !Bits; }

  /// The set without \p Variant, where 0 names no target.
  TargetSet without(unsigned Variant) const {
    assert(Variant <= MaxVariant && "not a target variant");
    return TargetSet(Bits & ~(uint64_t(1) << Variant));
  }

  TargetSet operator&(TargetSet Other) const {
    return TargetSet(Bits & Other.Bits);
  }
  TargetSet operator|(TargetSet Other) const {
    return TargetSet(Bits | Other.Bits);
  }
  bool operator==(TargetSet Other) const { return Bits == Other.Bits; }
};

} // namespace clang

#endif // LLVM_CLANG_AST_TARGETSET_H
