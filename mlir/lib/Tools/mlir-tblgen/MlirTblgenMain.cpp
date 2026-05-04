//===- MlirTblgenMain.cpp - MLIR Tablegen Driver main -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Main entry function for mlir-tblgen for when built as standalone binary.
//
//===----------------------------------------------------------------------===//

#include "mlir/Tools/mlir-tblgen/MlirTblgenMain.h"

#include "mlir/TableGen/GenInfo.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/Signals.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Main.h"
#include "llvm/TableGen/Record.h"
#include <deque>

using namespace mlir;
using namespace llvm;

enum DeprecatedAction { DA_None, DA_Warn, DA_Error };

static constexpr clv2::EnumVal<DeprecatedAction> DeprecatedActionVals[] = {
    {"none", DA_None, "No action"},
    {"warn", DA_Warn, "Warn on use"},
    {"error", DA_Error, "Error on use"},
};
static constexpr clv2::OptionInfo<DeprecatedAction> ActionOnDeprecatedOpt{
    "on-deprecated", "Action to perform on deprecated def",
    clv2::ValuesRef<DeprecatedAction>(DeprecatedActionVals),
    clv2::Init{DA_Warn}};
static constexpr clv2::OptionsRegistry<&ActionOnDeprecatedOpt>
    DeprecatedOptsReg;

// Returns if there is a use of `deprecatedInit` in `field`.
static bool findUse(const Init *field, const Init *deprecatedInit,
                    llvm::DenseMap<const Init *, bool> &known) {
  if (field == deprecatedInit)
    return true;

  auto it = known.find(field);
  if (it != known.end())
    return it->second;

  auto memoize = [&](bool val) {
    known[field] = val;
    return val;
  };

  if (auto *defInit = dyn_cast<DefInit>(field)) {
    // Only recurse into defs if they are anonymous.
    // Non-anonymous defs are handled by the main loop, with a proper
    // deprecation warning for each. Returning true here, would cause
    // all users of a def to also emit a deprecation warning.
    if (!defInit->getDef()->isAnonymous())
      // Purposefully not memoize as to not include every def use in the map.
      // This is also a trivial case we return false for in constant time.
      return false;

    return memoize(
        llvm::any_of(defInit->getDef()->getValues(), [&](const RecordVal &val) {
          return findUse(val.getValue(), deprecatedInit, known);
        }));
  }

  if (auto *dagInit = dyn_cast<DagInit>(field)) {
    if (findUse(dagInit->getOperator(), deprecatedInit, known))
      return memoize(true);

    return memoize(llvm::any_of(dagInit->getArgs(), [&](const Init *arg) {
      return findUse(arg, deprecatedInit, known);
    }));
  }

  if (const ListInit *li = dyn_cast<ListInit>(field)) {
    return memoize(llvm::any_of(li->getElements(), [&](const Init *jt) {
      return findUse(jt, deprecatedInit, known);
    }));
  }

  // Purposefully don't use memoize here. There is no need to cache the result
  // for every kind of init (e.g. BitInit or StringInit), which will always
  // return false. Doing so would grow the DenseMap to include almost every Init
  // within the main file.
  return false;
}

// Returns if there is a use of `deprecatedInit` in `record`.
static bool findUse(Record &record, const Init *deprecatedInit,
                    llvm::DenseMap<const Init *, bool> &known) {
  return llvm::any_of(record.getValues(), [&](const RecordVal &val) {
    return findUse(val.getValue(), deprecatedInit, known);
  });
}

