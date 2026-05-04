//===- bolt/Utils/CommandLineOpts.h - BOLT CLI options ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// BOLT CLI options
//
//===----------------------------------------------------------------------===//

#ifndef BOLT_UTILS_COMMAND_LINE_OPTS_H
#define BOLT_UTILS_COMMAND_LINE_OPTS_H

#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/OptionsContext.h"
#include <string>

namespace llvm {
namespace clv2 {
class OptionsContext;
}
namespace bolt {
class BinaryContext;
class BinaryFunction;
enum IndirectCallPromotionType : char;
enum JumpTableSupportLevel : char;
}
} // namespace llvm

namespace opts {

enum HeatmapModeKind {
  HM_None = 0,
  HM_Exclusive, // llvm-bolt-heatmap
  HM_Optional   // perf2bolt --heatmap
};

/// Strategy used to partition blocks into fragments.
enum SplitFunctionsStrategy : char {
  /// Split each function into a hot and cold fragment using profiling
  /// information.
  Profile2 = 0,
  /// Split each function into a hot, warm, and cold fragment using
  /// profiling information.
  CDSplit,
  /// Split each function into a hot and cold fragment at a randomly chosen
  /// split point (ignoring any available profiling information).
  Random2,
  /// Split each function into N fragments at a randomly chosen split points
  /// (ignoring any available profiling information).
  RandomN,
  /// Split all basic blocks of each function into fragments such that each
  /// fragment contains exactly a single basic block.
  All
};

// cl::OptionCategory objects.
extern llvm::cl::OptionCategory BoltCategory;
extern llvm::cl::OptionCategory BoltDiffCategory;
extern llvm::cl::OptionCategory BoltOptCategory;
extern llvm::cl::OptionCategory BoltRelocCategory;
extern llvm::cl::OptionCategory BoltOutputCategory;
extern llvm::cl::OptionCategory AggregatorCategory;
extern llvm::cl::OptionCategory BoltInstrCategory;
extern llvm::cl::OptionCategory HeatmapCategory;
extern llvm::cl::OptionCategory BinaryAnalysisCategory;

// The format to use with -o in aggregation mode (perf2bolt)
enum ProfileFormatKind { PF_Fdata, PF_YAML, PF_PreAgg, PF_PerfScript };

/// Return the verbosity level from the options context, falling back to 0.
unsigned getVerbosity(const llvm::bolt::BinaryContext &BC);

/// Bitmask representing a subset of possible gadget kinds.
enum GadgetKindBitmask : unsigned {
  /// Scan for unprotected backward control-flow (return instructions).
  GS_PTRAUTH_RETURN_TARGETS = (1 << 0),
  /// Scan for tail calls performed with untrusted link register.
  GS_PTRAUTH_TAIL_CALLS = (1 << 1),
  /// Scan for unprotected forward control-flow (branch and call instructions).
  GS_PTRAUTH_BRANCH_AND_CALL_TARGETS = (1 << 2),
  /// Scan for signing oracles.
  GS_PTRAUTH_SIGN_ORACLES = (1 << 3),
  /// Scan for authentication oracles.
  GS_PTRAUTH_AUTH_ORACLES = (1 << 4),

  /// Scan for all Pointer Authentication issues.
  GS_PTRAUTH_ALL_MASK = GS_PTRAUTH_RETURN_TARGETS | GS_PTRAUTH_TAIL_CALLS |
                        GS_PTRAUTH_BRANCH_AND_CALL_TARGETS |
                        GS_PTRAUTH_SIGN_ORACLES | GS_PTRAUTH_AUTH_ORACLES,

  /// Run all implemented scanners.
  GS_ALL_MASK = GS_PTRAUTH_ALL_MASK,
};

} // namespace opts

namespace llvm {
namespace bolt {
extern const char *BoltRevision;

/// Return true if we should process all functions in the binary.
bool processAllFunctions(const clv2::OptionsContext &Ctx);

/// Return true if we should dump dot graphs for the given function.
bool shouldDumpDot(const BinaryFunction &Function);
} // namespace bolt
} // namespace llvm

#endif
