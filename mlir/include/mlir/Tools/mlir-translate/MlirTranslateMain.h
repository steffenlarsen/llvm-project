//===- MlirTranslateMain.h - MLIR Translation Driver main -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Main entry function for mlir-translate for when built as standalone binary.
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_TOOLS_MLIRTRANSLATE_MLIRTRANSLATEMAIN_H
#define MLIR_TOOLS_MLIRTRANSLATE_MLIRTRANSLATEMAIN_H

#include "mlir/Support/LLVM.h"
#include "llvm/ADT/StringRef.h"

#include <functional>

namespace llvm::clv2 {
class OptionParser;
} // namespace llvm::clv2

namespace mlir {
/// Translate to/from an MLIR module from/to an external representation (e.g.
/// LLVM IR, SPIRV binary, ...). This is the entry point for the implementation
/// of tools like `mlir-translate`. The translation to perform is parsed from
/// the command line. The `toolName` argument is used for the header displayed
/// by `--help`.
/// If \p ConfigureParser is provided, it is called with the OptionParser
/// before parsing to allow callers to add tool-specific registries.
LogicalResult mlirTranslateMain(
    int argc, char **argv, StringRef toolName,
    std::function<void(llvm::clv2::OptionParser &)> ConfigureParser = {});
} // namespace mlir

#endif // MLIR_TOOLS_MLIRTRANSLATE_MLIRTRANSLATEMAIN_H
