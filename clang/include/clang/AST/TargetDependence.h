//===- TargetDependence.h - Code that differs between targets ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Predicates telling whether a reference in code compiled for several targets
// at once can mean something different for each of them.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_TARGETDEPENDENCE_H
#define LLVM_CLANG_AST_TARGETDEPENDENCE_H

#include "clang/Basic/LLVM.h"
#include "clang/Support/Compiler.h"
#include "llvm/ADT/STLFunctionalExtras.h"

namespace clang {

class ASTContext;
class CXXRecordDecl;
class FunctionDecl;
class QualType;
class Stmt;
class TemplateArgument;
class TypeDecl;
class ValueDecl;

/// True if \p D is a FunctionDecl whose real per-target siblings genuinely
/// diverge in body. That is either a specialization of a FunctionTemplateDecl
/// with a body-divergent redeclaration chain, or a plain (non-template)
/// FunctionDecl with a genuinely tagged sibling reachable via ordinary name
/// lookup in its DeclContext.
LLVM_ABI bool isTargetDivergentCallee(const ValueDecl *D);

/// True if \p D is a static data member of a class template specialization
/// whose FoldingSet identity is forked per target. A *reference* to \p D, not
/// just a call, can then resolve to a different, per-target answer if looked
/// up again under a different ambient target variant. The test matches
/// exactly the conditions under which such a specialization is forked, so it
/// neither over- nor under-triggers. A shared body reading such a member is
/// split per target.
LLVM_ABI bool isTargetDivergentValueMemberAccess(const ValueDecl *D);

/// True if \p D is a CXXMethodDecl declared directly on a class template
/// specialization whose FoldingSet identity is forked per target. A *call*
/// through an already-resolved MemberExpr or DeclRefExpr naming \p D can then
/// resolve to a different, per-target method body if looked up again under a
/// different ambient target variant. A sibling search in \p D's own
/// DeclContext cannot find this, as each target's fork of the owning class
/// template specialization is a separate DeclContext. Object-syntax member
/// calls are also never resolved again once the first target's analysis
/// binds them. A shared body calling such a method is split per target.
LLVM_ABI bool isTargetDivergentMemberFunctionAccess(const ValueDecl *D);

/// True if \p D is a typedef declared directly on such a class template
/// specialization, which each target has its own of.
LLVM_ABI bool isTargetDivergentTypeMemberAccess(const TypeDecl *D);

/// True if \p FD has a genuinely tagged (non-reparse-origin, non-redundant)
/// sibling FunctionDecl reachable via ordinary name lookup in its own
/// DeclContext, i.e. FD's own text was itself #if/#elif-widened.
LLVM_ABI bool hasTargetWidenedSiblingDecl(const FunctionDecl *FD);

/// True if any Stmt or Expr transitively reachable within \p S invokes, or is
/// a MemberExpr naming, a target-divergent callee. Constant folding evaluates
/// straight through the Sema-resolved callee, not the copy each target emits
/// under the same name, so code generation must emit such code as calls and
/// lvalues rather than fold it.
LLVM_ABI bool containsTargetDivergentCallee(const Stmt *S);

/// Calls \p Visit with each class \p T names, and each class enclosing one,
/// until it returns true, and returns whether it did. Looks through pointers,
/// references, arrays, function and member pointer types, and the type
/// arguments of class specializations, packs included, to a fixed depth.
LLVM_ABI bool
anyNamedClass(const ASTContext &Ctx, QualType T,
              llvm::function_ref<bool(const CXXRecordDecl *)> Visit);
/// The same for the classes the type arguments \p Args name.
LLVM_ABI bool
anyNamedClass(const ASTContext &Ctx, ArrayRef<TemplateArgument> Args,
              llvm::function_ref<bool(const CXXRecordDecl *)> Visit);

} // namespace clang

#endif // LLVM_CLANG_AST_TARGETDEPENDENCE_H
