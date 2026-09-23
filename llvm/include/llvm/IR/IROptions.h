//===- IROptions.h - IR options -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// llvm::IROptions: the plain C++ struct generated from IROptions.td by
// -gen-opt-parser-defs. See llvm/include/llvm/Option/OptParser.td and
// llvm/include/llvm/Option/LibraryOptions.h for the generation and runtime
// mechanism this relies on.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_IROPTIONS_H
#define LLVM_IR_IROPTIONS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/PrintPasses.h"
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
#include "llvm/IR/IROptions.inc"
#undef OPTIONS_STRUCT_DECL

namespace llvm::ir_opts {

// Pushes the CLI-parsed IROptions::Current values that back cross-cutting,
// non-struct global state into that state: llvm::TimePassesIsEnabled /
// llvm::TimePassesPerRun (see PassTimingInfo.h -- also written to directly by
// callers such as clang's CodeGenAction, independent of any CLI option) and
// the global OptBisect singleton (see OptBisect.h). Unlike a plain getter,
// this has to run once, explicitly, after a tool's
// parseLibraryOptionsChain<..., IROptions, ...>() call populates
// IROptions::Current -- there is no automatic post-parse hook in the new
// per-library OptTable design (contrast with clv2's Registry::apply, which
// ran automatically). Call this from every tool that wires IROptions into
// its chain and wants -time-passes/-opt-bisect*/-opt-disable to take effect.
LLVM_ABI void applyIROptions();

} // namespace llvm::ir_opts

#endif // LLVM_IR_IROPTIONS_H
