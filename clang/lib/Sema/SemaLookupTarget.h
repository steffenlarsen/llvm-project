//===--- SemaLookupTarget.h - Lookup for several targets --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Name lookup helpers for a compilation for several targets. Not installed.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_SEMA_SEMALOOKUPTARGET_H
#define LLVM_CLANG_LIB_SEMA_SEMALOOKUPTARGET_H

#include "llvm/ADT/STLFunctionalExtras.h"

namespace clang {

class FieldDecl;
class NamedDecl;
class Sema;

/// Retry a member-name lookup scoped to one specific widened RecordDecl copy
/// against that class's sibling widened copies, for the case where the only
/// candidate found was hidden by merging fields across target copies. Returns
/// the live (non-TargetVariantRedundant) sibling member, or null if none is
/// found.
NamedDecl *findLiveFieldInSiblingRecord(const FieldDecl *D);

/// Returns the shared function or function template that a
/// TargetVariantRedundant one was folded into, or null.
NamedDecl *findLiveFunctionSibling(NamedDecl *D);

/// Returns the copy of \p D parsed from the same declaration for target
/// variant \p Variant, or null.
NamedDecl *findTargetVariantSibling(NamedDecl *D, unsigned Variant);

namespace multitarget {

/// The declaration whose target copies \p D stands for. That is a using
/// shadow's target unless the shadow is a copy itself.
const NamedDecl *getTargetVariantCarrier(const NamedDecl *D);

/// Whether \p D is visible to code analysed for the current target.
bool isVisibleForTarget(Sema &S, const NamedDecl *D, bool ForRedecl,
                        bool AllowFallback,
                        bool SuppressDivergenceFlagForTags = false,
                        bool PreferPrimaryForRecordReferences = false,
                        bool SoleCandidate = false);

/// The redeclaration of \p D that \p IsVisible accepts.
NamedDecl *
findTargetVisibleRedecl(NamedDecl *D,
                        llvm::function_ref<bool(const NamedDecl *)> IsVisible);

} // namespace multitarget
} // namespace clang

#endif // LLVM_CLANG_LIB_SEMA_SEMALOOKUPTARGET_H
