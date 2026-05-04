//===- SSAFLinker.cpp - SSAF Linker ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the SSAF entity linker tool. Its default behavior is to
//  link N inputs (TU summaries, static libraries, and multi-arch static
//  libraries) into one LU summary via the EntityLinker framework. It also
//  provides the `static-library` subcommand for bundling TU summaries into a
//  StaticLibrary, and the `multi-arch` subcommand for bundling StaticLibrary
//  and SharedLibrary members (or existing multi-arch bundles) into
//  MultiArchStaticLibrary or MultiArchSharedLibrary.
//
//===----------------------------------------------------------------------===//

#include "LinkCLI.h"
#include "MultiArchCreateCLI.h"
#include "StaticLibraryCreateCLI.h"

#include "clang/ScalableStaticAnalysis/SSAFForceLinker.h" // IWYU pragma: keep
#include "clang/ScalableStaticAnalysis/Tool/Utils.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include <string>

using namespace llvm;
using namespace clang::ssaf;

namespace {

//===----------------------------------------------------------------------===//
// Command-Line Options
//===----------------------------------------------------------------------===//

clv2::OptionCategory SsafLinkerCategory("clang-ssaf-linker options");

// clang-ssaf-linker uses initTool() which owns the parse.

//--- Top-level (default) `link` action options ---

inline constexpr clv2::ListOptionInfo<std::string> SLInputPathsOpt{
    "", "<input files>", clv2::Positional{}, clv2::OneOrMore,
    clv2::cat(SsafLinkerCategory)};

inline constexpr clv2::OptionInfo<std::string> SLOutputPathOpt{
    "o", "Output file path", clv2::Required, clv2::value_desc("path"),
    clv2::cat(SsafLinkerCategory)};

inline constexpr clv2::OptionInfo<std::string> SLTargetTripleOpt{
    "target-triple",
    "Target triple of the link unit (defaults to the first input's; required "
    "when the first input is a multi-arch static library with several members)",
    clv2::value_desc("triple"), clv2::cat(SsafLinkerCategory)};

// --verbose and --time apply to every subcommand.
inline constexpr clv2::OptionInfo<bool> SLVerboseOpt{
    "verbose", "Enable verbose output", clv2::cat(SsafLinkerCategory)};

inline constexpr clv2::OptionInfo<bool> SLTimeOpt{
    "time", "Enable timing", clv2::cat(SsafLinkerCategory)};

//--- `static-library` subcommand options ---

inline constexpr clv2::OptionInfo<std::string> SLStaticLibVerbOpt{
    "",
    "<verb>",
    clv2::Positional{},
    clv2::Required,
    clv2::value_desc("create"),
    clv2::cat(SsafLinkerCategory)};

inline constexpr clv2::ListOptionInfo<std::string> SLStaticLibInputsOpt{
    "", "<TU summary files>", clv2::Positional{},
    clv2::cat(SsafLinkerCategory)};

inline constexpr clv2::OptionInfo<std::string> SLStaticLibOutputOpt{
    "o", "Output file path", clv2::Required, clv2::value_desc("path"),
    clv2::cat(SsafLinkerCategory)};

inline constexpr clv2::OptionInfo<std::string> SLStaticLibNamespaceOpt{
    "namespace",
    "Namespace name for the StaticLibrary (defaults to output file stem)",
    clv2::value_desc("name"), clv2::cat(SsafLinkerCategory)};

inline constexpr clv2::OptionInfo<std::string> SLStaticLibTripleOpt{
    "target-triple",
    "Target triple (defaults to inputs' triple; must match all inputs when "
    "set)",
    clv2::value_desc("triple"), clv2::cat(SsafLinkerCategory)};

// The `static-library` subcommand groups the StaticLibrary operations.
inline constexpr clv2::SubCommandInfo<
    &SLStaticLibVerbOpt, &SLStaticLibInputsOpt, &SLStaticLibOutputOpt,
    &SLStaticLibNamespaceOpt, &SLStaticLibTripleOpt, &SLVerboseOpt, &SLTimeOpt>
    StaticLibraryCmdInfo{"static-library", "Operations on StaticLibraries"};

//--- `multi-arch` subcommand options ---

// The verb positional is declared BEFORE the input list so that argv[0] under
// the subcommand binds to the verb rather than to the greedy input list.
inline constexpr clv2::OptionInfo<std::string> SLMultiArchVerbOpt{
    "",
    "<verb>",
    clv2::Positional{},
    clv2::Required,
    clv2::value_desc("create"),
    clv2::cat(SsafLinkerCategory)};

// The action-specific positional input list, currently consumed by
// `multi-arch create`.
inline constexpr clv2::ListOptionInfo<std::string> SLMultiArchInputsOpt{
    "", "<static-library or shared-library files>", clv2::Positional{},
    clv2::cat(SsafLinkerCategory)};

inline constexpr clv2::OptionInfo<std::string> SLMultiArchOutputOpt{
    "o", "Output file path", clv2::Required, clv2::value_desc("path"),
    clv2::cat(SsafLinkerCategory)};

// The `multi-arch` subcommand groups all multi-architecture operations.
inline constexpr clv2::SubCommandInfo<
    &SLMultiArchVerbOpt, &SLMultiArchInputsOpt, &SLMultiArchOutputOpt,
    &SLVerboseOpt, &SLTimeOpt>
    MultiArchCmdInfo{"multi-arch",
                     "Operations on multi-architecture StaticLibrary and "
                     "SharedLibrary artifacts"};

//--- Top-level registry (includes the subcommands) ---

inline constexpr clv2::OptionsRegistry<
    &SLInputPathsOpt, &SLOutputPathOpt, &SLTargetTripleOpt, &SLVerboseOpt,
    &SLTimeOpt, &StaticLibraryCmdInfo, &MultiArchCmdInfo>
    SsafLinkerOptsReg;

// Parsed option values, threaded through main() to the run* dispatchers.
struct SsafLinkerOptions {
  std::vector<std::string> InputPaths;
  std::string OutputPath;
  std::string TargetTriple;
  bool Verbose = false;
  bool Time = false;

