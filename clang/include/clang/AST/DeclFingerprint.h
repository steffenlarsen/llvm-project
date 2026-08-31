//===- DeclFingerprint.h - Order-independent AST comparison -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Prints one normalised line per declaration, so two compilations' ASTs can be
// compared as sets.
//
// Every increment of the combined-frontend work so far could be checked by
// byte-identical output against an unmodified compiler. Once one AST carries
// declarations for several targets that stops being possible: the combined AST
// is legitimately different from either target's. What has to hold instead is
// that the declarations belonging to a target are the ones that target's own
// compilation produces.
//
// Comparing -ast-dump output does not answer that. It embeds pointers and
// source locations, and it is ordered -- and a combined parse visits a
// divergent region once per target, so declaration order legitimately differs.
// A fingerprint carries only identity and shape, and is compared sorted.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_DECLFINGERPRINT_H
#define LLVM_CLANG_AST_DECLFINGERPRINT_H

#include "clang/Basic/LLVM.h"
#include "clang/Support/Compiler.h"
#include "llvm/Support/CommandLine.h"
#include <string>

namespace clang {

class ASTContext;

/// PROTOTYPE: see DeclFingerprint.cpp. With -merge-equivalent-variants, also
/// hide the copy that turned out to match the original.
LLVM_ABI extern llvm::cl::opt<bool> HideRedundantVariants;

/// PROTOTYPE: see DeclFingerprint.cpp. Report declarations whose name
/// contains this string as mergeEquivalentVariants un-claims them.
LLVM_ABI extern llvm::cl::opt<std::string> TraceUnclaim;

/// Print a fingerprint line for every declaration in \p Ctx's translation unit.
///
/// \param Variant Which target's declarations to print. Declarations marked as
/// belonging to every target are always printed; those marked for a specific
/// target are printed only when it matches. 0 prints everything, which is what
/// an ordinary single-target compilation contains.
///
/// The variant itself is deliberately *not* part of a line: a separate
/// per-target compilation marks nothing, so including it would make every line
/// differ. It selects which lines appear, not what they say.
LLVM_ABI void printDeclFingerprints(raw_ostream &OS, ASTContext &Ctx,
                                    unsigned Variant);

/// Report how many target-tagged declarations are structurally the same.
///
/// Everything parsed inside a divergent region inherits that region's target,
/// so a declaration identical in every arm is tagged anyway. Measured on
/// ggml-cuda, 23 declarations are tagged for every one that actually differs,
/// and each tagged declaration makes every shared user of it a candidate for
/// re-parsing. This checks the cheap signal (fingerprint equality) against the
/// real one (structural equivalence) before anything is merged on the strength
/// of it.
LLVM_ABI void reportVariantEquivalence(raw_ostream &OS, ASTContext &Ctx);

/// Un-claim target-tagged declarations whose targets agree.
///
/// Everything parsed inside a divergent region, and everything a divergent
/// user's re-parse touches, is tagged for a target whether or not it depends on
/// one. Measured on ggml-cuda, 996 of 1,011 pairs are structurally equivalent
/// and the 15 that are not are the same 15 on every translation unit --
/// `numeric_limits<long double>`, `hardware_destructive_interference_size`, and
/// two locals in HIP intrinsics. Reverting the primary's copy to "every target"
/// keeps it, and everything instantiated from it, visible to the others.
///
/// Must run before pending instantiations: an instantiation inherits its
/// pattern's target, so a template left claimed takes all of its
/// specializations with it -- 6,232 locals of `launch_bin_bcast_pack` alone.
///
/// \returns how many declarations were un-claimed.
LLVM_ABI unsigned mergeEquivalentVariants(ASTContext &Ctx);

} // namespace clang

#endif
