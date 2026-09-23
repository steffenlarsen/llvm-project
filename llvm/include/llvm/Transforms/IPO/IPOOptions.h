//===- IPOOptions.h - IPO options ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// llvm::IPOOptions: the plain C++ struct generated from IPOOptions.td by
// -gen-opt-parser-defs. See llvm/include/llvm/Option/OptParser.td and
// llvm/include/llvm/Option/LibraryOptions.h for the generation and runtime
// mechanism this relies on.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_IPOOPTIONS_H
#define LLVM_TRANSFORMS_IPO_IPOOPTIONS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/ReplayInlinerEnums.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/ExpandVariadicsMode.h"

#include <climits>
#include <cstdint>
#include <optional>

namespace llvm {
namespace opt {
class Arg;
class OptTable;
} // namespace opt
} // namespace llvm

#define OPTIONS_STRUCT_DECL
#include "llvm/Transforms/IPO/IPOOptions.inc"
#undef OPTIONS_STRUCT_DECL

#endif // LLVM_TRANSFORMS_IPO_IPOOPTIONS_H
