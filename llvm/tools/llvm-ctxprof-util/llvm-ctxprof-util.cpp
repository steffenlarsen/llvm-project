//===--- llvm-ctxprof-util - utilities for ctxprof --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
///
/// Utilities for manipulating contextual profiles
///
//===----------------------------------------------------------------------===//

#include "llvm/IR/GlobalValue.h"
#include "llvm/ProfileData/PGOCtxProfReader.h"
#include "llvm/ProfileData/PGOCtxProfWriter.h"
#include "llvm/ProfileData/ProfileDataOptionsOptInfos.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::clv2;

inline constexpr OptionInfo<std::string> InputFilenameOpt{
    "input",
    "Input file. The format is an array of contexts.\n"
    "Each context is a dictionary with the following keys:\n"
    "'Guid', mandatory. The value is a 64-bit integer.\n"
    "'Counters', mandatory. An array of 32-bit ints. These are the "
    "counter values.\n"
    "'Contexts', optional. An array containing arrays of contexts. The "
    "context array at a position 'i' is the set of callees at that "
    "callsite index. Use an empty array to indicate no callees.",
    value_desc("input"), Init{"-"}};

inline constexpr OptionInfo<std::string> OutputFilenameOpt{
    "output", "Output file", value_desc("output"), Init{"-"}};

inline constexpr SubCommandInfo<&InputFilenameOpt, &OutputFilenameOpt>
    FromYAMLCmd{"fromYAML", "Convert from yaml"};

inline constexpr SubCommandInfo<&InputFilenameOpt, &OutputFilenameOpt>
    ToYAMLCmd{"toYAML", "Convert to yaml"};

inline constexpr OptionsRegistry<&FromYAMLCmd, &ToYAMLCmd> CtxProfToolReg;

namespace {
// Save the bitstream profile from the JSON representation.
Error convertFromYaml(StringRef Input, StringRef Output) {
  auto BufOrError = MemoryBuffer::getFileOrSTDIN(Input, /*IsText=*/true);
  if (!BufOrError)
    return createFileError(Input, BufOrError.getError());

  std::error_code EC;
  // Using a fd_ostream instead of a fd_stream. The latter would be more
  // efficient as the bitstream writer supports incremental flush to it, but the
  // json scenario is for test, and file size scalability doesn't really concern
  // us.
  raw_fd_ostream Out(Output, EC);
  if (EC)
    return createStringError(EC, "failed to open output");

  return llvm::createCtxProfFromYAML(BufOrError.get()->getBuffer(), Out);
}

Error convertToYaml(StringRef Input, StringRef Output) {
  auto BufOrError = MemoryBuffer::getFileOrSTDIN(Input);
  if (!BufOrError)
    return createFileError(Input, BufOrError.getError());

  std::error_code EC;
  raw_fd_ostream Out(Output, EC);
  if (EC)
    return createStringError(EC, "failed to open output");
  PGOCtxProfileReader Reader(BufOrError.get()->getBuffer());
  auto Prof = Reader.loadProfiles();
  if (!Prof)
    return Prof.takeError();
  llvm::convertCtxProfToYaml(Out, *Prof);
  Out << "\n";
  return Error::success();
}
} // namespace

int main(int argc, const char **argv) {
  clv2::OptionParser P;
  P.add<&CtxProfToolReg>();
  P.add<&clv2::ProfileDataOptsReg>();
  RegisterCoreLLVMOptions(P);
  P.showOptions({"disable-auto-upgrade-debug-info", "disable-i2p-p2i-opt",
                 "elide-all-zero-branch-weights"});
  {
    // Visible versions of options that are Hidden in ProfileDataOptsReg
    static constexpr clv2::OptionInfo<bool> V4{
        "enable-name-compression", "Enable name/filename string compression",
        clv2::Init{true}};
    static constexpr clv2::OptionInfo<bool> V5{
        "enable-vtable-profile-use",
        "If ThinLTO and WPD is enabled and this option is true, vtable "
        "profiles will be used by ICP pass for more efficient indirect "
        "call sequence. If false, type profiles won't be used.",
        clv2::Init{false}};
    static constexpr clv2::OptionInfo<bool> V6{
        "enable-vtable-value-profiling",
        "If true, the virtual table address will be instrumented to know "
        "the types of a C++ pointer. The information is used in indirect "
        "call promotion to do selective vtable-based comparison.",
        clv2::Init{false}};
    static constexpr clv2::OptionInfo<bool> V7{
        "generate-merged-base-profiles",
        "When generating nested context-sensitive profiles, always generate "
        "extra base profile for function with all its context profiles merged "
        "into it.",
        clv2::Init{false}};
    static constexpr clv2::OptionInfo<bool> V8{
        "ctx-prof-include-empty",
        "Also write profiles with all-zero counters. Intended for "
        "testing/debugging.",
        clv2::Init{false}};
    static constexpr clv2::OptionsRegistry<&V4, &V5, &V6, &V7, &V8> VisReg;
    using PT = decltype(VisReg)::ParsedOptionsT;
    auto *S = new PT();
    decltype(VisReg)::applyDefaultsTo(*S);
    std::vector<clv2::detail::OptionEntry> Es;
    std::vector<clv2::detail::AliasEntry> As;
    std::vector<clv2::detail::SubCommandSpec> Ss;
    decltype(VisReg)::staticBuildInto(*S, Es, As, Ss);
    for (auto &E : Es)
      P.addDynamicEntry(std::move(E));
  }
  auto OptsCtx = P.parse(argc, argv, "LLVM Contextual Profile Utils\n");
  auto *Opts = OptsCtx->getViewPtr<&CtxProfToolReg>();

  auto HandleErr = [&](Error E) -> int {
    if (E) {
      handleAllErrors(std::move(E), [&](const ErrorInfoBase &E) {
        E.log(errs());
        errs() << "\n";
      });
      return 1;
    }
    return 0;
  };

  if (Opts->isActive<&FromYAMLCmd>()) {
    auto &Sub = Opts->getSubOptions<&FromYAMLCmd>();
    return HandleErr(convertFromYaml(Sub.get<&InputFilenameOpt>(),
                                     Sub.get<&OutputFilenameOpt>()));
  }

  if (Opts->isActive<&ToYAMLCmd>()) {
    auto &Sub = Opts->getSubOptions<&ToYAMLCmd>();
    return HandleErr(convertToYaml(Sub.get<&InputFilenameOpt>(),
                                   Sub.get<&OutputFilenameOpt>()));
  }

  errs() << "No subcommand specified. Use --help for usage.\n";
  return 1;
}
