//===- lld/Common/Driver.h - Linker Driver Emulator -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COMMON_DRIVER_H
#define LLD_COMMON_DRIVER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm::parallel {
class Executor;
} // namespace llvm::parallel

namespace lld {
enum Flavor {
  Invalid,
  Gnu,     // -flavor gnu
  MinGW,   // -flavor gnu MinGW
  WinLink, // -flavor link
  Darwin,  // -flavor darwin
  Wasm,    // -flavor wasm
};

using Driver = bool (*)(llvm::ArrayRef<const char *>, llvm::raw_ostream &,
                        llvm::raw_ostream &, bool, bool,
                        llvm::parallel::Executor *);

struct DriverDef {
  Flavor f;
  Driver d;
};

struct Result {
  int retCode;
  bool canRunAgain;
};

// Generic entry point when using LLD as a library, safe for re-entry, supports
// crash recovery. Returns a general completion code and a boolean telling
// whether it can be called again. In some cases, a crash could corrupt memory
// and re-entry would not be possible anymore. Use exitLld() in that case to
// properly exit your application and avoid intermittent crashes on exit caused
// by cleanup.
//
// If `executor` is given, the link runs its parallel work on that executor,
// using at most as many of its threads as the link's --threads option allows.
// This lets a host share one thread pool between links. Otherwise the link
// creates its own pool, sized by --threads, and destroys it when it is done.
Result lldMain(llvm::ArrayRef<const char *> args, llvm::raw_ostream &stdoutOS,
               llvm::raw_ostream &stderrOS, llvm::ArrayRef<DriverDef> drivers,
               llvm::parallel::Executor *executor = nullptr);
} // namespace lld

// With this macro, library users must specify which drivers they use, provide
// that information to lldMain() in the `drivers` param, and link the
// corresponding driver library in their executable.
#define LLD_HAS_DRIVER(name)                                                   \
  namespace lld {                                                              \
  namespace name {                                                             \
  bool link(llvm::ArrayRef<const char *> args, llvm::raw_ostream &stdoutOS,    \
            llvm::raw_ostream &stderrOS, bool exitEarly, bool disableOutput,   \
            llvm::parallel::Executor *executor);                               \
  }                                                                            \
  }

// An array which declares that all LLD drivers are linked in your executable.
// Must be used along with LLD_HAS_DRIVERS. See examples in LLD unittests.
#define LLD_ALL_DRIVERS                                                        \
  {                                                                            \
    {lld::WinLink, &lld::coff::link}, {lld::Gnu, &lld::elf::link},             \
        {lld::MinGW, &lld::mingw::link}, {lld::Darwin, &lld::macho::link}, {   \
      lld::Wasm, &lld::wasm::link                                              \
    }                                                                          \
  }

#endif
