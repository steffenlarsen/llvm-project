//===- AMDGPUSplitModuleOptions.h - AMDGPU module splitting options -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_AMDGPUSPLITMODULEOPTIONS_H
#define LLVM_TARGET_AMDGPUSPLITMODULEOPTIONS_H

#include <string>

namespace llvm {

struct AMDGPUSplitModuleOptions {
  unsigned MaxDepth = 8;
  float LargeFnFactor = 2.0f;
  float LargeFnOverlapForMerge = 0.7f;
  bool NoExternalizeGlobals = false;
  bool NoExternalizeOnAddrTaken = false;
  std::string ModuleDotCfgOutput;
  std::string PartitionSummariesOutput;
#ifndef NDEBUG
  bool UseLockFile = false;
  bool DebugProposalSearch = false;
#endif
};

} // end namespace llvm

#endif // LLVM_TARGET_AMDGPUSPLITMODULEOPTIONS_H
