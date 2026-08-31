//===--- SemaMultiTargetInstantiate.h - Instantiation for targets -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// What template instantiation uses of SemaMultiTargetInstantiate.cpp. Not
// installed.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_SEMA_SEMAMULTITARGETINSTANTIATE_H
#define LLVM_CLANG_LIB_SEMA_SEMAMULTITARGETINSTANTIATE_H

namespace clang {

class ASTContext;
class Decl;
class FunctionDecl;
class NamedDecl;
class Sema;

namespace multitarget {

/// The definition \p Function is instantiated from: of a template re-parsed
/// for each target, the copy of the target its key names.
const FunctionDecl *getTargetPattern(Sema &S, const FunctionDecl *Function,
                                     const FunctionDecl *Pattern);

/// The definition of \p Pattern's template that \p Variant compiles.
const FunctionDecl *getDefinitionForTarget(const FunctionDecl *Pattern,
                                           unsigned Variant);

/// Whether device code \p Function is instantiated before it is known which
/// devices compile it.
bool isTentativeTargetInstantiation(Sema &S, FunctionDecl *Function);

/// The target a specialization, or the outermost specialization enclosing it,
/// was made for, or 0; \p Owner is set to that specialization.
unsigned getDeviceViewOwner(const ASTContext &Context, const Decl *D,
                            const Decl *&Owner);

/// Whether \p Function, or a class it is a member of, is instantiated with a
/// class specialization keyed by the host.
bool namesHostKeyedClass(const ASTContext &Context,
                         const FunctionDecl *Function);

/// The class \p D is a member of, if it is some targets' only.
const Decl *getTaggedEnclosingClass(const Decl *D);

/// \p D's copy for \p Target, if \p D is another target's copy.
const NamedDecl *getCopyForTarget(const ASTContext &Context, const NamedDecl *D,
                                  unsigned Target);

} // namespace multitarget
} // namespace clang

#endif // LLVM_CLANG_LIB_SEMA_SEMAMULTITARGETINSTANTIATE_H
