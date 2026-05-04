//===- LLDBTableGen.cpp - Top-Level TableGen implementation for LLDB ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the main function for LLDB's TableGen.
//
//===----------------------------------------------------------------------===//

#include "LLDBTableGenBackends.h" // Declares all backends.
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Main.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"

using namespace llvm;
using namespace lldb_private;

static void printRecords(const RecordKeeper &Records, raw_ostream &OS) {
  OS << Records;
}

static TableGen::Emitter::Opt LLDBOpts[] = {
    {"print-records", printRecords, "Print all records to stdout (default)",
     true},
    {"dump-json", EmitJSON, "Dump all records as machine-readable JSON"},
    {"gen-lldb-option-defs", EmitOptionDefs,
     "Generate lldb option definitions"},
    {"gen-lldb-property-defs", EmitPropertyDefs,
     "Generate lldb property definitions"},
    {"gen-lldb-property-enum-defs", EmitPropertyEnumDefs,
     "Generate lldb property enum definitions"},
};

int main(int argc, char **argv) {
  sys::PrintStackTraceOnErrorSignal(argv[0]);
  PrettyStackTraceProgram X(argc, argv);
  clv2::OptionParser P;
  registerTableGenMainOptions(P);
  TableGen::Emitter::registerBackendOptions(P);
  P.parse(argc, argv);
  llvm_shutdown_obj Y;

  MultiFileTableGenMainFn MainFn = nullptr;
  return TableGenMain(argv[0], MainFn);
}

#ifdef __has_feature
#if __has_feature(address_sanitizer)
#include <sanitizer/lsan_interface.h>
// Disable LeakSanitizer for this binary as it has too many leaks that are not
// very interesting to fix. See compiler-rt/include/sanitizer/lsan_interface.h .
int __lsan_is_turned_off() { return 1; }
#endif // __has_feature(address_sanitizer)
#endif // defined(__has_feature)
