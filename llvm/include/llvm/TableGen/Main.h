//===- llvm/TableGen/Main.h - tblgen entry point ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the common entry point for tblgen tools.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TABLEGEN_MAIN_H
#define LLVM_TABLEGEN_MAIN_H

#include "llvm/ADT/StringRef.h"
#include <map>
#include <string>

namespace llvm {

class raw_ostream;
class RecordKeeper;
namespace clv2 {
class OptionParser;
}

struct TableGenOutputFiles {
  std::string MainFile;

  // Translates additional output file names to their contents.
  std::map<StringRef, std::string> AdditionalFiles;
};

/// Returns true on error, false otherwise.
using TableGenMainFn =
    function_ref<bool(raw_ostream &OS, const RecordKeeper &Records)>;

/// Perform the action using Records, and store output in OutFiles.
/// Returns true on error, false otherwise.
using MultiFileTableGenMainFn = function_ref<bool(TableGenOutputFiles &OutFiles,
                                                  const RecordKeeper &Records)>;

LLVM_ABI int TableGenMain(const char *argv0, TableGenMainFn MainFn = nullptr);

LLVM_ABI int TableGenMain(const char *argv0,
                          MultiFileTableGenMainFn MainFn = nullptr);

void registerTableGenMainOptions(clv2::OptionParser &P);

/// Controls emitting large character arrays as strings or character arrays.
/// Typically set to false when building with MSVC.
extern bool EmitLongStrLiterals;

} // end namespace llvm

#endif // LLVM_TABLEGEN_MAIN_H
