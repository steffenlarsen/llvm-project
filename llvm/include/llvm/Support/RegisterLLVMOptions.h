//===- llvm/Support/RegisterLLVMOptions.h - Register library opts -*-
// C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Register all LLVM library option registries on an OptionParser.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_REGISTERLLVMOPTIONS_H
#define LLVM_SUPPORT_REGISTERLLVMOPTIONS_H

#include "llvm/Support/Compiler.h"

namespace llvm {
namespace clv2 {
class OptionParser;
}

/// Register core LLVM library option registries (Support, IR, Remarks, Passes,
/// Bitcode) on an OptionParser. Use for tools that only need basic LLVM options
/// without codegen, targets, or transforms.
LLVM_ABI void RegisterCoreLLVMOptions(clv2::OptionParser &P);

/// Register common LLVM library option registries on an OptionParser.
/// Includes Core + Analysis, Transforms, CodeGen passes, MC, ProfileData,
/// LTO, CGData — everything except target backends and AsmParser.
/// Also enables global dynamic entries (CostModel, DwarfDebug, etc.).
LLVM_ABI void RegisterCommonLLVMOptions(clv2::OptionParser &P);

/// Like RegisterCommonLLVMOptions but marks all non-Core options as Hidden.
/// Tools then use P.showOptions({...}) to reveal exactly the options they
/// need.
LLVM_ABI void RegisterCommonLLVMOptionsHidden(clv2::OptionParser &P);

/// Register all LLVM library option registries on an OptionParser.
LLVM_ABI void RegisterAllLLVMOptions(clv2::OptionParser &P);

} // namespace llvm

#endif // LLVM_SUPPORT_REGISTERLLVMOPTIONS_H
