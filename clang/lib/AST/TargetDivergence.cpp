//===- TargetDivergence.cpp - Multi-target divergence analysis ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/AST/TargetDivergence.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecordLayout.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

/// Width and alignment of \p K according to \p TI, or std::nullopt for kinds
/// whose layout TargetInfo does not describe directly.
static std::optional<std::pair<unsigned, unsigned>>
getLayout(const TargetInfo &TI, BuiltinType::Kind K) {
  switch (K) {
  case BuiltinType::Bool:
    return std::make_pair(TI.getBoolWidth(), TI.getBoolAlign());
  case BuiltinType::Char_S:
  case BuiltinType::Char_U:
  case BuiltinType::SChar:
  case BuiltinType::UChar:
    return std::make_pair(TI.getCharWidth(), TI.getCharAlign());
  case BuiltinType::Short:
  case BuiltinType::UShort:
    return std::make_pair(TI.getShortWidth(), TI.getShortAlign());
  case BuiltinType::Int:
  case BuiltinType::UInt:
    return std::make_pair(TI.getIntWidth(), TI.getIntAlign());
  case BuiltinType::Long:
  case BuiltinType::ULong:
    return std::make_pair(TI.getLongWidth(), TI.getLongAlign());
  case BuiltinType::LongLong:
  case BuiltinType::ULongLong:
    return std::make_pair(TI.getLongLongWidth(), TI.getLongLongAlign());
  case BuiltinType::Half:
    return std::make_pair(TI.getHalfWidth(), TI.getHalfAlign());
  case BuiltinType::Float:
    return std::make_pair(TI.getFloatWidth(), TI.getFloatAlign());
  case BuiltinType::Double:
    return std::make_pair(TI.getDoubleWidth(), TI.getDoubleAlign());
  case BuiltinType::LongDouble:
    return std::make_pair(TI.getLongDoubleWidth(), TI.getLongDoubleAlign());
  case BuiltinType::Float128:
    return std::make_pair(TI.getFloat128Width(), TI.getFloat128Align());
  default:
    return std::nullopt;
  }
}

/// Names for the kinds getLayout() describes. BuiltinType::getName needs an
/// instance and a PrintingPolicy; this analysis only ever names these.
static const char *kindName(BuiltinType::Kind K) {
  switch (K) {
  case BuiltinType::Bool:       return "bool";
  case BuiltinType::Char_S:     return "char";
  case BuiltinType::Char_U:     return "char";
  case BuiltinType::SChar:      return "signed char";
  case BuiltinType::UChar:      return "unsigned char";
  case BuiltinType::Short:      return "short";
  case BuiltinType::UShort:     return "unsigned short";
  case BuiltinType::Int:        return "int";
  case BuiltinType::UInt:       return "unsigned int";
  case BuiltinType::Long:       return "long";
  case BuiltinType::ULong:      return "unsigned long";
  case BuiltinType::LongLong:   return "long long";
  case BuiltinType::ULongLong:  return "unsigned long long";
  case BuiltinType::Half:       return "half";
  case BuiltinType::Float:      return "float";
  case BuiltinType::Double:     return "double";
  case BuiltinType::LongDouble: return "long double";
  case BuiltinType::Float128:   return "__float128";
  default:                      return "<other>";
  }
}

TargetDivergence::TargetDivergence(ArrayRef<const TargetInfo *> Targets)
    : DivergentBuiltins(BuiltinType::LastKind + 1) {
  // One target cannot diverge from itself.
  if (Targets.size() < 2)
    return;

  const TargetInfo &Ref = *Targets.front();
  for (unsigned K = 0; K <= BuiltinType::LastKind; ++K) {
    auto Kind = static_cast<BuiltinType::Kind>(K);
    std::optional<std::pair<unsigned, unsigned>> RefLayout =
        getLayout(Ref, Kind);
    if (!RefLayout)
      continue;
    for (const TargetInfo *TI : Targets.drop_front()) {
      if (getLayout(*TI, Kind) != RefLayout) {
        DivergentBuiltins.set(K);
        AnyDivergence = true;
        break;
      }
    }
  }

  // Pointer width differing would make almost every type divergent; record it
  // as such rather than pretending otherwise.
  for (const TargetInfo *TI : Targets.drop_front())
    if (TI->getPointerWidth(LangAS::Default) !=
        Ref.getPointerWidth(LangAS::Default))
      AnyDivergence = true;
}

bool TargetDivergence::isDivergent(BuiltinType::Kind K) const {
  return DivergentBuiltins.test(K);
}

bool TargetDivergence::isLayoutDivergent(QualType T) const {
  if (!AnyDivergence)
    return false;
  T = T.getCanonicalType();
  if (const auto *BT = T->getAs<BuiltinType>())
    return isDivergent(BT->getKind());
  if (const auto *AT = T->getAsArrayTypeUnsafe())
    return isLayoutDivergent(AT->getElementType());
  if (const auto *RT = T->getAs<RecordType>())
    return isLayoutDivergent(RT->getDecl());
  // Pointers, references and function types are only divergent if the pointer
  // width is, which is folded into AnyDivergence above; being conservative
  // here would make everything divergent for no benefit.
  return false;
}

bool TargetDivergence::isLayoutDivergent(const RecordDecl *RD) const {
  if (!AnyDivergence)
    return false;
  const RecordDecl *Def = RD ? RD->getDefinition() : nullptr;
  if (!Def)
    return true; // incomplete: cannot prove it is safe
  for (const FieldDecl *F : Def->fields())
    if (isLayoutDivergent(F->getType()))
      return true;
  return false;
}

void TargetDivergence::print(raw_ostream &OS) const {
  if (!AnyDivergence) {
    OS << "target divergence: none; every target-dependent result is shared\n";
    return;
  }
  OS << "target divergence: divergent builtin types:";
  bool Any = false;
  for (unsigned K = 0; K <= BuiltinType::LastKind; ++K)
    if (DivergentBuiltins.test(K)) {
      OS << ' ' << kindName(static_cast<BuiltinType::Kind>(K));
      Any = true;
    }
  if (!Any)
    OS << " (none; divergence is in pointer width)";
  OS << '\n';
}
