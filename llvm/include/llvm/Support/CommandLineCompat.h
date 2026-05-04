//===- llvm/Support/CommandLineCompat.h - cl:: compat aliases ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_COMMANDLINECOMPAT_H
#define LLVM_SUPPORT_COMMANDLINECOMPAT_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/BoolOrDefault.h"
#include "llvm/Support/CommandLineTokenizer.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"

// Version printing — always available regardless of CommandLine.h.
namespace llvm::cl {
/// Print the version banner (and any registered extra version printers) to
/// \p OS.
LLVM_ABI void PrintVersionMessage(raw_ostream &OS);
/// Convenience overload targeting llvm::outs().
LLVM_ABI void PrintVersionMessage();
LLVM_ABI void SetVersionPrinter(std::function<void(raw_ostream &)> Fn);
LLVM_ABI void AddExtraVersionPrinter(std::function<void(raw_ostream &)> Fn);
/// No-op: clv2 prints option values from the parser itself when
/// --print-all-options or --print-options is seen, which is the only point at
/// which the values are final.  Kept so out-of-tree cl:: callers still
/// compile; they lose nothing, since the dump has already happened.
LLVM_DEPRECATED("option values are printed by the parser; this call does "
                "nothing",
                "")
inline void PrintOptionValues() {}
} // namespace llvm::cl

// Declarations of things cl:: defines, for callers that include only this
// header.  Guarded because CommandLine.h declares them itself.
#ifndef LLVM_SUPPORT_COMMANDLINE_H

namespace llvm::cl {

/// Defined by cl::; prints help for the cl:: option registry.
LLVM_ABI void PrintHelpMessage(bool Hidden = false, bool Categorized = false);
LLVM_ABI ArrayRef<StringRef> getCompilerBuildConfig();
LLVM_ABI void printBuildConfig(raw_ostream &OS);

using OptionCategory = llvm::clv2::OptionCategory;

using llvm::clv2::ConsumeAfter;
using llvm::clv2::OneOrMore;
using llvm::clv2::Optional;
using llvm::clv2::Required;
using llvm::clv2::ZeroOrMore;
using NumOccurrencesFlag = llvm::clv2::OptionNumOccurrencesFlag;

using llvm::clv2::ValueDisallowed;
using llvm::clv2::ValueOptional;
using llvm::clv2::ValueRequired;

using llvm::clv2::Hidden;
using llvm::clv2::NotHidden;
using llvm::clv2::ReallyHidden;

using llvm::clv2::CommaSeparated;
using llvm::clv2::Sink;

inline OptionCategory &getGeneralCategory() {
  static OptionCategory Cat("General options");
  return Cat;
}

inline void ResetAllOptionOccurrences() {
  // No-op: OptionParser uses per-instance state, no global reset needed.
}

struct extrahelp {
  StringRef morehelp;
  explicit extrahelp(StringRef help) : morehelp(help) {}
};

/// Shim for cl::list_init, used by MLIR PassOptions ListOption defaults.
template <class Ty> struct list_initializer {
  ArrayRef<Ty> Inits;
  list_initializer(ArrayRef<Ty> Vals) : Inits(Vals) {}

  template <class Opt> void apply(Opt &O) const { O.setInitialValues(Inits); }
};

template <class Ty> list_initializer<Ty> list_init(ArrayRef<Ty> Vals) {
  return list_initializer<Ty>(Vals);
}

} // namespace llvm::cl

#endif // !LLVM_SUPPORT_COMMANDLINE_H

#endif // LLVM_SUPPORT_COMMANDLINECOMPAT_H
