//===- UtilsOptions.h - Transforms/Utils options --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// llvm::UtilsOptions: the plain C++ struct generated from UtilsOptions.td by
// -gen-opt-parser-defs. See llvm/include/llvm/Option/OptParser.td and
// llvm/include/llvm/Option/LibraryOptions.h for the generation and runtime
// mechanism this relies on.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_UTILSOPTIONS_H
#define LLVM_TRANSFORMS_UTILS_UTILSOPTIONS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace llvm {
namespace opt {
class Arg;
class OptTable;
} // namespace opt

// Self-generated enum with no pre-existing home elsewhere: hand-declared
// here (rather than left to the OptionEnum emitter) so that
// OptimizeExistingHotColdNewKindEnum can be marked External in
// UtilsOptions.td, letting two EnumMembers ("always" and the empty
// ValueOptional spelling) share this single "Always" enumerator without the
// emitter trying to declare "Always" twice. See UtilsOptions.td for detail.
enum class OptimizeExistingHotColdNewKind { None, Cold, Always };

} // namespace llvm

#define OPTIONS_STRUCT_DECL
#include "llvm/Transforms/Utils/UtilsOptions.inc"
#undef OPTIONS_STRUCT_DECL

#endif // LLVM_TRANSFORMS_UTILS_UTILSOPTIONS_H
