//===- SupportOptions.cpp - LLVMSupport option bridge --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/SupportOptions.h"
#include "DebugOptions.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/BoolOrDefault.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DebugCounter.h"
#include "llvm/Support/OptionsContext.h"

using namespace llvm;
using namespace llvm::clv2;

bool support::StatsEnabled = false;
bool support::StatsAsJsonEnabled = false;
unsigned support::DebugBufferSizeVal = 0;
bool support::ViewBackgroundFlag = false;
StringRef support::DagFileLocation;
/// Backing storage for DagFileLocation.  Deliberately a pointer, not a
/// std::string: llvm/lib/Support is built with -Werror=global-constructors.
/// Kept at namespace scope alongside the other bridge globals above rather
/// than hidden in a function-local static -- this is process-wide state and
/// should look like it.  Never freed; readers hold a StringRef into it.
static std::string *DagFileLocationStorage = nullptr;
bool support::NoOpenDagViewer = false;

// Defined in Signals.cpp — written directly because signal handlers read it
// without going through a getter.
extern bool DisableSymbolicationFlag;
extern cl::boolOrDefault UseColorVal;

void support::applySupportOptions(const support::ParsedOpts &Opts) {
  if (Opts.specified<&SUP_DisableSymbolication>())
    DisableSymbolicationFlag = Opts.get<&SUP_DisableSymbolication>();

#ifndef NDEBUG
  if (Opts.specified<&SUP_Debug>())
    llvm::DebugFlag = Opts.get<&SUP_Debug>();

  if (Opts.specified<&SUP_DebugOnly>()) {
    llvm::DebugFlag = true;
    const auto &Types = Opts.get<&SUP_DebugOnly>();
    SmallVector<const char *, 8> TypePtrs;
    TypePtrs.reserve(Types.size());
    for (const std::string &Type : Types)
      TypePtrs.push_back(Type.c_str());
    llvm::setCurrentDebugTypes(TypePtrs.data(), TypePtrs.size());
  }
#endif

  if (Opts.specified<&SUP_DebugCounter>()) {
    for (const std::string &Arg : Opts.get<&SUP_DebugCounter>())
      llvm::DebugCounter::instance().push_back(Arg);
  }
  if (Opts.specified<&SUP_PrintDebugCounter>())
    llvm::DebugCounter::instance().setPrintCounter(
        Opts.get<&SUP_PrintDebugCounter>());
  if (Opts.specified<&SUP_PrintDebugCounterQueries>())
    llvm::DebugCounter::instance().setPrintCounterQueries(
        Opts.get<&SUP_PrintDebugCounterQueries>());
  if (Opts.specified<&SUP_BreakOnLastCount>())
    llvm::DebugCounter::instance().setBreakOnLast(
        Opts.get<&SUP_BreakOnLastCount>());

  if (Opts.specified<&SUP_InfoOutputFile>())
    setInfoOutputFilename(Opts.get<&SUP_InfoOutputFile>());
  if (Opts.specified<&SUP_TrackMemory>())
    setTrackSpace(Opts.get<&SUP_TrackMemory>());
  if (Opts.specified<&SUP_SortTimers>())
    setSortTimers(Opts.get<&SUP_SortTimers>());
  if (Opts.specified<&SUP_Stats>())
    support::StatsEnabled = Opts.get<&SUP_Stats>();
  if (Opts.specified<&SUP_StatsJson>())
    support::StatsAsJsonEnabled = Opts.get<&SUP_StatsJson>();
  if (Opts.specified<&SUP_DebugBufferSize>())
    support::DebugBufferSizeVal = Opts.get<&SUP_DebugBufferSize>();
  if (Opts.specified<&SUP_ViewBackground>())
    support::ViewBackgroundFlag = Opts.get<&SUP_ViewBackground>();
  if (Opts.specified<&SUP_DagFileLocation>()) {
    if (!DagFileLocationStorage)
      DagFileLocationStorage = new std::string();
    *DagFileLocationStorage = Opts.get<&SUP_DagFileLocation>();
    support::DagFileLocation = *DagFileLocationStorage;
  }
  if (Opts.specified<&SUP_NoOpenDagViewer>())
    support::NoOpenDagViewer = Opts.get<&SUP_NoOpenDagViewer>();
}
