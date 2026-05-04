//===-- PluginLoader.cpp - Implement -load command line option ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the -load <plugin> command line option handler.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/PluginLoader.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/raw_ostream.h"
#include <vector>
using namespace llvm;

namespace {

struct Plugins {
  sys::SmartMutex<true> Lock;
  std::vector<std::string> List;
};

Plugins &getPlugins() {
  static Plugins P;
  return P;
}

} // anonymous namespace

void PluginLoader::operator=(const std::string &Filename) {
  auto &P = getPlugins();
  sys::SmartScopedLock<true> Lock(P.Lock);
  // The parser pre-loads plugins before draining dynamic registrations, so the
  // option's own callback reaches this a second time for the same file.
  if (llvm::is_contained(P.List, Filename))
    return;
  std::string Error;
  if (sys::DynamicLibrary::LoadLibraryPermanently(Filename.c_str(), &Error)) {
    errs() << "Error opening '" << Filename << "': " << Error
           << "\n  -load request ignored.\n";
  } else {
    P.List.push_back(Filename);
  }
}

unsigned PluginLoader::getNumPlugins() {
  auto &P = getPlugins();
  sys::SmartScopedLock<true> Lock(P.Lock);
  return P.List.size();
}

std::string &PluginLoader::getPlugin(unsigned num) {
  auto &P = getPlugins();
  sys::SmartScopedLock<true> Lock(P.Lock);
  assert(num < P.List.size() && "Asking for an out of bounds plugin");
  return P.List[num];
}

static void loadPlugin(const std::string &Filename) {
  PluginLoader PL;
  PL = Filename;
}

static constexpr clv2::ListOptionInfo<std::string> OI_Load{
    "load", "Load the specified plugin", clv2::value_desc("pluginfilename"),
    clv2::Callback<std::string>{loadPlugin}};
static constexpr clv2::OptionsRegistry<&OI_Load> PluginLoaderReg;

// Registration is explicit rather than automatic: lib/Support is built with
// -Werror=global-constructors, so this cannot be done from a namespace-scope
// initialiser.  Every tool that wants -load must call this (llc, opt,
// llvm-lto2 do).
static bool PluginLoaderOptionRegistered = false;

void llvm::registerPluginLoaderOption() {
  if (!PluginLoaderOptionRegistered) {
    clv2::registerDynamicRegistry<&PluginLoaderReg>();
    PluginLoaderOptionRegistered = true;
  }
}

bool llvm::pluginLoaderOptionRegistered() {
  return PluginLoaderOptionRegistered;
}
