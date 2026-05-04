//===- TableGen.h ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Shared entry point for llvm-tblgen and llvm-min-tblgen.
//
//===----------------------------------------------------------------------===//

namespace llvm {
namespace clv2 {
class OptionParser;
}
} // namespace llvm

int tblgen_main(int argc, char **argv);

/// Hook for registering backend-specific options before command-line parsing.
/// llvm-tblgen defines this to register all full-backend options;
/// llvm-min-tblgen defines this as a no-op.
void registerExtraTblgenOptions(llvm::clv2::OptionParser &P);
