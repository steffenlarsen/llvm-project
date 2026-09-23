//===-- FuzzerCLI.h - Common logic for CLIs of fuzzers ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Common logic needed to implement LLVM's fuzz targets' CLIs - including LLVM
// concepts like cl::opt and libFuzzer concepts like -ignore_remaining_args=1.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZMUTATE_FUZZERCLI_H
#define LLVM_FUZZMUTATE_FUZZERCLI_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"
#include <memory>
#include <stddef.h>
#include <vector>

namespace llvm {

class StringRef;
namespace clv2 {
class OptionParser;
class OptionsContext;
} // namespace clv2

/// Parse cl::opts from a fuzz target commandline.
///
/// This handles all arguments after -ignore_remaining_args=1 as cl::opts.
LLVM_ABI std::unique_ptr<clv2::OptionsContext> parseFuzzerCLOpts(int ArgC,
                                                                 char *ArgV[]);

/// Overload that parses into a caller-provided OptionParser.
/// The caller can pre-configure P with tool-specific registries before calling.
/// RegisterAllLLVMOptions is called automatically.
LLVM_ABI std::unique_ptr<clv2::OptionsContext>
parseFuzzerCLOpts(int ArgC, char *ArgV[], clv2::OptionParser &P);

/// Overload that parses a caller-supplied argument list instead of computing
/// one from ArgC/ArgV. Args[0] is treated as the program name. Useful when
/// the caller needs to pre-parse some tokens (e.g. through a migrated
/// llvm::opt::parseLibraryOptionsChain<...>() struct) out of the result of
/// getFuzzerCLArgs() before handing the rest to this OptionParser.
LLVM_ABI std::unique_ptr<clv2::OptionsContext>
parseFuzzerCLOpts(ArrayRef<const char *> Args, clv2::OptionParser &P);

/// Returns the merged command-line arguments a fuzz target should parse:
/// any options injected via handleExecNameEncodedBEOpts /
/// handleExecNameEncodedOptimizerOpts, followed by everything in ArgV after
/// -ignore_remaining_args=1 (or all of ArgV if that marker isn't present).
/// ArgV[0] is preserved as the first element. This is the argument list
/// parseFuzzerCLOpts(ArgC, ArgV, P) parses; exposed separately so callers can
/// pre-parse some of it (see the ArrayRef overload above) before the rest is
/// handed to a clv2::OptionParser.
LLVM_ABI std::vector<const char *> getFuzzerCLArgs(int ArgC, char *ArgV[]);

/// Handle backend options that are encoded in the executable name.
///
/// Parses some common backend options out of a specially crafted executable
/// name (argv[0]). For example, a name like llvm-foo-fuzzer--aarch64-gisel
/// might set up an AArch64 triple and the Global ISel selector. This should be
/// called *before* parseFuzzerCLOpts if calling both.
///
/// This is meant to be used for environments like OSS-Fuzz that aren't capable
/// of passing in command line arguments in the normal way.
LLVM_ABI void handleExecNameEncodedBEOpts(StringRef ExecName);

/// Handle optimizer options which are encoded in the executable name.
/// Same semantics as in 'handleExecNameEncodedBEOpts'.
LLVM_ABI void handleExecNameEncodedOptimizerOpts(StringRef ExecName);

using FuzzerTestFun = int (*)(const uint8_t *Data, size_t Size);
using FuzzerInitFun = int (*)(int *argc, char ***argv);

/// Runs a fuzz target on the inputs specified on the command line.
///
/// Useful for testing fuzz targets without linking to libFuzzer. Finds inputs
/// in the argument list in a libFuzzer compatible way.
LLVM_ABI int runFuzzerOnInputs(
    int ArgC, char *ArgV[], FuzzerTestFun TestOne,
    FuzzerInitFun Init = [](int *, char ***) { return 0; });

} // namespace llvm

#endif // LLVM_FUZZMUTATE_FUZZERCLI_H
