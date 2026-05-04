//===- bolt/Utils/CommandLineOpts.cpp - BOLT CLI options ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// BOLT CLI options
//
//===----------------------------------------------------------------------===//

#include "bolt/Utils/CommandLineOpts.h"
#include "VCSVersion.inc"
#include "bolt/Core/BinaryContext.h"
#include "bolt/Utils/BoltUtilsOptionsOptInfos.h"

using namespace llvm;

namespace llvm {
namespace bolt {
const char *BoltRevision =
#ifdef BOLT_REVISION
    BOLT_REVISION;
#else
    "<unknown>";
#endif
}
}

namespace opts {

// cl::OptionCategory objects.
cl::OptionCategory BoltCategory("BOLT generic options");
cl::OptionCategory BoltDiffCategory("BOLTDIFF generic options");
cl::OptionCategory BoltOptCategory("BOLT optimization options");
cl::OptionCategory BoltRelocCategory("BOLT options in relocation mode");
cl::OptionCategory BoltOutputCategory("Output options");
cl::OptionCategory AggregatorCategory("Data aggregation options");
cl::OptionCategory BoltInstrCategory("BOLT instrumentation options");
cl::OptionCategory HeatmapCategory("Heatmap options");
cl::OptionCategory BinaryAnalysisCategory("BinaryAnalysis options");

unsigned getVerbosity(const bolt::BinaryContext &BC) {
  if (auto *Opts =
          bolt::bolt_utils_opts::getBoltUtilsOpts(BC.getOptionsContext()))
    return Opts->get<&clv2::BOLT_Verbosity>();
  return 0;
}

} // namespace opts

namespace llvm {
namespace bolt {

bool processAllFunctions(const clv2::OptionsContext &Ctx) {
  if (auto *Opts = bolt_utils_opts::getBoltUtilsOpts(Ctx)) {
    if (Opts->get<&clv2::BOLT_AggregateOnly>())
      return false;
    if (Opts->get<&clv2::BOLT_UseOldText>() ||
        Opts->get<&clv2::BOLT_StrictMode>())
      return true;
  }
  return false;
}

} // namespace bolt
} // namespace llvm
