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
/// diverge in body -- either a specialization of a FunctionTemplateDecl with
/// a body-divergent redeclaration chain (see
/// FunctionTemplateDecl::hasTargetTaggedRedeclaration()), or a plain
/// (non-template) FunctionDecl with a genuinely tagged sibling reachable via
/// ordinary name lookup in its DeclContext. Used by
/// SemaMultiTargetInstantiate.cpp's DivergentCalleeVisitor (within an
/// already-instantiated function body, to split it) and by
/// ClassTemplateDecl::hasTargetTaggedValueMember() (within an uninstantiated
/// static-data-member initializer).
LLVM_ABI bool isTargetDivergentCallee(const ValueDecl *D);

/// True if \p D is a static data member of a class template specialization
/// whose FoldingSet identity is target-forked by
/// ClassTemplateSpecializationDecl::Profile (i.e.
/// hasTargetTaggedPartialSpecialization() || hasTargetTaggedValueMember() on
/// the specialized template) -- meaning a *reference* to \p D (not just a
/// call, unlike isTargetDivergentCallee above) can resolve to a different,
/// per-target answer if re-looked-up under a different ambient target
/// variant. Mirrors Profile()'s own disjunction so this neither over- nor
/// under-triggers relative to what Profile can actually fork. A shared body
/// reading e.g. tile_like<16,16,int>::ne is split per target.
LLVM_ABI bool isTargetDivergentValueMemberAccess(const ValueDecl *D);

/// True if \p D is a CXXMethodDecl declared directly on a class template
/// specialization whose FoldingSet identity is target-forked by
/// ClassTemplateSpecializationDecl::Profile (same disjunction as
/// isTargetDivergentValueMemberAccess above) -- meaning a *call* through an
/// already-resolved MemberExpr/DeclRefExpr naming \p D can resolve to a
/// different, per-target method body if re-looked-up under a different
/// ambient target variant. isTargetDivergentCallee above cannot detect this:
/// its hasTargetWidenedSiblingDecl sibling search only looks inside \p D's
/// own DeclContext, but each target's fork of the owning class template
/// specialization is a physically separate DeclContext, so a sibling method
/// in a different fork is never found that way -- e.g.
/// ggml_cuda_mma::tile<16,16,float,...>::get_i()/get_j(), whose owning
/// tile<> specialization is forked per real target (see
/// ClassTemplateDecl::hasTargetTaggedPrimaryTemplate) but whose call sites
/// (t.get_i(l)) are ordinary object-syntax member calls, never re-resolved
/// after the first target's Sema pass binds them. A shared body calling such
/// a method is split per target.
LLVM_ABI bool isTargetDivergentMemberFunctionAccess(const ValueDecl *D);

/// True if \p D is a typedef declared directly on such a class template
/// specialization, which each target has its own of.
LLVM_ABI bool isTargetDivergentTypeMemberAccess(const TypeDecl *D);

/// True if \p FD has a genuinely tagged (non-reparse-origin, non-redundant)
/// sibling FunctionDecl reachable via ordinary name lookup in its own
/// DeclContext -- i.e. FD's own text was itself #if/#elif-widened. Used by
/// isTargetDivergentCallee() above.
LLVM_ABI bool hasTargetWidenedSiblingDecl(const FunctionDecl *FD);

/// True if any Stmt/Expr transitively reachable within \p S invokes, or is a
/// MemberExpr naming, a target-divergent callee (see isTargetDivergentCallee
/// above). CodeGen's constant-folding fast paths
/// (Expr::EvaluateAsInt/EvaluateAsRValue) evaluate straight through the
/// Sema-resolved callee, not the copy each target emits under the same name,
/// so those fast paths must be skipped in favor of the normal call/lvalue
/// emission path whenever this returns true.
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
