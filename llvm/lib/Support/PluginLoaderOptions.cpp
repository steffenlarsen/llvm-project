//===- PluginLoaderOptions.cpp - `-load` plugin option -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/PluginLoaderOptions.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/PluginLoader.h"

using namespace llvm;

PluginLoaderOptions PluginLoaderOptions::Current;

Error PluginLoaderOptions::parse(ArrayRef<const char *> Args,
                                  SmallVectorImpl<const char *> &Rest,
                                  raw_ostream &Errs) {
  for (const char *Arg : Args) {
    StringRef A(Arg);
    if (A.consume_front("-load=") || A.consume_front("--load=")) {
      Load.push_back(A.str());
      continue;
    }
    Rest.push_back(Arg);
  }
  return Error::success();
}

void llvm::loadRequestedPlugins() {
  for (const std::string &Filename : PluginLoaderOptions::Current.Load) {
    PluginLoader PL;
    PL = Filename;
  }
}

std::vector<const char *>
llvm::loadPluginsAndStripArgs(int argc, const char *const *argv) {
  std::vector<const char *> Result;
  if (argc == 0)
    return Result;
  Result.push_back(argv[0]);

  SmallVector<const char *, 32> Rest;
  std::string Errs;
  raw_string_ostream ErrsOS(Errs);
  cantFail(PluginLoaderOptions::Current.parse(
      ArrayRef<const char *>(argv + 1, argv + argc), Rest, ErrsOS));
  loadRequestedPlugins();

  Result.insert(Result.end(), Rest.begin(), Rest.end());
  return Result;
}
