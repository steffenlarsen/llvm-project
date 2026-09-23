//===- CodeGenPassOptionsCore1.h - Core1 command-line options ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// llvm::CodeGenCore1Options: the plain C++ struct generated from
// CodeGenPassOptionsCore1.td by -gen-opt-parser-defs. See
// llvm/include/llvm/Option/OptParser.td and
// llvm/include/llvm/Option/LibraryOptions.h for the generation and runtime
// mechanism this relies on.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_CODEGENPASSOPTIONSCORE1_H
#define LLVM_CODEGEN_CODEGENPASSOPTIONSCORE1_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include <climits>
#include <optional>
#include <string>
#include <vector>

namespace llvm {
namespace opt {
class Arg;
class OptTable;
} // namespace opt
} // namespace llvm

#define OPTIONS_STRUCT_DECL
#include "llvm/CodeGen/CodeGenPassOptionsCore1.inc"
#undef OPTIONS_STRUCT_DECL

#endif // LLVM_CODEGEN_CODEGENPASSOPTIONSCORE1_H
