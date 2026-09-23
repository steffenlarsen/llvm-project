//===- PluginLoaderOptions.h - `-load` plugin option ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// llvm::PluginLoaderOptions: a hand-written options struct for `-load=<path>`
// (Joined, one plugin per occurrence, repeatable). This cannot be generated
// by -gen-opt-parser-defs the way llvm::PassesOptions is: that mechanism is
// built on llvm::opt::OptTable/Arg/ArgList (LLVMOption), and llvm/lib/Option
// links Support (see llvm/lib/Option/CMakeLists.txt's LINK_COMPONENTS), so
// Support cannot link Option back without a cycle -- and this struct must
// live in lib/Support, next to PluginLoader.cpp. It exposes the exact same
// `parse(Args, Rest, Errs)` signature a generated struct does, so it composes
// with llvm::opt::parseLibraryOptionsChain (see
// llvm/include/llvm/Option/LibraryOptions.h) like any other library options
// struct, but has no Slot/table()/apply()/RegisterLibraryOptions<T> -- those
// only matter for LLVMContext::getOptions<T>() integration, which plugin
// loading (an inherently process-wide, one-shot side effect) does not need.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_PLUGINLOADEROPTIONS_H
#define LLVM_SUPPORT_PLUGINLOADEROPTIONS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <vector>

namespace llvm {

struct PluginLoaderOptions {
  std::vector<std::string> Load;

  LLVM_ABI static PluginLoaderOptions Current;

  LLVM_ABI llvm::Error parse(llvm::ArrayRef<const char *> Args,
                              llvm::SmallVectorImpl<const char *> &Rest,
                              llvm::raw_ostream &Errs);
};

/// Dlopens every plugin collected into PluginLoaderOptions::Current.Load, in
/// argv order, the same way the old clv2-based `-load` callback did. Call
/// this once, right after parsing, before anything that might depend on a
/// plugin's own static initializers having run.
LLVM_ABI void loadRequestedPlugins();

/// Convenience wrapper for tools that only need PluginLoaderOptions (no other
/// library options struct) ahead of their own clv2::OptionParser::parse()
/// call: parses and strips `-load=`/`--load=` from argv, dlopens each
/// requested plugin via loadRequestedPlugins(), and returns what's left --
/// with argv[0] preserved as the first element -- for the caller to forward
/// on, e.g. `P.parse(Rest.size(), Rest.data(), ...)`.
LLVM_ABI std::vector<const char *>
loadPluginsAndStripArgs(int argc, const char *const *argv);

} // namespace llvm

#endif // LLVM_SUPPORT_PLUGINLOADEROPTIONS_H
