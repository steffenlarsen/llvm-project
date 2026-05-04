//===-- llvm/Support/PluginLoader.h - Plugin Loader for Tools ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A tool can #include this file to get a -load option that allows the user to
// load arbitrary shared objects into the tool's address space.  Note that this
// header can only be included by a program ONCE, so it should never to used by
// library authors.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_PLUGINLOADER_H
#define LLVM_SUPPORT_PLUGINLOADER_H

#include "llvm/Support/Compiler.h"

#include <string>

namespace llvm {
  struct PluginLoader {
    LLVM_ABI void operator=(const std::string &Filename);
    LLVM_ABI static unsigned getNumPlugins();
    LLVM_ABI static std::string &getPlugin(unsigned num);
  };

  /// Register the -load runtime option. Call this explicitly from tool entry
  /// points before parsing instead of relying on a global constructor.
  LLVM_ABI void registerPluginLoaderOption();

  /// Whether registerPluginLoaderOption() has been called, i.e. whether -load
  /// is an option of this program. The parser uses this to decide whether to
  /// dlopen -load arguments before it snapshots dynamic registrations.
  LLVM_ABI bool pluginLoaderOptionRegistered();
}

#endif
