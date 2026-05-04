//===-- Options.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TOOLS_LLVM_PROFGEN_OPTIONS_H
#define LLVM_TOOLS_LLVM_PROFGEN_OPTIONS_H

#include "llvm/ProfileData/SampleProf.h"
#include "llvm/Support/OptionsContext.h"
#include <cstdint>
#include <string>
#include <vector>

namespace llvm {
namespace clv2 {
class OptionsContext;
}

struct ProfGenConfig {
  const clv2::OptionsContext *OptsCtx = &clv2::defaultOptionsContext();
  // ProfileGenerator options
  std::string OutputFilename;
  sampleprof::SampleProfileFormat OutputFormat = sampleprof::SPF_Ext_Binary;
  bool UseMD5 = false;
  bool PopulateProfileSymbolList = false;
  bool FillZeroForAllFuncs = false;
  bool TrimColdProfile = false;
  bool MarkAllContextPreinlined = false;
  bool CSProfMergeColdContext = true;
  unsigned CSProfMergeColdContextOccurrences = 0;
  uint32_t CSProfMaxColdContextDepth = 1;
  double ProfileDensityThreshold = 50;
  bool ShowDensity = false;
  int ProfileDensityCutOffHot = 990000;
  bool UpdateTotalSamples = false;
  bool GenCSNestedProfile = true;
  bool InferMissingFrames = true;

  // PerfReader options
  bool SkipSymbolization = false;
  bool ShowMmapEvents = false;
  bool UseOffset = true;
  bool UseLoadableSegmentAsBase = false;
  bool IgnoreStackSamples = false;
  int CSProfMaxUnsymbolizedCtxDepth = -1;
  bool ShowDetailedWarning = false;
  bool TimeProfGen = false;

  // ProfiledBinary options
  bool ShowDisassemblyOnly = false;
  bool ShowSourceLocations = false;
  bool LoadFunctionFromSymbol = true;
  bool ShowCanonicalFnName = false;
  bool ShowPseudoProbe = false;
  bool UseDwarfCorrelation = false;
  std::string DWPPath;
  std::vector<std::string> DisassembleFunctions;
  bool KernelBinary = false;

  // CSPreInliner options
  bool EnableCSPreInliner = true;
  bool UseContextCostForPreInliner = true;
  bool SamplePreInlineReplay = false;
  int CSPreinlMultiplierForPrevInl = 100;

  // MissingFrameInferrer option
  uint32_t MaximumSearchDepth = UINT32_MAX - 1;
};

} // end namespace llvm

#endif