  // StaticLibrary subcommand state.
  bool StaticLibraryCmd = false;
  std::string StaticLibraryVerb;
  std::vector<std::string> StaticLibraryInputs;
  std::string StaticLibraryOutput;
  std::string StaticLibraryNamespace;
  std::string StaticLibraryTriple;

  // MultiArch subcommand state.
  bool MultiArchCmd = false;
  std::string MultiArchVerb;
  std::vector<std::string> MultiArchInputs;
  std::string MultiArchOutput;
};

} // namespace

static void
applySsafLinkerOpts(const decltype(SsafLinkerOptsReg)::ParsedOptionsT &Opts,
                    SsafLinkerOptions &ToolOpts) {
  // Top-level options (only valid when no subcommand is active).
  ToolOpts.InputPaths = Opts.get<&SLInputPathsOpt>();
  ToolOpts.OutputPath = Opts.get<&SLOutputPathOpt>();
  ToolOpts.TargetTriple = Opts.get<&SLTargetTripleOpt>();
  ToolOpts.Verbose = Opts.get<&SLVerboseOpt>();
  ToolOpts.Time = Opts.get<&SLTimeOpt>();

  // StaticLibrary subcommand.
  if (Opts.isActive<&StaticLibraryCmdInfo>()) {
    ToolOpts.StaticLibraryCmd = true;
    const auto &Sub = Opts.getSubOptions<&StaticLibraryCmdInfo>();
    ToolOpts.StaticLibraryVerb = Sub.get<&SLStaticLibVerbOpt>();
    ToolOpts.StaticLibraryInputs = Sub.get<&SLStaticLibInputsOpt>();
    ToolOpts.StaticLibraryOutput = Sub.get<&SLStaticLibOutputOpt>();
    ToolOpts.StaticLibraryNamespace = Sub.get<&SLStaticLibNamespaceOpt>();
    ToolOpts.StaticLibraryTriple = Sub.get<&SLStaticLibTripleOpt>();
    // --verbose/--time are shared across all subcommands.
    ToolOpts.Verbose = Sub.get<&SLVerboseOpt>();
    ToolOpts.Time = Sub.get<&SLTimeOpt>();
  }

  // MultiArch subcommand.
  if (Opts.isActive<&MultiArchCmdInfo>()) {
    ToolOpts.MultiArchCmd = true;
    const auto &Sub = Opts.getSubOptions<&MultiArchCmdInfo>();
    ToolOpts.MultiArchVerb = Sub.get<&SLMultiArchVerbOpt>();
    ToolOpts.MultiArchInputs = Sub.get<&SLMultiArchInputsOpt>();
    ToolOpts.MultiArchOutput = Sub.get<&SLMultiArchOutputOpt>();
    // --verbose/--time are shared across all subcommands.
    ToolOpts.Verbose = Sub.get<&SLVerboseOpt>();
    ToolOpts.Time = Sub.get<&SLTimeOpt>();
  }
}

