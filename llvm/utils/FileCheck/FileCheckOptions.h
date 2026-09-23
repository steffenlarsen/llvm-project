//===- FileCheckOptions.h - FileCheck tool command-line options -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// llvm::FileCheckOptions: the plain C++ struct generated from
// FileCheckOptions.td by -gen-opt-parser-defs. See
// llvm/include/llvm/Option/OptParser.td and
// llvm/include/llvm/Option/LibraryOptions.h for the generation and runtime
// mechanism this relies on.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_FILECHECK_FILECHECKOPTIONS_H
#define LLVM_UTILS_FILECHECK_FILECHECKOPTIONS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <vector>

namespace llvm {
namespace opt {
class Arg;
class OptTable;
} // namespace opt
} // namespace llvm

#define OPTIONS_STRUCT_DECL
#include "FileCheckOptions.inc"
#undef OPTIONS_STRUCT_DECL

#endif // LLVM_UTILS_FILECHECK_FILECHECKOPTIONS_H
