//===--- SemaLookupTarget.h - Lookup for several targets at once -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// What SemaLookup.cpp uses of SemaLookupTarget.cpp. Not installed.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_SEMA_SEMALOOKUPTARGET_H
#define LLVM_CLANG_LIB_SEMA_SEMALOOKUPTARGET_H

#include "llvm/ADT/STLFunctionalExtras.h"

namespace clang {

class NamedDecl;
class Sema;

namespace multitarget {

/// The declaration whose target copies \p D stands for: a using shadow's
/// target unless the shadow is a copy itself.
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
