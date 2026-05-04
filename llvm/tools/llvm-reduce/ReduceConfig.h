//===- ReduceConfig.h - llvm-reduce configuration struct ---------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// All llvm-reduce command-line option values collected in one struct.
// Populated in main() after clv2 parse; threaded to consumers via TestRunner.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_REDUCE_REDUCECONFIG_H
#define LLVM_TOOLS_LLVM_REDUCE_REDUCECONFIG_H

#include "llvm/Config/llvm-config.h"
#include <string>
#include <vector>

namespace llvm {

struct ReduceConfig {
  bool Verbose = false;
  bool AbortOnInvalidReduction = false;
  bool SkipVerifyAfterCountingChunks = false;
  unsigned StartingGranularityLevel = 0;
#ifdef LLVM_ENABLE_THREADS
  unsigned NumJobs = 1;
#else
  static constexpr unsigned NumJobs = 1;
#endif
  std::vector<std::string> DeltaPassList;
  std::vector<std::string> SkipDeltaPassList;
  std::string ReduceTargetTriple;
  bool PrintInvalidMachineReductions = false;
  bool TmpFilesAsBitcode = false;
  int CallsiteInlineThreshold = 5;
  bool AggressiveMetadataReduction = false;
  std::string IRPassPipeline =
      "function(sroa,instcombine<no-verify-fixpoint>,gvn,"
      "simplifycfg,infer-address-spaces)";
};

} // namespace llvm

#endif
