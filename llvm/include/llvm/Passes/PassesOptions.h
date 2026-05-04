//===- PassesOptions.h - LLVMPasses option bridge API ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Bridge API for LLVMPasses library options.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PASSES_PASSESOPTIONS_H
#define LLVM_PASSES_PASSESOPTIONS_H

#include "llvm/Passes/PassesOptionsOptInfos.h"
#include "llvm/Support/Compiler.h"

namespace llvm::passes {

/// The parsed-options view type for the Passes library registry.
using ParsedOpts = decltype(clv2::PassesOptsReg)::ParsedOptionsT;

} // namespace llvm::passes

#endif // LLVM_PASSES_PASSESOPTIONS_H
