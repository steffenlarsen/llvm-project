//===- WebAssemblyOptions.h - WebAssembly options --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_WEBASSEMBLY_WEBASSEMBLYOPTIONS_H
#define LLVM_TARGET_WEBASSEMBLY_WEBASSEMBLYOPTIONS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
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
#include "llvm/Target/WebAssembly/WebAssemblyOptions.inc"
#undef OPTIONS_STRUCT_DECL

#endif // LLVM_TARGET_WEBASSEMBLY_WEBASSEMBLYOPTIONS_H
