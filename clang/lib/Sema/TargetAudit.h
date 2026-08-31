//===--- TargetAudit.h - Audit of target-divergent inputs -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// What each analysis of a multi-target compilation consumed that differs
// between targets (see SemaTargetAudit.cpp).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_SEMA_TARGETAUDIT_H
#define LLVM_CLANG_LIB_SEMA_TARGETAUDIT_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/TargetDivergence.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include <map>
#include <memory>
#include <string>

namespace clang {
class Decl;
class Type;

class TargetAudit {
public:
  /// One input an analysis consumed. Kind: 'L' lookup of a per-target entity,
  /// 'T' layout of a type or another target query, 'C' host-or-device mode,
  /// 'O' a language option, 'R' a call resolved for each side where it is
  /// made, 'D' a diagnostic's reporting, 'E' constant evaluation of a
  /// per-target definition, 'S' use of a specialization instantiated per
  /// target, 'K' use of a specialization keyed by target, 'F' a function
  /// reached from a body whose copies differ in what their bodies mean.
  /// Targets: which targets get which answer.
  struct Input {
    char Kind;
    ASTContext::TargetPartition Targets;
    std::string What;
    /// For -multi-target-audit-joins, where it is finer than Targets: which
    /// targets get answers that mean different things.
    ASTContext::TargetPartition JoinTargets;
  };

  std::string Path;
  /// What differs in layout among all the targets.
  std::unique_ptr<TargetDivergence> Divergence;
  /// Top-level declarations parsed for every target.
  unsigned ParseUnits = 0;
  /// Inputs of the top-level declaration being parsed.
  llvm::SmallVector<Input, 4> ParseInputs;
  /// For -multi-target-audit-joins, what the top-level declaration being
  /// parsed consumed; see ASTContext::noteParseInput.
  llvm::SmallVector<Input, 4> TargetParseInputs;
  /// Whether some of them were consumed by a lambda written as device code,
  /// so that even a host function is analysed for the devices.
  bool ParseInputsInDeviceLambda = false;
  /// Inputs per unit: a top-level declaration or an instantiation.
  llvm::MapVector<const Decl *, llvm::SmallVector<Input, 2>> Units;
  /// Layout divergence per type.
  llvm::DenseMap<const Type *, ASTContext::TargetPartition> Layout;
  /// Top-level declarations the target-set rule would analyse per target,
  /// by canonical declaration, with the targets told apart. Naming one is
  /// itself an input ('P', propagated), as it would be once they are split.
  llvm::DenseMap<const Decl *, ASTContext::TargetPartition> Split;
  /// Classes whose member declarations consumed a divergent input: the class
  /// is split with them.
  llvm::DenseMap<const Decl *, ASTContext::TargetPartition> MemberInputs;
  /// Lookup divergence per entity; for a variable, of its type, and in
  /// ValueDivergence of the variable itself.
  llvm::DenseMap<const Decl *, ASTContext::TargetPartition> EntityDivergence;
  llvm::DenseMap<const Decl *, ASTContext::TargetPartition> ValueDivergence;
  /// Per function, which targets' copies differ in what their bodies mean.
  llvm::DenseMap<const Decl *, ASTContext::TargetPartition> BodyDivergence;

  /// What splitting shared instantiations into per-target parts would do,
  /// for -multi-target-audit: instantiations split and their parts, those
  /// naming a divergent callee or member and how many of them no split
  /// covers, the same for functions parsed once, and parse units and member
  /// bodies 'F' newly splits.
  struct {
    unsigned Instantiations = 0, Split = 0, Parts = 0;
    unsigned Redirected = 0, Uncovered = 0;
    unsigned ParseRedirected = 0, ParseUncovered = 0;
    unsigned RedirectSplit = 0, RedirectParts = 0;
    unsigned ParseUnits = 0;
    llvm::DenseMap<const Decl *, ASTContext::TargetPartition> MemberBodies;
    llvm::SmallVector<std::string, 8> Lines;
  } SplitCounts;

  static void add(const ASTContext &Ctx, llvm::SmallVectorImpl<Input> &Inputs,
                  Input In) {
    for (Input &Old : Inputs)
      if (Old.Kind == In.Kind && Old.What == In.What) {
        Old.Targets = Ctx.refineTargetPartition(Old.Targets, In.Targets);
        Old.JoinTargets =
            Ctx.refineTargetPartition(Old.JoinTargets, In.JoinTargets);
        return;
      }
    Inputs.push_back(std::move(In));
  }
};

} // namespace clang

#endif
