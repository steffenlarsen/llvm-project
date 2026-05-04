//===- CGPassBuilderOption.h - Options for pass builder ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the CCState and CCValAssign classes, used for lowering
// and implementing calling conventions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_CGPASSBUILDEROPTION_H
#define LLVM_TARGET_CGPASSBUILDEROPTION_H

#include "llvm/Support/Compiler.h"
#include "llvm/Target/TargetOptions.h"
#include <optional>
#include <string>

namespace llvm {
namespace clv2 {
class OptionsContext;
}

enum class RunOutliner {
  TargetDefault,
  AlwaysOutline,
  OptimisticPGO,
  ConservativePGO,
  NeverOutline
};
enum class RegAllocType { Unset, Default, Basic, Fast, Greedy, PBQP };

// Not one-on-one but mostly corresponding to commandline options in
// TargetPassConfig.cpp.
struct CGPassBuilderOption {
  std::optional<bool> OptimizeRegAlloc;
  std::optional<bool> EnableIPRA;
  bool DebugPM = false;
  bool DisableVerify = false;
  bool EnableImplicitNullChecks = false;
  bool EnableBlockPlacementStats = false;
  bool EnableGlobalMergeFunc = false;
  bool EnableMachineFunctionSplitter = false;
  bool EnableSinkAndFold = false;
  bool EnableTailMerge = true;
  /// Enable LoopTermFold immediately after LSR.
  bool EnableLoopTermFold = false;
  bool MISchedPostRA = false;
  bool EarlyLiveIntervals = false;
  bool EnableGCEmptyBlocks = false;

  bool DisableLSR = false;
  bool DisableCGP = false;
  bool DisablePartialLibcallInlining = false;
  bool DisableConstantHoisting = false;
  bool DisableSelectOptimize = true;
  bool DisableAtExitBasedGlobalDtorLowering = false;
  bool DisableExpandReductions = false;
  bool DisableRAFSProfileLoader = false;
  bool DisableLayoutFSProfileLoader = false;
  bool DisableCFIFixup = false;
  bool DisablePostRASched = false;
  bool DisableBranchFold = false;
  bool DisableTailDuplicate = false;
  bool DisableEarlyTailDup = false;
  bool DisableBlockPlacement = false;
  bool DisableSSC = false;
  bool DisableMachineDCE = false;
  bool DisableEarlyIfConversion = false;
  bool DisableMachineLICM = false;
  bool DisableMachineCSE = false;
  bool DisablePostRAMachineLICM = false;
  bool DisableMachineSink = false;
  bool DisablePostRAMachineSink = false;
  bool DisableCopyProp = false;
  bool PrintAfterISel = false;
  bool PrintISelInput = false;
  bool PrintRegUsage = false;
  bool RequiresCodeGenSCCOrder = false;
  bool SplitStaticData = false;
  bool BasicBlockSectionMatchInfer = false;
  bool EmitBBHash = false;

  RunOutliner EnableMachineOutliner = RunOutliner::TargetDefault;
  RegAllocType RegAlloc = RegAllocType::Unset;
  std::optional<GlobalISelAbortMode> EnableGlobalISelAbort;
  std::string FSProfileFile;
  std::string FSRemappingFile;

  std::optional<bool> VerifyMachineCode;
  std::optional<bool> EnableFastISelOption;
  std::optional<bool> EnableGlobalISelOption;
  std::optional<bool> DebugifyAndStripAll;
  std::optional<bool> DebugifyCheckAndStripAll;

  // Start/stop pass pipeline options.
  std::string StartBefore;
  std::string StartAfter;
  std::string StopBefore;
  std::string StopAfter;
};

LLVM_ABI CGPassBuilderOption
getCGPassBuilderOption(const clv2::OptionsContext &Ctx);

} // namespace llvm

#endif // LLVM_TARGET_CGPASSBUILDEROPTION_H
