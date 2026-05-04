//===- MLIROptionsOptInfos.h - clv2 OptionInfo decls for MLIR ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// clv2 OptionInfo declarations for MLIR core library command-line flags.
//
// Compile-time option descriptors for MLIR library files (MLIRContext.cpp,
// AsmPrinter.cpp, PassManagerOptions.cpp, Timing.cpp, CLOptionsSetup.cpp,
// DebugCounter.cpp). Consumer code reads values via getMLIROptsReg().
//
// NOTE: Tool-specific options (mlir-opt, mlir-translate, JitRunner, etc.)
// are NOT included here. They will be migrated with their respective tools.
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_IR_MLIROPTIONSOPTINFOS_H
#define MLIR_IR_MLIROPTIONSOPTINFOS_H

#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"

#define CLV2_OPTIONS_DECL
#include "mlir/IR/MLIROptionsOptInfos.inc"
#undef CLV2_OPTIONS_DECL

namespace llvm::clv2 {
class OptionsContext;
}

namespace mlir::mlir_opts {

using MLIROptsRegOpts = decltype(llvm::clv2::MLIROptsReg)::ParsedOptionsT;

LLVM_ABI const MLIROptsRegOpts *
getMLIROptsReg(const llvm::clv2::OptionsContext &Ctx);

} // namespace mlir::mlir_opts

#endif // MLIR_IR_MLIROPTIONSOPTINFOS_H
