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
// A combined AST carries declarations for several targets, so it is
// legitimately different from any single target's own AST -- comparing it
// byte-for-byte against an unmodified compiler's output does not work. What
// has to hold instead is that the declarations belonging to a target are the
// ones that target's own compilation would produce.
//
// Comparing -ast-dump output does not answer that: it embeds pointers and
// source locations and is ordered, while a combined parse visits a divergent
// region once per target and so produces declarations in a different order.
// A fingerprint carries only identity and shape, and is compared sorted.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_DECLFINGERPRINT_H
#define LLVM_CLANG_AST_DECLFINGERPRINT_H

#include "clang/Basic/LLVM.h"
#include "clang/Support/Compiler.h"

namespace clang {

class ASTContext;

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

} // namespace clang

#endif
