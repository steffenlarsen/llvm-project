//===- SupportOptions.h - LLVMSupport option bridge API ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Bridge API for LLVMSupport library options. clv2-migrated tools pass the
// parsed view for SupportOptsReg to applySupportOptions().
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SUPPORTOPTIONS_H
#define LLVM_SUPPORT_SUPPORTOPTIONS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/SupportOptionsOptInfos.h"
#include <cstdint>
#include <string>

namespace llvm {
class LLVMContext;
}
namespace llvm::clv2 {
class OptionsContext;
}

namespace llvm::support {

/// The parsed-options view type for the Support library registry.
using ParsedOpts = decltype(clv2::SupportOptsReg)::ParsedOptionsT;

/// Apply parsed Support options to the library's internal state.
/// Call once after clv2 parse, before any Support getter is invoked.
LLVM_ABI void applySupportOptions(const ParsedOpts &Opts);

LLVM_ABI extern bool StatsEnabled;
LLVM_ABI extern bool StatsAsJsonEnabled;
LLVM_ABI extern unsigned DebugBufferSizeVal;
LLVM_ABI extern bool ViewBackgroundFlag;
/// Directory for -dot-file-location, or empty for a temporary file.  A
/// StringRef rather than a std::string: llvm/lib/Support is built with
/// -Werror=global-constructors, which a namespace-scope std::string violates.
/// applySupportOptions() owns the characters.
LLVM_ABI extern StringRef DagFileLocation;
LLVM_ABI extern bool NoOpenDagViewer;

} // namespace llvm::support

#endif // LLVM_SUPPORT_SUPPORTOPTIONS_H
