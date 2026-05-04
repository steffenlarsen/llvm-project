//===- TableGenBackend.cpp - Utilities for TableGen Backends ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides useful services for TableGen backends...
//
//===----------------------------------------------------------------------===//

#include "llvm/TableGen/TableGenBackend.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

using namespace llvm;
using namespace TableGen::Emitter;

const size_t MAX_LINE_LEN = 80U;

namespace {
struct BackendEntry {
  std::string Name;
  FnT CB;
  std::string Desc;
  // Per-entry parse state.  The option is built at runtime because the name
  // and description only exist once a backend has registered, so it is a
  // RuntimeOption (which owns the descriptor, its static info, and the value
  // slot).  Held behind a unique_ptr because RuntimeOption is immovable and
  // Entries reallocates as backends register; the pointee's address, which
  // clv2 retains, stays put.
  std::unique_ptr<clv2::RuntimeOption<bool>> Opt;
};

struct BackendRegistry;
static BackendRegistry &getBackendRegistry();

struct BackendRegistry {
  std::vector<BackendEntry> Entries;
  FnT DefaultCB;
  FnT SelectedCB;
  bool Registered = false;
  bool Selected = false;
  bool FallbackDone = false;

  void add(StringRef Name, FnT CB, StringRef Desc, bool ByDefault) {
    Entries.push_back({Name.str(), CB, Desc.str()});
    if (ByDefault)
      DefaultCB = CB;
  }

  void ensureRegistered() {
    if (Registered)
      return;
    Registered = true;
  }

  /// Invoked by clv2 when a backend flag is seen; Ctx is the BackendEntry.
  static bool selectBackend(void *Ctx, const bool &) {
    auto *BE = static_cast<BackendEntry *>(Ctx);
    BackendRegistry &R = getBackendRegistry();
    R.SelectedCB = BE->CB;
    R.Selected = true;
    return true;
  }

  void registerAll(clv2::OptionParser &P, StringRef GroupHeader = {}) {
    bool First = true;
    for (auto &BE : Entries) {
      if (!BE.Opt)
        BE.Opt = std::make_unique<clv2::RuntimeOption<bool>>(
            BE.Name, BE.Desc, clv2::ValueDisallowed,
            clv2::CtxCallback<bool>{&selectBackend, &BE});
      // Group-header display is a help-only concern with no descriptor
      // spelling, so it is set on the option's own static info.
      if (!GroupHeader.empty()) {
        BE.Opt->staticInfo().IsEnumGroupMember = true;
        if (First)
          BE.Opt->staticInfo().EnumGroupHeader = GroupHeader;
      }
      clv2::detail::OptionEntry E = BE.Opt->makeEntry();
      P.addDynamicEntry(std::move(E));
      First = false;
    }
  }
};

BackendRegistry &getBackendRegistry() {
  static BackendRegistry R;
  return R;
}
} // namespace

Opt::Opt(StringRef Name, FnT CB, StringRef Desc, bool ByDefault) {
  auto &R = getBackendRegistry();
  R.add(Name, CB, Desc, ByDefault);
  R.ensureRegistered();
}

void llvm::TableGen::Emitter::registerBackendOptions(clv2::OptionParser &P) {
  getBackendRegistry().registerAll(P, "Action to perform:");
}

bool llvm::TableGen::Emitter::ApplyCallback(const RecordKeeper &Records,
                                            TableGenOutputFiles &OutFiles,
                                            StringRef FilenamePrefix) {
  auto &BR = getBackendRegistry();
  FnT Fn = BR.Selected ? BR.SelectedCB : BR.DefaultCB;
  if (Fn.SingleFileGenerator) {
    std::string S;
    raw_string_ostream OS(S);
    Fn.SingleFileGenerator(Records, OS);
    OutFiles = {std::move(S), {}};
    return false;
  }
  if (Fn.MultiFileGenerator) {
    OutFiles = Fn.MultiFileGenerator(FilenamePrefix, Records);
    return false;
  }
  return true;
}

static void printLine(raw_ostream &OS, const Twine &Prefix, char Fill,
                      StringRef Suffix) {
  size_t Pos = (size_t)OS.tell();
  assert((Prefix.str().size() + Suffix.size() <= MAX_LINE_LEN) &&
         "header line exceeds max limit");
  OS << Prefix;
  for (size_t i = (size_t)OS.tell() - Pos, e = MAX_LINE_LEN - Suffix.size();
         i < e; ++i)
    OS << Fill;
  OS << Suffix << '\n';
}

void llvm::emitSourceFileHeader(StringRef Desc, raw_ostream &OS,
                                const RecordKeeper &Record) {
  printLine(OS, "/*===- TableGen'erated file ", '-', "*- C++ -*-===*\\");
  StringRef Prefix("|* ");
  StringRef Suffix(" *|");
  printLine(OS, Prefix, ' ', Suffix);
  size_t PSLen = Prefix.size() + Suffix.size();
  assert(PSLen < MAX_LINE_LEN);
  size_t Pos = 0U;
  do {
    size_t Length = std::min(Desc.size() - Pos, MAX_LINE_LEN - PSLen);
    printLine(OS, Prefix + Desc.substr(Pos, Length), ' ', Suffix);
    Pos += Length;
  } while (Pos < Desc.size());
  printLine(OS, Prefix, ' ', Suffix);
  printLine(OS, Prefix + "Automatically generated file, do not edit!", ' ',
            Suffix);

  // Print the filename of source file.
  if (!Record.getInputFilename().empty())
    printLine(
        OS, Prefix + "From: " + sys::path::filename(Record.getInputFilename()),
        ' ', Suffix);
  printLine(OS, Prefix, ' ', Suffix);
  printLine(OS, "\\*===", '-', "===*/");
  OS << '\n';
}