namespace {

//===----------------------------------------------------------------------===//
// MultiArch Verbs
//===----------------------------------------------------------------------===//

// Verb strings for the `multi-arch` subcommand. Kept in sync with
// UnknownMultiArchVerb below.
constexpr const char *MultiArchCreateVerb = "create";

//===----------------------------------------------------------------------===//
// Error Messages
//===----------------------------------------------------------------------===//

namespace LocalErrorMessages {

constexpr const char *UnknownStaticLibraryVerb =
    "unknown static-library verb '{0}': expected 'create'";

constexpr const char *UnknownMultiArchVerb =
    "unknown multi-arch verb '{0}': expected 'create'";

} // namespace LocalErrorMessages

//===----------------------------------------------------------------------===//
// StaticLibrary Verbs
//===----------------------------------------------------------------------===//

// Verb strings for the `static-library` subcommand. Kept in sync with
// UnknownStaticLibraryVerb above.
constexpr const char *StaticLibraryCreateVerb = "create";

//===----------------------------------------------------------------------===//
// Diagnostic Utilities
//===----------------------------------------------------------------------===//

void runLink(llvm::TimerGroup &TG, const SsafLinkerOptions &ToolOpts) {
  LinkCLI LC;
  LC.run(TG, ToolOpts.InputPaths, ToolOpts.OutputPath, ToolOpts.TargetTriple,
         ToolOpts.Verbose, ToolOpts.Time);
}

//===----------------------------------------------------------------------===//
// static-library subcommand dispatch
//===----------------------------------------------------------------------===//

void runStaticLibrary(llvm::TimerGroup &TG, const SsafLinkerOptions &ToolOpts) {
  if (ToolOpts.StaticLibraryVerb == StaticLibraryCreateVerb) {
    StaticLibraryCreateCLI::Config Cfg;
    Cfg.InputPaths = ToolOpts.StaticLibraryInputs;
    Cfg.OutputPath = ToolOpts.StaticLibraryOutput;
    Cfg.Namespace = ToolOpts.StaticLibraryNamespace;
    Cfg.TargetTriple = ToolOpts.StaticLibraryTriple;
    Cfg.Verbose = ToolOpts.Verbose;
    Cfg.Time = ToolOpts.Time;

    StaticLibraryCreateCLI SLC;
    SLC.run(TG, Cfg);
    return;
  }
  fail(LocalErrorMessages::UnknownStaticLibraryVerb,
       ToolOpts.StaticLibraryVerb);
}

//===----------------------------------------------------------------------===//
// multi-arch subcommand dispatch
//===----------------------------------------------------------------------===//

void runMultiArch(llvm::TimerGroup &TG, const SsafLinkerOptions &ToolOpts) {
  if (ToolOpts.MultiArchVerb == MultiArchCreateVerb) {
    MultiArchCreateCLI MAC;
    MAC.run(TG, ToolOpts.MultiArchInputs, ToolOpts.MultiArchOutput,
            ToolOpts.Verbose, ToolOpts.Time);
    return;
  }
  fail(LocalErrorMessages::UnknownMultiArchVerb, ToolOpts.MultiArchVerb);
}

} // namespace

//===----------------------------------------------------------------------===//
// Driver
//===----------------------------------------------------------------------===//

int main(int argc, const char **argv) {
  llvm::StringRef ToolHeading = "SSAF Linker";

  InitLLVM X(argc, argv);
  // No bridge is registered here: a bridge is a non-capturing function
  // pointer, but registering the subcommands only via P.add<&Reg>() (rather
  // than the manual addDynamicEntry() path) is what makes their
  // SubCommandSpecs reach the parser. The parsed values are instead read back
  // from the returned OptionsContext below.
  std::unique_ptr<clv2::OptionsContext> OptsCtx =
      initTool(argc, argv, "0.1", SsafLinkerCategory, ToolHeading,
               [](clv2::OptionParser &P) { P.add<&SsafLinkerOptsReg>(); });

  SsafLinkerOptions ToolOpts;
  applySsafLinkerOpts(*OptsCtx->getViewPtr<&SsafLinkerOptsReg>(), ToolOpts);

  llvm::TimerGroup Timers(getToolName(), ToolHeading);

  if (ToolOpts.StaticLibraryCmd) {
    runStaticLibrary(Timers, ToolOpts);
  } else if (ToolOpts.MultiArchCmd) {
    runMultiArch(Timers, ToolOpts);
  } else {
    // Default (no subcommand): run the linker pipeline.
    runLink(Timers, ToolOpts);
  }

  return 0;
}
