//===--- MultiTargetDebugOptions.cpp - Multi-target debugging aids --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/MultiTargetDebugOptions.h"

using namespace llvm;

/// Writes each pre-backend CodeGenModule's IR (primary and, in a multi-target
/// compile, aux) to
/// <dir>/<input-stem>.{primary,aux}.ll, for comparison against separately
/// invoked single-target compiles of the same TU. A debug/verification aid
/// only; the dumped IR is pre-link, pre-EmbedBitcode, pre-backend.
llvm::cl::opt<std::string> clang::multitarget::MultiTargetCodeGenDumpDir(
    "multi-target-codegen-dump-dir", llvm::cl::Hidden, llvm::cl::init(""),
    llvm::cl::desc("Prototype: write each CodeGenModule's pre-backend IR "
                   "under this directory for comparison against separate "
                   "single-target compiles"));

/// Record the whole expanded token stream before parsing, then parse from the
/// recording rather than from the lexer. This is the vehicle a merged
/// multi-target stream needs: preprocessing runs once per target and splices
/// the recordings, so the parser must be able to run off a recording at all.
llvm::cl::opt<bool> clang::multitarget::RecordReplayTokens(
    "record-replay-tokens", llvm::cl::Hidden, llvm::cl::init(false),
    llvm::cl::desc("Prototype: parse from a recorded token stream rather than "
                   "directly from the lexer"));

/// Check the cheap tagging signal against structural equivalence before
/// merging anything on the strength of it.
llvm::cl::opt<bool> clang::multitarget::ReportVariantEquivalence(
    "report-variant-equivalence", llvm::cl::Hidden, llvm::cl::init(false),
    llvm::cl::desc("Prototype: report how many target-tagged declarations are "
                   "structurally the same"));

/// Print a fingerprint of the AST for one target, so a combined compilation
/// can be checked against separate per-target ones when byte-identical output
/// isn't available. -1 disables.
llvm::cl::opt<int> clang::multitarget::DumpDeclFingerprints(
    "dump-decl-fingerprints", llvm::cl::Hidden, llvm::cl::init(-1),
    llvm::cl::desc("Prototype: print one line per declaration of the given "
                   "target variant; 0 prints all of them"));

/// Dump a content hash per recorded token, so two targets' recordings can be
/// aligned outside the compiler. Identifier and literal text is hashed rather
/// than pointers, which are not comparable across processes.
llvm::cl::opt<bool> clang::multitarget::DumpTokenStream(
    "dump-token-stream", llvm::cl::Hidden, llvm::cl::init(false),
    llvm::cl::desc("Prototype: print a content hash for each recorded token"));

llvm::cl::opt<bool> clang::multitarget::VerifyIdenticalAlternatives(
    "verify-identical-alternatives", llvm::cl::Hidden, llvm::cl::init(false),
    llvm::cl::desc("Parse every alternative, and report an error where one "
                   "left to an earlier one with the same tokens means "
                   "something else"));

llvm::cl::opt<std::string> clang::multitarget::TraceTaggedDecl(
    "trace-tagged-decl", llvm::cl::Hidden,
    llvm::cl::desc("Prototype: report every declaration whose name contains "
                   "this string as it is claimed for a target"));

/// Dump the conditional-token-range map the recording builds, so the
/// boundaries a multi-target splice will use can be checked against the
/// source before anything is spliced.
llvm::cl::opt<bool> clang::multitarget::DumpConditionalRegions(
    "dump-conditional-regions", llvm::cl::Hidden, llvm::cl::init(false),
    llvm::cl::desc("Prototype: print the token range each #if branch "
                   "contributed to the recorded stream"));

llvm::cl::opt<bool> clang::multitarget::TraceReparse(
    "trace-reparse", llvm::cl::Hidden,
    llvm::cl::desc("Prototype: report each declaration that is re-parsed"));

llvm::cl::opt<std::string> clang::multitarget::MultiTargetAudit(
    "multi-target-audit", llvm::cl::Hidden, llvm::cl::init(""),
    llvm::cl::desc("Append to this file what each unit of a multi-target "
                   "compilation consumed that differs between targets"));

llvm::cl::opt<std::string> clang::multitarget::MultiTargetAuditJoins(
    "multi-target-audit-joins", llvm::cl::Hidden, llvm::cl::init(""),
    llvm::cl::desc("Append to this file, for each merge of targets' copies "
                   "of a declaration, whether the inputs its parse consumed "
                   "allowed it"));