static void warnOfDeprecatedUses(const RecordKeeper &records,
                                 DeprecatedAction actionOnDeprecatedValue) {
  // This performs a direct check for any def marked as deprecated and then
  // finds all uses of deprecated def. Deprecated defs are not expected to be
  // either numerous or long lived.
  bool deprecatedDefsFounds = false;
  for (auto &it : records.getDefs()) {
    const RecordVal *r = it.second->getValue("odsDeprecated");
    if (!r || !r->getValue())
      continue;

    llvm::DenseMap<const Init *, bool> hasUse;
    if (auto *si = dyn_cast<StringInit>(r->getValue())) {
      for (auto &jt : records.getDefs()) {
        // Skip anonymous defs.
        if (jt.second->isAnonymous())
          continue;

        if (findUse(*jt.second, it.second->getDefInit(), hasUse)) {
          PrintWarning(jt.second->getLoc(),
                       "Using deprecated def `" + it.first + "`");
          PrintNote(si->getAsUnquotedString());
          deprecatedDefsFounds = true;
        }
      }
    }
  }
  if (deprecatedDefsFounds && actionOnDeprecatedValue == DA_Error)
    PrintFatalNote("Error'ing out due to deprecated defs");
}

// TableGenMain requires a function pointer so this function is passed in which
// simply wraps the call to the generator.
static bool mlirTableGenMain(raw_ostream &os, const RecordKeeper &records,
                             DeprecatedAction actionOnDeprecatedValue,
                             const GenInfo *generator) {
  if (actionOnDeprecatedValue != DA_None)
    warnOfDeprecatedUses(records, actionOnDeprecatedValue);

  if (!generator) {
    os << records;
    return false;
  }
  return generator->invoke(records, os);
}

/// Ctx identifies the GenInfo this flag selects and where to record it.
namespace {
struct GeneratorSelection {
  const GenInfo *Info;
  const GenInfo **Selected;
};
} // namespace

static bool selectGenerator(void *Ctx, const bool &) {
  auto *Sel = static_cast<GeneratorSelection *>(Ctx);
  *Sel->Selected = Sel->Info;
  return true;
}

static void registerGeneratorOption(clv2::OptionParser &P,
                                    const GenInfo *&SelectedGenerator) {
  ArrayRef<GenInfo> gens = mlir::getRegisteredGenerators();
  // Sort generators alphabetically for deterministic output.
  llvm::SmallVector<const GenInfo *> sorted;
  for (const auto &G : gens)
    sorted.push_back(&G);
  llvm::sort(sorted, [](const GenInfo *A, const GenInfo *B) {
    return A->getGenArgument() < B->getGenArgument();
  });
  static std::deque<clv2::RuntimeOption<bool>> Options;
  static std::deque<GeneratorSelection> Selections;
  bool First = true;
  for (const auto *GPtr : sorted) {
    const auto &G = *GPtr;
    Selections.push_back(GeneratorSelection{GPtr, &SelectedGenerator});
    Options.emplace_back(
        G.getGenArgument(), G.getGenDescription(), clv2::ValueDisallowed,
        clv2::CtxCallback<bool>{&selectGenerator, &Selections.back()});
    // Group display has no descriptor spelling, so it is set on the option's
    // own static info.
    Options.back().staticInfo().IsEnumGroupMember = true;
    if (First)
      Options.back().staticInfo().EnumGroupHeader = "Generator to run";
    clv2::detail::OptionEntry E = Options.back().makeEntry();
    P.addDynamicEntry(std::move(E));
    First = false;
  }
}

int mlir::MlirTblgenMain(
    int argc, char **argv,
    std::function<void(llvm::clv2::OptionParser &)> ConfigureParser) {

  llvm::InitLLVM y(argc, argv);

  clv2::OptionParser P;
  llvm::registerTableGenMainOptions(P);
  const GenInfo *SelectedGenerator = nullptr;
  registerGeneratorOption(P, SelectedGenerator);
  P.add<&DeprecatedOptsReg>();
  if (ConfigureParser)
    ConfigureParser(P);
  auto OptsCtx = P.parse(argc, argv);
  auto *DeprecatedOpts = OptsCtx->getViewPtr<&DeprecatedOptsReg>();
  DeprecatedAction ActionOnDeprecated =
      DeprecatedOpts->get<&ActionOnDeprecatedOpt>();

  return TableGenMain(argv[0], [&](TableGenOutputFiles &OutFiles,
                                   const RecordKeeper &RK) {
    std::string S;
    raw_string_ostream OS(S);
    bool Res = mlirTableGenMain(OS, RK, ActionOnDeprecated, SelectedGenerator);
    OutFiles = {S, {}};
    return Res;
  });
}
