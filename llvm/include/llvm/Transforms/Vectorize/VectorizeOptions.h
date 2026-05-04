//===- VectorizeOptions.h - Vectorize option bridge API ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Bridge API for Vectorize library options. clv2-migrated tools pass the
// parsed view for VectorizeOptsReg to applyVectorizeOptions().
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_VECTORIZEOPTIONS_H
#define LLVM_TRANSFORMS_VECTORIZE_VECTORIZEOPTIONS_H

#include "llvm/Support/Compiler.h"
#include "llvm/Transforms/Vectorize/VectorizeOptionsOptInfos.h"

namespace llvm {
class Function;
class LLVMContext;
class Module;
} // namespace llvm

namespace llvm::vec_opts {

/// The parsed-options view type for the Vectorize library registry.
using ParsedOpts = decltype(clv2::VectorizeOptsReg)::ParsedOptionsT;

} // namespace llvm::vec_opts

#endif // LLVM_TRANSFORMS_VECTORIZE_VECTORIZEOPTIONS_H
