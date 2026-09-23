//===- TargetPassConfig.cpp - Target independent code generation passes ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines interfaces to access the target independent code
// generation passes provided by the LLVM backend.
//
//===---------------------------------------------------------------------===//

#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/ScopedNoAliasAA.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/TypeBasedAliasAnalysis.h"
#include "llvm/CodeGen/BasicBlockSectionsProfileReader.h"
#include "llvm/CodeGen/CSEConfigBase.h"
#include "llvm/CodeGen/CodeGenPassOptionsSched1.h"
#include "llvm/CodeGen/CodeGenPassOptionsSched2.h"
#include "llvm/CodeGen/CodeGenTargetMachineImpl.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachinePassRegistry.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/PassInstrumentation.h"
#include "llvm/IR/Verifier.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Pass.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Discriminator.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Target/CGPassBuilderOption.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/ObjCARC.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils.h"
#include <cassert>
#include <optional>
#include <string>

using namespace llvm;

namespace {
class TriggerCrashFunctionLegacyPass : public FunctionPass {
public:
  static char ID;
  TriggerCrashFunctionLegacyPass() : FunctionPass(ID) {}
  bool runOnFunction(Function &F) override {
    abort();
    return false;
  }
  StringRef getPassName() const override { return "TriggerCrashFunctionPass"; }
};
} // namespace

char TriggerCrashFunctionLegacyPass::ID = 0;

/// Option names for limiting the codegen pipeline.
/// Those are used in error reporting and we didn't want
/// to duplicate their names all over the place.
static const char StartAfterOptName[] = "start-after";
static const char StartBeforeOptName[] = "start-before";
static const char StopAfterOptName[] = "stop-after";
static const char StopBeforeOptName[] = "stop-before";

// Override storage for clv2-migrated tools (set via setTPCValues()).
static std::optional<CGPassBuilderOption> TPCOverride;

void llvm::setTPCValues(const CGPassBuilderOption &V) { TPCOverride = V; }

static constexpr clv2::OptionInfo<bool> OI_CodegenTriggerCrash{
    "codegen-pipeline-trigger-crash", "Trigger a crash in the codegen pipeline",
    clv2::Init{false}, clv2::Hidden};

static constexpr clv2::OptionsRegistry<&OI_CodegenTriggerCrash>
    CodegenTriggerCrashReg;

static const int CodegenTriggerCrashRegistered = [] {
  clv2::registerDynamicRegistry<&CodegenTriggerCrashReg>();
  return 0;
}();

// TargetPassConfig is reached both from places with a real Function/Module
// (and thus LLVMContext) in scope and from pipeline-construction-time code
// that only has a TargetMachine. Fall back to the process-wide default in
// the latter case; see TargetLoweringBase.cpp's getSched1Options for the
// same pattern and its rationale.
static const CodeGenSched1Options &getSched1Options(const LLVMContext *Ctx) {
  return Ctx ? Ctx->getOptions<CodeGenSched1Options>()
             : CodeGenSched1Options::Current;
}

// Same rationale as getSched1Options above, for the Sched2 registry.
static const CodeGenSched2Options &getSched2Options(const LLVMContext *Ctx) {
  return Ctx ? Ctx->getOptions<CodeGenSched2Options>()
             : CodeGenSched2Options::Current;
}

static std::optional<bool> getEnableIpra(const LLVMContext *Ctx) {
  return getSched1Options(Ctx).CGPASS_EnableIpra;
}

static std::optional<bool> getVerifyMachineinstrs(const LLVMContext *Ctx) {
  return getSched1Options(Ctx).CGPASS_VerifyMachineinstrs;
}

static bool
getDisableAtexitBasedGlobalDtorLowering(const LLVMContext *Ctx) {
  return getSched1Options(Ctx).CGPASS_DisableAtexitBasedGlobalDtorLowering;
}

static std::optional<bool> getDebugifyAndStripAllSafe(const LLVMContext *Ctx) {
  return getSched1Options(Ctx).CGPASS_DebugifyAndStripAllSafe;
}

static std::optional<bool>
getDebugifyCheckAndStripAllSafe(const LLVMContext *Ctx) {
  return getSched1Options(Ctx).CGPASS_DebugifyCheckAndStripAllSafe;
}

static std::optional<bool> getFastIsel(const LLVMContext *Ctx) {
  return getSched1Options(Ctx).CGPASS_FastIsel;
}

static std::optional<bool> getGlobalIsel(const LLVMContext *Ctx) {
  return getSched1Options(Ctx).CGPASS_GlobalIsel;
}

static bool getPrintAfterIsel(const LLVMContext *Ctx) {
  return getSched1Options(Ctx).CGPASS_PrintAfterIsel;
}

static bool getDisableExpandReductions(const LLVMContext *Ctx) {
  return getSched1Options(Ctx).CGPASS_DisableExpandReductions;
}

static bool getEnableGcEmptyBasicBlocks(const LLVMContext *Ctx) {
  return getSched1Options(Ctx).CGPASS_EnableGcEmptyBasicBlocks;
}

static std::string getFsProfileFile(const LLVMContext *Ctx) {
  return getSched1Options(Ctx).CGPASS_FsProfileFile;
}

static bool getDisablePostRa(const LLVMContext *Ctx) {
  return getSched1Options(Ctx).CGPASS_DisablePostRa;
}

static bool getDisableBranchFold(const LLVMContext *Ctx) {
  return getSched1Options(Ctx).CGPASS_DisableBranchFold;
}

static bool getDisableTailDuplicate(const LLVMContext *Ctx) {
  return getSched1Options(Ctx).CGPASS_DisableTailDuplicate;
}

static bool getDisableEarlyTaildup(const LLVMContext *Ctx) {
  return getSched1Options(Ctx).CGPASS_DisableEarlyTaildup;
}

static bool getDisableBlockPlacement(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_DisableBlockPlacement;
}

static bool getEnableBlockPlacementStats(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_EnableBlockPlacementStats;
}

static bool getDisableSsc(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_DisableSsc;
}

static bool getDisableMachineDce(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_DisableMachineDce;
}

static bool getDisableEarlyIfcvt(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_DisableEarlyIfcvt;
}

static bool getDisableMachineLicm(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_DisableMachineLicm;
}

static bool getDisableMachineCse(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_DisableMachineCse;
}

static bool getDisablePostraMachineLicm(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_DisablePostraMachineLicm;
}

static bool getDisableMachineSink(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_DisableMachineSink;
}

static bool getDisablePostraMachineSink(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_DisablePostraMachineSink;
}

static bool getDisableLsr(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_DisableLsr;
}

static bool getDisableConstantHoisting(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_DisableConstantHoisting;
}

static bool getDisableCgp(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_DisableCgp;
}

static bool getDisableCopyprop(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_DisableCopyprop;
}

static bool getDisablePartialLibcallInlining(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_DisablePartialLibcallInlining;
}

static bool getEnableImplicitNullChecks(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_EnableImplicitNullChecks;
}

static bool getPrintIselInput(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_PrintIselInput;
}

static bool getEnableGlobalMergeFunc(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_EnableGlobalMergeFunc;
}

static bool getDisableCfiFixup(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_DisableCfiFixup;
}

static bool getDisableRaFsprofileLoader(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_DisableRaFsprofileLoader;
}

static bool getDisableLayoutFsprofileLoader(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_DisableLayoutFsprofileLoader;
}

static std::string getFsRemappingFile(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_FsRemappingFile;
}

static bool getMischedPostra(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_MischedPostra;
}

static bool getEarlyLiveIntervals(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_EarlyLiveIntervals;
}

static bool getDisableReplaceWithVecLib(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_DisableReplaceWithVecLib;
}

static std::string getStartAfter(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_StartAfter;
}

static std::string getStartBefore(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_StartBefore;
}

static std::string getStopAfter(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_StopAfter;
}

static std::string getStopBefore(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_StopBefore;
}

static bool getEnableSplitMachineFunctions(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_EnableSplitMachineFunctions;
}

static bool getDisableSelectOptimize(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_DisableSelectOptimize;
}

static bool getSplitStaticData(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_SplitStaticData;
}

static bool getBasicBlockSectionMatchInfer(const LLVMContext *Ctx) {
  return getSched2Options(Ctx).CGPASS_BasicBlockSectionMatchInfer;
}

// Read each value from the override when a tool installed one, otherwise from
// the parsed options.

static std::string getStartAfterName(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->StartAfter : getStartAfter(Ctx);
}
static std::string getStartBeforeName(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->StartBefore : getStartBefore(Ctx);
}
static std::string getStopAfterName(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->StopAfter : getStopAfter(Ctx);
}
static std::string getStopBeforeName(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->StopBefore : getStopBefore(Ctx);
}

// For bool fields the override wins when true; the parsed value is the
// fallback.

static bool getEffectivePrintAfterISel(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->PrintAfterISel || getPrintAfterIsel(Ctx)
                     : getPrintAfterIsel(Ctx);
}
static std::optional<bool>
getEffectiveVerifyMachineCode(const LLVMContext *Ctx) {
  if (TPCOverride && TPCOverride->VerifyMachineCode)
    return TPCOverride->VerifyMachineCode;
  return getVerifyMachineinstrs(Ctx); // sink-forwarded cl::opt fallback
}
static std::optional<bool>
getEffectiveDebugifyAndStripAll(const LLVMContext *Ctx) {
  if (TPCOverride && TPCOverride->DebugifyAndStripAll)
    return TPCOverride->DebugifyAndStripAll;
  return getDebugifyAndStripAllSafe(Ctx);
}
static std::optional<bool>
getEffectiveDebugifyCheckAndStripAll(const LLVMContext *Ctx) {
  if (TPCOverride && TPCOverride->DebugifyCheckAndStripAll)
    return TPCOverride->DebugifyCheckAndStripAll;
  return getDebugifyCheckAndStripAllSafe(Ctx);
}
static bool
getEffectiveDisableReplaceWithVecLib(const LLVMContext *Ctx) {
  return TPCOverride ? getDisableReplaceWithVecLib(Ctx)
                     : getDisableReplaceWithVecLib(Ctx);
}
static bool
getEffectiveDisableExpandReductions(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->DisableExpandReductions ||
                           getDisableExpandReductions(Ctx)
                     : getDisableExpandReductions(Ctx);
}
static bool getEffectiveDisableSelectOptimize(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->DisableSelectOptimize ||
                           getDisableSelectOptimize(Ctx)
                     : getDisableSelectOptimize(Ctx);
}
static bool
getEffectiveEnableMachineFunctionSplitter(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->EnableMachineFunctionSplitter ||
                           getEnableSplitMachineFunctions(Ctx)
                     : getEnableSplitMachineFunctions(Ctx);
}
static bool getEffectiveSplitStaticData(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->SplitStaticData || getSplitStaticData(Ctx)
                     : getSplitStaticData(Ctx);
}
static bool getEffectiveEmitBBHash(const LLVMContext *Ctx) {
  bool Val = getSched2Options(Ctx).CGPASS_EmitBbHash;
  return TPCOverride ? TPCOverride->EmitBBHash || Val : Val;
}
static bool
getEffectiveBasicBlockSectionMatchInfer(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->BasicBlockSectionMatchInfer ||
                           getBasicBlockSectionMatchInfer(Ctx)
                     : getBasicBlockSectionMatchInfer(Ctx);
}
static bool getEffectiveDisableLSR(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->DisableLSR || getDisableLsr(Ctx)
                     : getDisableLsr(Ctx);
}
static bool
getEffectiveDisableConstantHoisting(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->DisableConstantHoisting ||
                           getDisableConstantHoisting(Ctx)
                     : getDisableConstantHoisting(Ctx);
}
static bool getEffectiveDisableCGP(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->DisableCGP || getDisableCgp(Ctx)
                     : getDisableCgp(Ctx);
}
static bool
getEffectiveDisablePartialLibcallInlining(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->DisablePartialLibcallInlining ||
                           getDisablePartialLibcallInlining(Ctx)
                     : getDisablePartialLibcallInlining(Ctx);
}
static bool getEffectivePrintISelInput(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->PrintISelInput || getPrintIselInput(Ctx)
                     : getPrintIselInput(Ctx);
}
static bool
getEffectiveDisableRAFSProfileLoader(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->DisableRAFSProfileLoader ||
                           getDisableRaFsprofileLoader(Ctx)
                     : getDisableRaFsprofileLoader(Ctx);
}
static bool
getEffectiveDisableLayoutFSProfileLoader(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->DisableLayoutFSProfileLoader ||
                           getDisableLayoutFsprofileLoader(Ctx)
                     : getDisableLayoutFsprofileLoader(Ctx);
}
static bool
getEffectiveEnableImplicitNullChecks(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->EnableImplicitNullChecks ||
                           getEnableImplicitNullChecks(Ctx)
                     : getEnableImplicitNullChecks(Ctx);
}
static bool getEffectiveMISchedPostRA(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->MISchedPostRA || getMischedPostra(Ctx)
                     : getMischedPostra(Ctx);
}
static bool getEffectiveEnableGCEmptyBlocks(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->EnableGCEmptyBlocks ||
                           getEnableGcEmptyBasicBlocks(Ctx)
                     : getEnableGcEmptyBasicBlocks(Ctx);
}
static bool getEffectiveEarlyLiveIntervals(const LLVMContext *Ctx) {
  return TPCOverride
             ? TPCOverride->EarlyLiveIntervals || getEarlyLiveIntervals(Ctx)
             : getEarlyLiveIntervals(Ctx);
}
static bool
getEffectiveEnableBlockPlacementStats(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->EnableBlockPlacementStats ||
                           getEnableBlockPlacementStats(Ctx)
                     : getEnableBlockPlacementStats(Ctx);
}
static bool getEffectiveDisableCFIFixup(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->DisableCFIFixup || getDisableCfiFixup(Ctx)
                     : getDisableCfiFixup(Ctx);
}
static RunOutliner getEnableMachineOutlinerOption(const LLVMContext *Ctx) {
  // CGRunOutliner's enumerators match llvm::RunOutliner's exactly
  // (TargetDefault=0, AlwaysOutline=1, OptimisticPGO=2, ConservativePGO=3,
  // NeverOutline=4), so the cast is safe.
  return static_cast<RunOutliner>(
      getSched1Options(Ctx).CGPASS_EnableMachineOutliner);
}
// CGGlobalISelAbortMode has an extra leading TargetDefault enumerator that
// llvm::GlobalISelAbortMode lacks, so the two enums are NOT value-compatible
// -- map explicitly rather than casting. Returns std::nullopt when the
// option was left at its target-default (i.e. not specified).
static std::optional<GlobalISelAbortMode>
getGlobalIselAbortMode(const LLVMContext *Ctx) {
  switch (getSched1Options(Ctx).CGPASS_GlobalISelAbort) {
  case CGGlobalISelAbortMode::TargetDefault:
    return std::nullopt;
  case CGGlobalISelAbortMode::Disable:
    return GlobalISelAbortMode::Disable;
  case CGGlobalISelAbortMode::Enable:
    return GlobalISelAbortMode::Enable;
  case CGGlobalISelAbortMode::DisableWithDiag:
    return GlobalISelAbortMode::DisableWithDiag;
  }
  llvm_unreachable("all CGGlobalISelAbortMode cases handled");
}
static RunOutliner
getEffectiveEnableMachineOutliner(const LLVMContext *Ctx) {
  if (!TPCOverride)
    return RunOutliner(getEnableMachineOutlinerOption(Ctx));
  // If TPCOverride has a non-default (non-TargetDefault) value, use it;
  // otherwise fall through to the parsed value (which may have come via Sink).
  if (TPCOverride->EnableMachineOutliner != RunOutliner::TargetDefault)
    return TPCOverride->EnableMachineOutliner;
  return RunOutliner(getEnableMachineOutlinerOption(Ctx));
}
static bool getEffectiveEnableGlobalMergeFunc(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->EnableGlobalMergeFunc ||
                           getEnableGlobalMergeFunc(Ctx)
                     : getEnableGlobalMergeFunc(Ctx);
}
static bool getEffectiveDisableAtExitBasedGlobalDtorLowering(
    const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->DisableAtExitBasedGlobalDtorLowering ||
                           getDisableAtexitBasedGlobalDtorLowering(Ctx)
                     : getDisableAtexitBasedGlobalDtorLowering(Ctx);
}
static bool getEffectiveDisablePostRASched(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->DisablePostRASched || getDisablePostRa(Ctx)
                     : getDisablePostRa(Ctx);
}
static bool getEffectiveDisableBranchFold(const LLVMContext *Ctx) {
  return TPCOverride
             ? TPCOverride->DisableBranchFold || getDisableBranchFold(Ctx)
             : getDisableBranchFold(Ctx);
}
static bool getEffectiveDisableTailDuplicate(const LLVMContext *Ctx) {
  return TPCOverride
             ? TPCOverride->DisableTailDuplicate || getDisableTailDuplicate(Ctx)
             : getDisableTailDuplicate(Ctx);
}
static bool getEffectiveDisableEarlyTailDup(const LLVMContext *Ctx) {
  return TPCOverride
             ? TPCOverride->DisableEarlyTailDup || getDisableEarlyTaildup(Ctx)
             : getDisableEarlyTaildup(Ctx);
}
static bool getEffectiveDisableBlockPlacement(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->DisableBlockPlacement ||
                           getDisableBlockPlacement(Ctx)
                     : getDisableBlockPlacement(Ctx);
}
static bool getEffectiveDisableSSC(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->DisableSSC || getDisableSsc(Ctx)
                     : getDisableSsc(Ctx);
}
static bool getEffectiveDisableMachineDCE(const LLVMContext *Ctx) {
  return TPCOverride
             ? TPCOverride->DisableMachineDCE || getDisableMachineDce(Ctx)
             : getDisableMachineDce(Ctx);
}
static bool
getEffectiveDisableEarlyIfConversion(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->DisableEarlyIfConversion ||
                           getDisableEarlyIfcvt(Ctx)
                     : getDisableEarlyIfcvt(Ctx);
}
static bool getEffectiveDisableMachineLICM(const LLVMContext *Ctx) {
  return TPCOverride
             ? TPCOverride->DisableMachineLICM || getDisableMachineLicm(Ctx)
             : getDisableMachineLicm(Ctx);
}
static bool getEffectiveDisableMachineCSE(const LLVMContext *Ctx) {
  return TPCOverride
             ? TPCOverride->DisableMachineCSE || getDisableMachineCse(Ctx)
             : getDisableMachineCse(Ctx);
}
static bool
getEffectiveDisablePostRAMachineLICM(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->DisablePostRAMachineLICM ||
                           getDisablePostraMachineLicm(Ctx)
                     : getDisablePostraMachineLicm(Ctx);
}
static bool getEffectiveDisableMachineSink(const LLVMContext *Ctx) {
  return TPCOverride
             ? TPCOverride->DisableMachineSink || getDisableMachineSink(Ctx)
             : getDisableMachineSink(Ctx);
}
static bool
getEffectiveDisablePostRAMachineSink(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->DisablePostRAMachineSink ||
                           getDisablePostraMachineSink(Ctx)
                     : getDisablePostraMachineSink(Ctx);
}
static bool getEffectiveDisableCopyProp(const LLVMContext *Ctx) {
  return TPCOverride ? TPCOverride->DisableCopyProp || getDisableCopyprop(Ctx)
                     : getDisableCopyprop(Ctx);
}
static std::optional<bool> getEffectiveFastISelOption(const LLVMContext *Ctx) {
  if (TPCOverride && TPCOverride->EnableFastISelOption)
    return TPCOverride->EnableFastISelOption;
  return getFastIsel(Ctx);
}
static std::optional<bool>
getEffectiveGlobalISelOption(const LLVMContext *Ctx) {
  if (TPCOverride && TPCOverride->EnableGlobalISelOption)
    return TPCOverride->EnableGlobalISelOption;
  return getGlobalIsel(Ctx);
}

/// Allow standard passes to be disabled by command line options. This supports
/// simple binary flags that either suppress the pass or do nothing.
/// i.e. -disable-mypass=false has no effect.
/// These should be converted to boolOrDefault in order to use applyOverride.
static IdentifyingPassPtr applyDisable(IdentifyingPassPtr PassID,
                                       bool Override) {
  if (Override)
    return IdentifyingPassPtr();
  return PassID;
}

/// Allow standard passes to be disabled by the command line, regardless of who
/// is adding the pass.
///
/// StandardID is the pass identified in the standard pass pipeline and provided
/// to addPass(). It may be a target-specific ID in the case that the target
/// directly adds its own pass, but in that case we harmlessly fall through.
///
/// TargetID is the pass that the target has configured to override StandardID.
///
/// StandardID may be a pseudo ID. In that case TargetID is the name of the real
/// pass to run. This allows multiple options to control a single pass depending
/// on where in the pipeline that pass is added.
static IdentifyingPassPtr overridePass(AnalysisID StandardID,
                                       IdentifyingPassPtr TargetID,
                                       const LLVMContext *Sched1Ctx) {
  if (StandardID == &PostRASchedulerID)
    return applyDisable(TargetID, getEffectiveDisablePostRASched(Sched1Ctx));

  if (StandardID == &BranchFolderPassID)
    return applyDisable(TargetID, getEffectiveDisableBranchFold(Sched1Ctx));

  if (StandardID == &TailDuplicateLegacyID)
    return applyDisable(TargetID, getEffectiveDisableTailDuplicate(Sched1Ctx));

  if (StandardID == &EarlyTailDuplicateLegacyID)
    return applyDisable(TargetID, getEffectiveDisableEarlyTailDup(Sched1Ctx));

  if (StandardID == &MachineBlockPlacementID)
    return applyDisable(TargetID, getEffectiveDisableBlockPlacement(Sched1Ctx));

  if (StandardID == &StackSlotColoringID)
    return applyDisable(TargetID, getEffectiveDisableSSC(Sched1Ctx));

  if (StandardID == &DeadMachineInstructionElimID)
    return applyDisable(TargetID, getEffectiveDisableMachineDCE(Sched1Ctx));

  if (StandardID == &EarlyIfConverterLegacyID)
    return applyDisable(TargetID,
                        getEffectiveDisableEarlyIfConversion(Sched1Ctx));

  if (StandardID == &EarlyMachineLICMID)
    return applyDisable(TargetID, getEffectiveDisableMachineLICM(Sched1Ctx));

  if (StandardID == &MachineCSELegacyID)
    return applyDisable(TargetID, getEffectiveDisableMachineCSE(Sched1Ctx));

  if (StandardID == &MachineLICMID)
    return applyDisable(TargetID,
                        getEffectiveDisablePostRAMachineLICM(Sched1Ctx));

  if (StandardID == &MachineSinkingLegacyID)
    return applyDisable(TargetID, getEffectiveDisableMachineSink(Sched1Ctx));

  if (StandardID == &PostRAMachineSinkingID)
    return applyDisable(TargetID,
                        getEffectiveDisablePostRAMachineSink(Sched1Ctx));

  if (StandardID == &MachineCopyPropagationID)
    return applyDisable(TargetID, getEffectiveDisableCopyProp(Sched1Ctx));

  return TargetID;
}

// Find the FSProfile file name. The internal option takes the precedence
// before getting from TargetMachine.
static std::string getFSProfileFile(const TargetMachine *TM) {
  assert(TM && "TargetMachine must not be null");
  // Prefer the override; fall back to the parsed value. No Function/Module
  // is reachable here, so fall back to the process-wide default (see
  // getSched1Options).
  std::string FileName = (TPCOverride && !TPCOverride->FSProfileFile.empty())
                             ? TPCOverride->FSProfileFile
                             : getFsProfileFile(/*Ctx=*/nullptr);
  if (!FileName.empty())
    return FileName;
  const std::optional<PGOOptions> &PGOOpt = TM->getPGOOption();
  if (PGOOpt == std::nullopt || PGOOpt->Action != PGOOptions::SampleUse)
    return std::string();
  return PGOOpt->ProfileFile;
}

// Find the Profile remapping file name. The internal option takes the
// precedence before getting from TargetMachine.
static std::string getFSRemappingFile(const TargetMachine *TM) {
  assert(TM && "TargetMachine must not be null");
  // No Function/Module is reachable here, so fall back to the process-wide
  // default (see getSched1Options / getSched2Options).
  std::string FileName = (TPCOverride && !TPCOverride->FSRemappingFile.empty())
                             ? TPCOverride->FSRemappingFile
                             : getFsRemappingFile(/*Ctx=*/nullptr);
  if (!FileName.empty())
    return FileName;
  const std::optional<PGOOptions> &PGOOpt = TM->getPGOOption();
  if (PGOOpt == std::nullopt || PGOOpt->Action != PGOOptions::SampleUse)
    return std::string();
  return PGOOpt->ProfileRemappingFile;
}

//===---------------------------------------------------------------------===//
/// TargetPassConfig
//===---------------------------------------------------------------------===//

INITIALIZE_PASS(TargetPassConfig, "targetpassconfig",
                "Target Pass Configuration", false, false)
char TargetPassConfig::ID = 0;

namespace {

struct InsertedPass {
  AnalysisID TargetPassID;
  IdentifyingPassPtr InsertedPassID;

  InsertedPass(AnalysisID TargetPassID, IdentifyingPassPtr InsertedPassID)
      : TargetPassID(TargetPassID), InsertedPassID(InsertedPassID) {}

  Pass *getInsertedPass() const {
    assert(InsertedPassID.isValid() && "Illegal Pass ID!");
    if (InsertedPassID.isInstance())
      return InsertedPassID.getInstance();
    Pass *NP = Pass::createPass(InsertedPassID.getID());
    assert(NP && "Pass ID not registered");
    return NP;
  }
};

} // end anonymous namespace

namespace llvm {

class PassConfigImpl {
public:
  // List of passes explicitly substituted by this target. Normally this is
  // empty, but it is a convenient way to suppress or replace specific passes
  // that are part of a standard pass pipeline without overridding the entire
  // pipeline. This mechanism allows target options to inherit a standard pass's
  // user interface. For example, a target may disable a standard pass by
  // default by substituting a pass ID of zero, and the user may still enable
  // that standard pass with an explicit command line option.
  DenseMap<AnalysisID, IdentifyingPassPtr> TargetPasses;

  /// Store the pairs of <AnalysisID, AnalysisID> of which the second pass
  /// is inserted after each instance of the first one.
  SmallVector<InsertedPass, 4> InsertedPasses;
};

} // end namespace llvm

// Out of line virtual method.
TargetPassConfig::~TargetPassConfig() { delete Impl; }

static const PassInfo *getPassInfo(StringRef PassName) {
  if (PassName.empty())
    return nullptr;

  const PassRegistry &PR = *PassRegistry::getPassRegistry();
  const PassInfo *PI = PR.getPassInfo(PassName);
  if (!PI)
    reportFatalUsageError(Twine('\"') + Twine(PassName) +
                          Twine("\" pass is not registered."));
  return PI;
}

static AnalysisID getPassIDFromName(StringRef PassName) {
  const PassInfo *PI = getPassInfo(PassName);
  return PI ? PI->getTypeInfo() : nullptr;
}

static std::pair<StringRef, unsigned>
getPassNameAndInstanceNum(StringRef PassName) {
  StringRef Name, InstanceNumStr;
  std::tie(Name, InstanceNumStr) = PassName.split(',');

  unsigned InstanceNum = 0;
  if (!InstanceNumStr.empty() && InstanceNumStr.getAsInteger(10, InstanceNum))
    reportFatalUsageError("invalid pass instance specifier " + PassName);

  return std::make_pair(Name, InstanceNum);
}

void TargetPassConfig::setStartStopPasses() {
  // No Function/Module is reachable at pipeline-construction time; fall back
  // to the process-wide default (see getSched1Options / getSched2Options).
  const LLVMContext *Ctx = nullptr;
  std::string StartBeforeStr = getStartBeforeName(Ctx);
  StringRef StartBeforeName;
  std::tie(StartBeforeName, StartBeforeInstanceNum) =
      getPassNameAndInstanceNum(StartBeforeStr);

  std::string StartAfterStr = getStartAfterName(Ctx);
  StringRef StartAfterName;
  std::tie(StartAfterName, StartAfterInstanceNum) =
      getPassNameAndInstanceNum(StartAfterStr);

  std::string StopBeforeStr = getStopBeforeName(Ctx);
  StringRef StopBeforeName;
  std::tie(StopBeforeName, StopBeforeInstanceNum) =
      getPassNameAndInstanceNum(StopBeforeStr);

  std::string StopAfterStr = getStopAfterName(Ctx);
  StringRef StopAfterName;
  std::tie(StopAfterName, StopAfterInstanceNum) =
      getPassNameAndInstanceNum(StopAfterStr);

  StartBefore = getPassIDFromName(StartBeforeName);
  StartAfter = getPassIDFromName(StartAfterName);
  StopBefore = getPassIDFromName(StopBeforeName);
  StopAfter = getPassIDFromName(StopAfterName);
  if (StartBefore && StartAfter)
    reportFatalUsageError(Twine(StartBeforeOptName) + Twine(" and ") +
                          Twine(StartAfterOptName) + Twine(" specified!"));
  if (StopBefore && StopAfter)
    reportFatalUsageError(Twine(StopBeforeOptName) + Twine(" and ") +
                          Twine(StopAfterOptName) + Twine(" specified!"));
  Started = (StartAfter == nullptr) && (StartBefore == nullptr);
}

CGPassBuilderOption
llvm::getCGPassBuilderOption(const clv2::OptionsContext &Ctx,
                              const LLVMContext *Sched1Ctx) {
  // Start from TPCOverride if a clv2-migrated tool called setTPCValues(), or
  // from a default-constructed struct otherwise.
  CGPassBuilderOption Opt = TPCOverride ? *TPCOverride : CGPassBuilderOption{};

  // std::optional<bool>/sentinel-enum Sched1 fields directly encode whether
  // the option was specified -- only overlay Opt when the field has a real
  // value, so an unspecified option leaves whatever TPCOverride/default
  // supplied untouched.
  if (auto V = getFastIsel(Sched1Ctx))
    Opt.EnableFastISelOption = *V;
  if (auto V = getGlobalIselAbortMode(Sched1Ctx))
    Opt.EnableGlobalISelAbort = *V;
  if (auto V = getGlobalIsel(Sched1Ctx))
    Opt.EnableGlobalISelOption = *V;
  if (auto V = getEnableIpra(Sched1Ctx))
    Opt.EnableIPRA = *V;
  if (auto V = getVerifyMachineinstrs(Sched1Ctx))
    Opt.VerifyMachineCode = *V;
  if (auto V = getDebugifyAndStripAllSafe(Sched1Ctx))
    Opt.DebugifyAndStripAll = *V;
  if (auto V = getDebugifyCheckAndStripAllSafe(Sched1Ctx))
    Opt.DebugifyCheckAndStripAll = *V;

  // Plain bool/string Sched1 fields have no per-option "specified" bit in the
  // new schema (their .td default matches CGPassBuilderOption's own
  // default), so overlay unconditionally -- same as the OR-in fields below.
  Opt.DisableAtExitBasedGlobalDtorLowering |=
      getDisableAtexitBasedGlobalDtorLowering(Sched1Ctx);
  Opt.DisableExpandReductions |= getDisableExpandReductions(Sched1Ctx);
  Opt.PrintAfterISel |= getPrintAfterIsel(Sched1Ctx);
  Opt.FSProfileFile = getFsProfileFile(Sched1Ctx);
  Opt.EnableGCEmptyBlocks |= getEnableGcEmptyBasicBlocks(Sched1Ctx);

  // Plain bool fields: OR-in so a true sink-forwarded value is never lost.
  // Sched2 has no context of its own in this signature; it shares Sched1Ctx
  // (see the class comment on getSched2Options).
  Opt.EarlyLiveIntervals |= getEarlyLiveIntervals(Sched1Ctx);
  Opt.EnableBlockPlacementStats |= getEnableBlockPlacementStats(Sched1Ctx);
  Opt.EnableGlobalMergeFunc |= getEnableGlobalMergeFunc(Sched1Ctx);
  Opt.EnableImplicitNullChecks |= getEnableImplicitNullChecks(Sched1Ctx);
  Opt.MISchedPostRA |= getMischedPostra(Sched1Ctx);
  Opt.DisableLSR |= getDisableLsr(Sched1Ctx);
  Opt.DisableConstantHoisting |= getDisableConstantHoisting(Sched1Ctx);
  Opt.DisableCGP |= getDisableCgp(Sched1Ctx);
  Opt.DisablePartialLibcallInlining |=
      getDisablePartialLibcallInlining(Sched1Ctx);
  Opt.DisableSelectOptimize |= getDisableSelectOptimize(Sched1Ctx);
  Opt.PrintISelInput |= getPrintIselInput(Sched1Ctx);
  Opt.PrintRegUsage |= getSched2Options(Sched1Ctx).CGPASS_PrintRegusage;
  Opt.DisableRAFSProfileLoader |= getDisableRaFsprofileLoader(Sched1Ctx);
  Opt.DisableLayoutFSProfileLoader |=
      getDisableLayoutFsprofileLoader(Sched1Ctx);
  Opt.DisableCFIFixup |= getDisableCfiFixup(Sched1Ctx);
  Opt.EnableMachineFunctionSplitter |=
      getEnableSplitMachineFunctions(Sched1Ctx);
  Opt.DisablePostRASched |= getDisablePostRa(Sched1Ctx);
  Opt.DisableBranchFold |= getDisableBranchFold(Sched1Ctx);
  Opt.DisableTailDuplicate |= getDisableTailDuplicate(Sched1Ctx);
  Opt.DisableEarlyTailDup |= getDisableEarlyTaildup(Sched1Ctx);
  Opt.DisableBlockPlacement |= getDisableBlockPlacement(Sched1Ctx);
  Opt.DisableSSC |= getDisableSsc(Sched1Ctx);
  Opt.DisableMachineDCE |= getDisableMachineDce(Sched1Ctx);
  Opt.DisableEarlyIfConversion |= getDisableEarlyIfcvt(Sched1Ctx);
  Opt.DisableMachineLICM |= getDisableMachineLicm(Sched1Ctx);
  Opt.DisableMachineCSE |= getDisableMachineCse(Sched1Ctx);
  Opt.DisablePostRAMachineLICM |= getDisablePostraMachineLicm(Sched1Ctx);
  Opt.DisableMachineSink |= getDisableMachineSink(Sched1Ctx);
  Opt.DisablePostRAMachineSink |= getDisablePostraMachineSink(Sched1Ctx);
  Opt.DisableCopyProp |= getDisableCopyprop(Sched1Ctx);
  Opt.SplitStaticData |= getSplitStaticData(Sched1Ctx);
  Opt.BasicBlockSectionMatchInfer |=
      getBasicBlockSectionMatchInfer(Sched1Ctx);
  Opt.EmitBBHash |= getSched2Options(Sched1Ctx).CGPASS_EmitBbHash;

  // EnableMachineOutliner is an enum, not a bool: only override when the
  // parsed value is not the target default.
  {
    auto OutlinerVal = getEnableMachineOutlinerOption(Sched1Ctx);
    if (OutlinerVal != RunOutliner::TargetDefault)
      Opt.EnableMachineOutliner = OutlinerVal;
  }

  // Start/stop pipeline control options: prefer TPCOverride if set.
  if (!TPCOverride) {
    Opt.StartAfter = getStartAfter(Sched1Ctx);
    Opt.StartBefore = getStartBefore(Sched1Ctx);
    Opt.StopAfter = getStopAfter(Sched1Ctx);
    Opt.StopBefore = getStopBefore(Sched1Ctx);
  }

  return Opt;
}

void llvm::registerCodeGenCallback(PassInstrumentationCallbacks &PIC,
                                   TargetMachine &TM) {

  // Register a callback for disabling passes.
  const clv2::OptionsContext *Ctx = &TM.getOptionsContext();
  PIC.registerShouldRunOptionalPassCallback(
      [Ctx](StringRef P, IRUnitRef) { return true; });
}

Expected<TargetPassConfig::StartStopInfo>
TargetPassConfig::getStartStopInfo(PassInstrumentationCallbacks &PIC,
                                   const LLVMContext *Ctx) {
  std::string StartBeforeStr = getStartBeforeName(Ctx);
  auto [StartBefore, StartBeforeInstanceNum] =
      getPassNameAndInstanceNum(StartBeforeStr);
  std::string StartAfterStr = getStartAfterName(Ctx);
  auto [StartAfter, StartAfterInstanceNum] =
      getPassNameAndInstanceNum(StartAfterStr);
  std::string StopBeforeStr = getStopBeforeName(Ctx);
  auto [StopBefore, StopBeforeInstanceNum] =
      getPassNameAndInstanceNum(StopBeforeStr);
  std::string StopAfterStr = getStopAfterName(Ctx);
  auto [StopAfter, StopAfterInstanceNum] =
      getPassNameAndInstanceNum(StopAfterStr);

  if (!StartBefore.empty() && !StartAfter.empty())
    return make_error<StringError>(
        Twine(StartBeforeOptName) + " and " + StartAfterOptName + " specified!",
        std::make_error_code(std::errc::invalid_argument));
  if (!StopBefore.empty() && !StopAfter.empty())
    return make_error<StringError>(
        Twine(StopBeforeOptName) + " and " + StopAfterOptName + " specified!",
        std::make_error_code(std::errc::invalid_argument));

  StartStopInfo Result;
  Result.StartPass = StartBefore.empty() ? StartAfter : StartBefore;
  Result.StopPass = StopBefore.empty() ? StopAfter : StopBefore;
  Result.StartInstanceNum =
      StartBefore.empty() ? StartAfterInstanceNum : StartBeforeInstanceNum;
  Result.StopInstanceNum =
      StopBefore.empty() ? StopAfterInstanceNum : StopBeforeInstanceNum;
  Result.StartAfter = !StartAfter.empty();
  Result.StopAfter = !StopAfter.empty();
  Result.StartInstanceNum += Result.StartInstanceNum == 0;
  Result.StopInstanceNum += Result.StopInstanceNum == 0;
  return Result;
}

// Out of line constructor provides default values for pass options and
// registers all common codegen passes.
TargetPassConfig::TargetPassConfig(TargetMachine &TM, PassManagerBase &PM)
    : ImmutablePass(ID), PM(&PM), TM(&TM) {
  Impl = new PassConfigImpl();

  PassRegistry &PR = *PassRegistry::getPassRegistry();
  // Register all target independent codegen passes to activate their PassIDs,
  // including this pass itself.
  initializeCodeGen(PR);

  initializeLibcallLoweringInfoWrapperPass(PR);

  // Also register alias analysis passes required by codegen passes.
  initializeBasicAAWrapperPassPass(PR);
  initializeAAResultsWrapperPassPass(PR);

  // No Function/Module is reachable at TargetPassConfig construction time;
  // fall back to the process-wide default (see getSched1Options).
  const LLVMContext *Ctx = nullptr;

  if (TPCOverride && TPCOverride->EnableIPRA) {
    TM.Options.EnableIPRA = *TPCOverride->EnableIPRA;
  } else if (auto V = getEnableIpra(Ctx)) {
    TM.Options.EnableIPRA = *V; // forwarded via Sink by clv2 tools
  } else {
    // If not explicitly specified, use target default.
    TM.Options.EnableIPRA |= TM.useIPRA();
  }
  if (TPCOverride && TPCOverride->EnableGlobalISelAbort) {
    TM.Options.GlobalISelAbort = *TPCOverride->EnableGlobalISelAbort;
  } else if (auto V = getGlobalIselAbortMode(Ctx)) {
    TM.Options.GlobalISelAbort = *V;
  }

  if (TM.Options.EnableIPRA)
    setRequiresCodeGenSCCOrder();

  setStartStopPasses();
}

CodeGenOptLevel TargetPassConfig::getOptLevel() const {
  return TM->getOptLevel();
}

/// Insert InsertedPassID pass after TargetPassID.
void TargetPassConfig::insertPass(AnalysisID TargetPassID,
                                  IdentifyingPassPtr InsertedPassID) {
  assert(((!InsertedPassID.isInstance() &&
           TargetPassID != InsertedPassID.getID()) ||
          (InsertedPassID.isInstance() &&
           TargetPassID != InsertedPassID.getInstance()->getPassID())) &&
         "Insert a pass after itself!");
  Impl->InsertedPasses.emplace_back(TargetPassID, InsertedPassID);
}

/// createPassConfig - Create a pass configuration object to be used by
/// addPassToEmitX methods for generating a pipeline of CodeGen passes.
///
/// Targets may override this to extend TargetPassConfig.
TargetPassConfig *
CodeGenTargetMachineImpl::createPassConfig(PassManagerBase &PM) {
  return new TargetPassConfig(*this, PM);
}

TargetPassConfig::TargetPassConfig() : ImmutablePass(ID) {
  reportFatalUsageError("trying to construct TargetPassConfig without a target "
                        "machine. Scheduling a CodeGen pass without a target "
                        "triple set?");
}

bool TargetPassConfig::willCompleteCodeGenPipeline(const LLVMContext *Ctx) {
  return getStopBeforeName(Ctx).empty() && getStopAfterName(Ctx).empty();
}

bool TargetPassConfig::hasLimitedCodeGenPipeline(const LLVMContext *Ctx) {
  return !getStartBeforeName(Ctx).empty() || !getStartAfterName(Ctx).empty() ||
         !getStopBeforeName(Ctx).empty() || !getStopAfterName(Ctx).empty();
}

std::string
TargetPassConfig::getLimitedCodeGenPipelineReason(const LLVMContext *Ctx) {
  if (!hasLimitedCodeGenPipeline(Ctx))
    return std::string();
  std::string Res;
  std::string PassNames[] = {getStartAfterName(Ctx), getStartBeforeName(Ctx),
                             getStopAfterName(Ctx), getStopBeforeName(Ctx)};
  static const char *OptNames[] = {StartAfterOptName, StartBeforeOptName,
                                   StopAfterOptName, StopBeforeOptName};
  bool IsFirst = true;
  for (int Idx = 0; Idx < 4; ++Idx)
    if (!PassNames[Idx].empty()) {
      if (!IsFirst)
        Res += " and ";
      IsFirst = false;
      Res += OptNames[Idx];
    }
  return Res;
}

// Helper to verify the analysis is really immutable.
void TargetPassConfig::setOpt(bool &Opt, bool Val) {
  assert(!Initialized && "PassConfig is immutable");
  Opt = Val;
}

void TargetPassConfig::substitutePass(AnalysisID StandardID,
                                      IdentifyingPassPtr TargetID) {
  Impl->TargetPasses[StandardID] = TargetID;
}

IdentifyingPassPtr TargetPassConfig::getPassSubstitution(AnalysisID ID) const {
  DenseMap<AnalysisID, IdentifyingPassPtr>::const_iterator I =
      Impl->TargetPasses.find(ID);
  if (I == Impl->TargetPasses.end())
    return ID;
  return I->second;
}

bool TargetPassConfig::isPassSubstitutedOrOverridden(AnalysisID ID) const {
  IdentifyingPassPtr TargetID = getPassSubstitution(ID);
  // No Function/Module is reachable at pipeline-construction time; fall back
  // to the process-wide default (see getSched1Options).
  IdentifyingPassPtr FinalPtr =
      overridePass(ID, TargetID, /*Sched1Ctx=*/nullptr);
  return !FinalPtr.isValid() || FinalPtr.isInstance() || FinalPtr.getID() != ID;
}

/// Add a pass to the PassManager if that pass is supposed to be run.  If the
/// Started/Stopped flags indicate either that the compilation should start at
/// a later pass or that it should stop after an earlier pass, then do not add
/// the pass.  Finally, compare the current pass against the StartAfter
/// and StopAfter options and change the Started/Stopped flags accordingly.
void TargetPassConfig::addPass(Pass *P) {
  assert(!Initialized && "PassConfig is immutable");

  // Cache the Pass ID here in case the pass manager finds this pass is
  // redundant with ones already scheduled / available, and deletes it.
  // Fundamentally, once we add the pass to the manager, we no longer own it
  // and shouldn't reference it.
  AnalysisID PassID = P->getPassID();

  // Must precede PM->add(P): scheduling calls getAnalysisUsage(), which for
  // some passes consults an option (e.g. -aggressive-ext-opt) to decide what
  // to require.  Reading that through an empty context there while the pass
  // body reads the real one leaves the two disagreeing.
  P->setOptionsContext(TM->getOptionsContext());

  if (StartBefore == PassID && StartBeforeCount++ == StartBeforeInstanceNum)
    Started = true;
  if (StopBefore == PassID && StopBeforeCount++ == StopBeforeInstanceNum)
    Stopped = true;
  if (Started && !Stopped) {
    if (AddingMachinePasses) {
      // Construct banner message before PM->add() as that may delete the pass.
      std::string Banner =
          std::string("After ") + std::string(P->getPassName());
      addMachinePrePasses();
      PM->add(P);
      addMachinePostPasses(Banner);
    } else {
      PM->add(P);
    }

    // Add the passes after the pass P if there is any.
    for (const auto &IP : Impl->InsertedPasses)
      if (IP.TargetPassID == PassID)
        addPass(IP.getInsertedPass());
  } else {
    delete P;
  }

  if (StopAfter == PassID && StopAfterCount++ == StopAfterInstanceNum)
    Stopped = true;

  if (StartAfter == PassID && StartAfterCount++ == StartAfterInstanceNum)
    Started = true;
  if (Stopped && !Started)
    reportFatalUsageError("Cannot stop compilation after pass that is not run");
}

/// Add a CodeGen pass at this point in the pipeline after checking for target
/// and command line overrides.
///
/// addPass cannot return a pointer to the pass instance because is internal the
/// PassManager and the instance we create here may already be freed.
AnalysisID TargetPassConfig::addPass(AnalysisID PassID) {
  IdentifyingPassPtr TargetID = getPassSubstitution(PassID);
  IdentifyingPassPtr FinalPtr =
      overridePass(PassID, TargetID, /*Sched1Ctx=*/nullptr);
  if (!FinalPtr.isValid())
    return nullptr;

  Pass *P;
  if (FinalPtr.isInstance())
    P = FinalPtr.getInstance();
  else {
    P = Pass::createPass(FinalPtr.getID());
    if (!P)
      llvm_unreachable("Pass ID not registered");
  }
  AnalysisID FinalID = P->getPassID();
  addPass(P); // Ends the lifetime of P.

  return FinalID;
}

void TargetPassConfig::printAndVerify(const std::string &Banner) {
  addPrintPass(Banner);
  addVerifyPass(Banner);
}

void TargetPassConfig::addPrintPass(const std::string &Banner) {
  // No Function/Module is reachable here; fall back to the process-wide
  // default (see getSched1Options).
  if (getEffectivePrintAfterISel(/*Sched1Ctx=*/nullptr))
    PM->add(createMachineFunctionPrinterPass(dbgs(), Banner));
}

void TargetPassConfig::addVerifyPass(const std::string &Banner) {
  // No Function/Module is reachable here; fall back to the process-wide
  // default (see getSched1Options).
  std::optional<bool> VerifyOpt =
      getEffectiveVerifyMachineCode(/*Sched1Ctx=*/nullptr);
  bool Verify = VerifyOpt.value_or(false);
#ifdef EXPENSIVE_CHECKS
  if (!VerifyOpt)
    Verify = TM->isMachineVerifierClean();
#endif
  if (Verify)
    PM->add(createMachineVerifierPass(Banner));
}

void TargetPassConfig::addDebugifyPass() {
  PM->add(createDebugifyMachineModulePass());
}

void TargetPassConfig::addStripDebugPass() {
  PM->add(createStripDebugMachineModuleLegacyPass(/*OnlyDebugified=*/true));
}

void TargetPassConfig::addCheckDebugPass() {
  PM->add(createCheckDebugMachineModuleLegacyPass());
}

void TargetPassConfig::addMachinePrePasses(bool AllowDebugify) {
  // No Function/Module is reachable here; fall back to the process-wide
  // default (see getSched1Options).
  const LLVMContext *Sched1Ctx = nullptr;
  if (AllowDebugify && DebugifyIsSafe &&
      (getEffectiveDebugifyAndStripAll(Sched1Ctx).value_or(false) ||
       getEffectiveDebugifyCheckAndStripAll(Sched1Ctx).value_or(false)))
    addDebugifyPass();
}

void TargetPassConfig::addMachinePostPasses(const std::string &Banner) {
  // No Function/Module is reachable here; fall back to the process-wide
  // default (see getSched1Options).
  const LLVMContext *Sched1Ctx = nullptr;
  if (DebugifyIsSafe) {
    if (getEffectiveDebugifyCheckAndStripAll(Sched1Ctx).value_or(false)) {
      addCheckDebugPass();
      addStripDebugPass();
    } else if (getEffectiveDebugifyAndStripAll(Sched1Ctx).value_or(false))
      addStripDebugPass();
  }
  addVerifyPass(Banner);
}

/// Add common target configurable passes that perform LLVM IR to IR transforms
/// following machine independent optimization.
void TargetPassConfig::addIRPasses() {
  // Before running any passes, run the verifier to determine if the input
  // coming from the front-end and/or optimizer is valid.
  if (!DisableVerify)
    addPass(createVerifierPass());

  if (getOptLevel() != CodeGenOptLevel::None) {
    // Basic AliasAnalysis support.
    // Add TypeBasedAliasAnalysis before BasicAliasAnalysis so that
    // BasicAliasAnalysis wins if they disagree. This is intended to help
    // support "obvious" type-punning idioms.
    addPass(createTypeBasedAAWrapperPass());
    addPass(createScopedNoAliasAAWrapperPass());
    addPass(createBasicAAWrapperPass());

    // Run loop strength reduction before anything else.
    // No Function/Module is reachable here; fall back to the process-wide
    // default (see getSched2Options).
    if (!getEffectiveDisableLSR(/*Sched1Ctx=*/nullptr)) {
      addPass(createCanonicalizeFreezeInLoopsPass());
      addPass(createLoopStrengthReducePass());
      if (EnableLoopTermFold)
        addPass(createLoopTermFoldPass());
    }
  }

  // Run GC lowering passes for builtin collectors
  // TODO: add a pass insertion point here
  addPass(&GCLoweringID);
  addPass(&ShadowStackGCLoweringID);

  // For MachO, lower @llvm.global_dtors into @llvm.global_ctors with
  // __cxa_atexit() calls to avoid emitting the deprecated __mod_term_func.
  // No Function/Module is reachable here; fall back to the process-wide
  // default (see getSched1Options).
  if (TM->getTargetTriple().isOSBinFormatMachO() &&
      !getEffectiveDisableAtExitBasedGlobalDtorLowering(
          /*Sched1Ctx=*/nullptr))
    addPass(createLowerGlobalDtorsLegacyPass());

  // Make sure that no unreachable blocks are instruction selected.
  addPass(createUnreachableBlockEliminationPass());

  // Prepare expensive constants for SelectionDAG.
  // No Function/Module is reachable here; fall back to the process-wide
  // default (see getSched2Options).
  if (getOptLevel() != CodeGenOptLevel::None &&
      !getEffectiveDisableConstantHoisting(/*Sched1Ctx=*/nullptr))
    addPass(createConstantHoistingPass());

  if (getOptLevel() != CodeGenOptLevel::None &&
      !getEffectiveDisableReplaceWithVecLib(/*Sched1Ctx=*/nullptr))
    addPass(createReplaceWithVeclibLegacyPass());

  if (getOptLevel() != CodeGenOptLevel::None &&
      !getEffectiveDisablePartialLibcallInlining(/*Sched1Ctx=*/nullptr))
    addPass(createPartiallyInlineLibCallsPass());

  // Instrument function entry after all inlining.
  addPass(createPostInlineEntryExitInstrumenterPass());

  // Add scalarization of target's unsupported masked memory intrinsics pass.
  // the unsupported intrinsic will be replaced with a chain of basic blocks,
  // that stores/loads element one-by-one if the appropriate mask bit is set.
  addPass(createScalarizeMaskedMemIntrinLegacyPass());

  // Expand reduction intrinsics into shuffle sequences if the target wants to.
  // Allow disabling it for testing purposes.
  // No Function/Module is reachable here; fall back to the process-wide
  // default (see getSched1Options).
  if (!getEffectiveDisableExpandReductions(/*Sched1Ctx=*/nullptr))
    addPass(createExpandReductionsPass());

  // Convert conditional moves to conditional jumps when profitable.
  // No Function/Module is reachable here; fall back to the process-wide
  // default (see getSched2Options).
  if (getOptLevel() != CodeGenOptLevel::None &&
      !getEffectiveDisableSelectOptimize(/*Sched1Ctx=*/nullptr))
    addPass(createSelectOptimizePass());

  if (getEffectiveEnableGlobalMergeFunc(/*Sched1Ctx=*/nullptr))
    addPass(createGlobalMergeFuncPass());

  if (TM->getTargetTriple().isOSWindows())
    addPass(createWindowsSecureHotPatchingPass());
}

/// Turn exception handling constructs into something the code generators can
/// handle.
void TargetPassConfig::addPassesToHandleExceptions() {
  const MCAsmInfo &MCAI = TM->getMCAsmInfo();
  switch (MCAI.getExceptionHandlingType()) {
  case ExceptionHandling::SjLj:
    // SjLj piggy-backs on dwarf for this bit. The cleanups done apply to both
    // Dwarf EH prepare needs to be run after SjLj prepare. Otherwise,
    // catch info can get misplaced when a selector ends up more than one block
    // removed from the parent invoke(s). This could happen when a landing
    // pad is shared by multiple invokes and is also a target of a normal
    // edge from elsewhere.
    addPass(createSjLjEHPreparePass(TM));
    [[fallthrough]];
  case ExceptionHandling::DwarfCFI:
  case ExceptionHandling::ARM:
  case ExceptionHandling::AIX:
  case ExceptionHandling::ZOS:
    addPass(createDwarfEHPass(getOptLevel()));
    break;
  case ExceptionHandling::WinEH:
    // We support using both GCC-style and MSVC-style exceptions on Windows, so
    // add both preparation passes. Each pass will only actually run if it
    // recognizes the personality function.
    addPass(createWinEHPass());
    addPass(createDwarfEHPass(getOptLevel()));
    break;
  case ExceptionHandling::Wasm:
    // Wasm EH uses Windows EH instructions, but it does not need to demote PHIs
    // on catchpads and cleanuppads because it does not outline them into
    // funclets. Catchswitch blocks are not lowered in SelectionDAG, so we
    // should remove PHIs there.
    addPass(createWinEHPass(/*DemoteCatchSwitchPHIOnly=*/true));
    addPass(createWasmEHPass());
    break;
  case ExceptionHandling::Default:
  case ExceptionHandling::None:
  case ExceptionHandling::Emscripten:
    // Emscripten EH is lowered earlier by WebAssemblyLowerEmscriptenEHSjLj, so
    // by this point it needs no generic EH preparation, like the None case.
    addPass(createLowerInvokePass());

    // The lower invoke pass may create unreachable code. Remove it.
    addPass(createUnreachableBlockEliminationPass());
    break;
  }
}

/// Add pass to prepare the LLVM IR for code generation. This should be done
/// before exception handling preparation passes.
void TargetPassConfig::addCodeGenPrepare() {
  // No Function/Module is reachable here; fall back to the process-wide
  // default (see getSched2Options).
  if (getOptLevel() != CodeGenOptLevel::None &&
      !getEffectiveDisableCGP(/*Sched1Ctx=*/nullptr))
    addPass(createCodeGenPrepareLegacyPass());
}

/// Add common passes that perform LLVM IR to IR transforms in preparation for
/// instruction selection.
void TargetPassConfig::addISelPrepare() {
  addPreISel();

  // Force codegen to run according to the callgraph.
  if (requiresCodeGenSCCOrder())
    addPass(new DummyCGSCCPass);

  addPass(createInlineAsmPreparePass());

  // Add both the safe stack and the stack protection passes: each of them will
  // only protect functions that have corresponding attributes.
  addPass(createSafeStackPass());
  addPass(createStackProtectorPass());

  // No Function/Module is reachable here; fall back to the process-wide
  // default (see getSched2Options).
  if (getEffectivePrintISelInput(/*Sched1Ctx=*/nullptr))
    addPass(createPrintFunctionPass(
        dbgs(), "\n\n*** Final LLVM Code input to ISel ***\n"));

  // All passes which modify the LLVM IR are now complete; run the verifier
  // to ensure that the IR is valid.
  if (!DisableVerify)
    addPass(createVerifierPass());
}

bool TargetPassConfig::addCoreISelPasses() {
  // Enable FastISel with -fast-isel, but allow that to be overridden.
  // Use getEffective*() so clv2-migrated tools (which set TPCOverride via
  // setTPCValues()) take effect even on the legacy pass manager path.
  // No Function/Module is reachable here; fall back to the process-wide
  // default (see getSched1Options).
  std::optional<bool> EffFastISel =
      getEffectiveFastISelOption(/*Sched1Ctx=*/nullptr);
  std::optional<bool> EffGlobalISel =
      getEffectiveGlobalISelOption(/*Sched1Ctx=*/nullptr);
  TM->setO0WantsFastISel(EffFastISel != false);

  // Determine an instruction selector.
  enum class SelectorType { SelectionDAG, FastISel, GlobalISel };
  SelectorType Selector;

  if (EffFastISel == true)
    Selector = SelectorType::FastISel;
  else if (EffGlobalISel == true ||
           (TM->Options.EnableGlobalISel && EffGlobalISel != false))
    Selector = SelectorType::GlobalISel;
  else if (TM->getOptLevel() == CodeGenOptLevel::None &&
           TM->getO0WantsFastISel())
    Selector = SelectorType::FastISel;
  else
    Selector = SelectorType::SelectionDAG;

  // Set consistently TM->Options.EnableFastISel and EnableGlobalISel.
  if (Selector == SelectorType::FastISel) {
    TM->setFastISel(true);
    TM->setGlobalISel(false);
  } else if (Selector == SelectorType::GlobalISel) {
    TM->setFastISel(false);
    TM->setGlobalISel(true);
  }

  // FIXME: Injecting into the DAGISel pipeline seems to cause issues with
  //        analyses needing to be re-run. This can result in being unable to
  //        schedule passes (particularly with 'Function Alias Analysis
  //        Results'). It's not entirely clear why but AFAICT this seems to be
  //        due to one FunctionPassManager not being able to use analyses from a
  //        previous one. As we're injecting a ModulePass we break the usual
  //        pass manager into two. GlobalISel with the fallback path disabled
  //        and -run-pass seem to be unaffected. The majority of GlobalISel
  //        testing uses -run-pass so this probably isn't too bad.
  SaveAndRestore SavedDebugifyIsSafe(DebugifyIsSafe);
  if (Selector != SelectorType::GlobalISel || !isGlobalISelAbortEnabled())
    DebugifyIsSafe = false;

  // Add instruction selector passes for global isel if enabled.
  if (Selector == SelectorType::GlobalISel) {
    SaveAndRestore SavedAddingMachinePasses(AddingMachinePasses, true);
    if (addIRTranslator())
      return true;

    addPreLegalizeMachineIR();

    if (addLegalizeMachineIR())
      return true;

    // Before running the register bank selector, ask the target if it
    // wants to run some passes.
    addPreRegBankSelect();

    if (addRegBankSelect())
      return true;

    addPreGlobalInstructionSelect();

    if (addGlobalInstructionSelect())
      return true;
  }

  // Pass to reset the MachineFunction if the ISel failed. Outside of the above
  // if so that the verifier is not added to it.
  if (Selector == SelectorType::GlobalISel)
    addPass(createResetMachineFunctionPass(
        reportDiagnosticWhenGlobalISelFallback(), isGlobalISelAbortEnabled()));

  // Run the SDAG InstSelector, providing a fallback path when we do not want to
  // abort on not-yet-supported input.
  if (Selector != SelectorType::GlobalISel || !isGlobalISelAbortEnabled())
    if (addInstSelector())
      return true;

  // Expand pseudo-instructions emitted by ISel. Don't run the verifier before
  // FinalizeISel.
  addPass(&FinalizeISelID);

  // Print the instruction selected machine code...
  printAndVerify("After Instruction Selection");

  return false;
}

bool TargetPassConfig::addISelPasses() {
  if (TM->useEmulatedTLS())
    addPass(createLowerEmuTLSPass());

  PM->add(createTargetTransformInfoWrapperPass(TM->getTargetIRAnalysis()));
  // ObjCARCContract operates on ObjC intrinsics and must run before
  // PreISelIntrinsicLowering.
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(createObjCARCContractPass());
  addPass(createPreISelIntrinsicLoweringPass());
  addPass(createExpandIRInstsPass(getOptLevel()));
  addIRPasses();

  if (clv2::getOptValOr<&CodegenTriggerCrashReg, &OI_CodegenTriggerCrash>(
          TM->getOptionsContext(), false))
    addPass(new TriggerCrashFunctionLegacyPass());

  addCodeGenPrepare();
  addPassesToHandleExceptions();
  addISelPrepare();

  return addCoreISelPasses();
}

/// -regalloc=... command line option.
static FunctionPass *useDefaultRegisterAllocator() { return nullptr; }

// Validates at parse time against the same registry resolveRegAlloc() walks.
// Without this an unknown name silently falls back to the default allocator,
// so a typo would change codegen with no diagnostic.  The registry is
// populated by file-scope statics, so it is complete before any parse.
static bool validateRegAlloc(const std::string &Value, StringRef OptName,
                             clv2::detail::ParseDiag &Diag) {
  if (Value.empty())
    return true;
  for (auto *I = RegisterRegAlloc::getList(); I; I = I->getNext())
    if (Value == I->getName())
      return true;
  return clv2::detail::rejectOptionValue(
      OptName, "Cannot find option named '" + Value + "'!", Diag);
}

static constexpr clv2::OptionInfo<std::string> OI_RegAlloc{
    "regalloc", "Register allocator to use", clv2::Hidden,
    clv2::Validate<std::string>{&validateRegAlloc}};
static constexpr clv2::OptionsRegistry<&OI_RegAlloc> RegAllocOptReg;
static const int RegisterRegAllocDynamic = [] {
  clv2::registerDynamicRegistry<&RegAllocOptReg>();
  return 0;
}();

/// Resolve the register allocator from the options context.
static RegisterRegAlloc::FunctionPassCtor
resolveRegAlloc(const clv2::OptionsContext &Ctx) {
  std::string Name = clv2::getOptValIfSpecified<&RegAllocOptReg, &OI_RegAlloc>(
      Ctx, std::string{});
  if (Name.empty())
    return useDefaultRegisterAllocator;
  for (auto *I = RegisterRegAlloc::getList(); I; I = I->getNext())
    if (Name == I->getName())
      return I->getCtor();
  return useDefaultRegisterAllocator;
}

/// Add the complete set of target-independent postISel code generator passes.
///
/// This can be read as the standard order of major LLVM CodeGen stages. Stages
/// with nontrivial configuration or multiple passes are broken out below in
/// add%Stage routines.
///
/// Any TargetPassConfig::addXX routine may be overriden by the Target. The
/// addPre/Post methods with empty header implementations allow injecting
/// target-specific fixups just before or after major stages. Additionally,
/// targets have the flexibility to change pass order within a stage by
/// overriding default implementation of add%Stage routines below. Each
/// technique has maintainability tradeoffs because alternate pass orders are
/// not well supported. addPre/Post works better if the target pass is easily
/// tied to a common pass. But if it has subtle dependencies on multiple passes,
/// the target should override the stage instead.
///
/// TODO: We could use a single addPre/Post(ID) hook to allow pass injection
/// before/after any target-independent pass. But it's currently overkill.
void TargetPassConfig::addMachinePasses() {
  AddingMachinePasses = true;

  // Add passes that optimize machine instructions in SSA form.
  if (getOptLevel() != CodeGenOptLevel::None) {
    addMachineSSAOptimization();
  } else {
    // If the target requests it, assign local variables to stack slots relative
    // to one another and simplify frame index references where possible.
    addPass(&LocalStackSlotAllocationID);
  }

  if (TM->Options.EnableIPRA)
    addPass(createRegUsageInfoPropPass());

  // Run pre-ra passes.
  addPreRegAlloc();

  // Debugifying the register allocator passes seems to provoke some
  // non-determinism that affects CodeGen and there doesn't seem to be a point
  // where it becomes safe again so stop debugifying here.
  DebugifyIsSafe = false;

  // Add a FSDiscriminator pass right before RA, so that we could get
  // more precise SampleFDO profile for RA.
  if (getEnableFSDiscriminator(TM->getOptionsContext())) {
    addPass(createMIRAddFSDiscriminatorsPass(
        sampleprof::FSDiscriminatorPass::Pass1));
    const std::string ProfileFile = getFSProfileFile(TM);
    // No Function/Module is reachable here; fall back to the process-wide
    // default (see getSched2Options).
    if (!ProfileFile.empty() &&
        !getEffectiveDisableRAFSProfileLoader(/*Sched1Ctx=*/nullptr))
      addPass(createMIRProfileLoaderPass(ProfileFile, getFSRemappingFile(TM),
                                         sampleprof::FSDiscriminatorPass::Pass1,
                                         nullptr));
  }

  // Run register allocation and passes that are tightly coupled with it,
  // including phi elimination and scheduling.
  if (getOptimizeRegAlloc())
    addOptimizedRegAlloc();
  else
    addFastRegAlloc();

  // Run post-ra passes.
  addPostRegAlloc();

  addPass(&RemoveRedundantDebugValuesID);

  addPass(&FixupStatepointCallerSavedID);

  // Insert prolog/epilog code.  Eliminate abstract frame index references...
  if (getOptLevel() != CodeGenOptLevel::None) {
    addPass(&PostRAMachineSinkingID);
    addPass(&ShrinkWrapID);
  }

  // Prolog/Epilog inserter needs a TargetMachine to instantiate. But only
  // do so if it hasn't been disabled, substituted, or overridden.
  if (!isPassSubstitutedOrOverridden(&PrologEpilogCodeInserterID))
    addPass(createPrologEpilogInserterPass());

  /// Add passes that optimize machine instructions after register allocation.
  if (getOptLevel() != CodeGenOptLevel::None)
    addMachineLateOptimization();

  // Expand pseudo instructions before second scheduling pass.
  addPass(&ExpandPostRAPseudosID);

  // Run pre-sched2 passes.
  addPreSched2();

  // No Function/Module is reachable here; fall back to the process-wide
  // default (see getSched2Options).
  if (getEffectiveEnableImplicitNullChecks(/*Sched1Ctx=*/nullptr))
    addPass(&ImplicitNullChecksID);

  // Second pass scheduler.
  // Let Target optionally insert this pass by itself at some other
  // point.
  if (getOptLevel() != CodeGenOptLevel::None &&
      !TM->targetSchedulesPostRAScheduling()) {
    // No Function/Module is reachable here; fall back to the process-wide
    // default (see getSched2Options).
    if (getEffectiveMISchedPostRA(/*Sched1Ctx=*/nullptr))
      addPass(&PostMachineSchedulerID);
    else
      addPass(&PostRASchedulerID);
  }

  // GC
  addGCPasses();

  // Basic block placement.
  if (getOptLevel() != CodeGenOptLevel::None)
    addBlockPlacement();

  // Insert before XRay Instrumentation.
  addPass(&FEntryInserterID);

  addPass(&XRayInstrumentationID);
  addPass(&PatchableFunctionID);

  addPreEmitPass();

  if (TM->Options.EnableIPRA)
    // Collect register usage information and produce a register mask of
    // clobbered registers, to be used to optimize call sites.
    addPass(createRegUsageInfoCollector());

  // FIXME: Some backends are incompatible with running the verifier after
  // addPreEmitPass.  Maybe only pass "false" here for those targets?
  addPass(&FuncletLayoutID);

  addPass(&RemoveLoadsIntoFakeUsesID);
  addPass(&StackMapLivenessID);
  addPass(&LiveDebugValuesID);
  addPass(&MachineSanitizerBinaryMetadataID);

  // No Function/Module is reachable here; fall back to the process-wide
  // default (see getSched1Options).
  if (TM->Options.EnableMachineOutliner &&
      getOptLevel() != CodeGenOptLevel::None &&
      getEffectiveEnableMachineOutliner(/*Sched1Ctx=*/nullptr) !=
          RunOutliner::NeverOutline) {
    if (getEffectiveEnableMachineOutliner(/*Sched1Ctx=*/nullptr) !=
            RunOutliner::TargetDefault ||
        TM->Options.SupportsDefaultOutlining)
      addPass(createMachineOutlinerPass(
          getEffectiveEnableMachineOutliner(/*Sched1Ctx=*/nullptr)));
  }

  if (getEffectiveEnableGCEmptyBlocks(/*Sched1Ctx=*/nullptr))
    addPass(llvm::createGCEmptyBasicBlocksLegacyPass());

  if (getEnableFSDiscriminator(TM->getOptionsContext()))
    addPass(createMIRAddFSDiscriminatorsPass(
        sampleprof::FSDiscriminatorPass::PassLast));

  // No Function/Module is reachable here; fall back to the process-wide
  // default (see getSched2Options).
  if (TM->Options.EnableMachineFunctionSplitter ||
      getEffectiveEnableMachineFunctionSplitter(/*Sched1Ctx=*/nullptr) ||
      getEffectiveSplitStaticData(/*Sched1Ctx=*/nullptr) ||
      TM->Options.EnableStaticDataPartitioning) {
    const std::string ProfileFile = getFSProfileFile(TM);
    if (!ProfileFile.empty()) {
      if (getEnableFSDiscriminator(TM->getOptionsContext())) {
        addPass(createMIRProfileLoaderPass(
            ProfileFile, getFSRemappingFile(TM),
            sampleprof::FSDiscriminatorPass::PassLast, nullptr));
      } else {
        // Sample profile is given, but FSDiscriminator is not
        // enabled, this may result in performance regression.
        WithColor::warning()
            << "Using AutoFDO without FSDiscriminator for MFS may regress "
               "performance.\n";
      }
    }
  }

  // Machine function splitter uses the basic block sections feature.
  // When used along with `-basic-block-sections=`, the basic-block-sections
  // feature takes precedence. This means functions eligible for
  // basic-block-sections optimizations (`=all`, or `=list=` with function
  // included in the list profile) will get that optimization instead.
  // No Function/Module is reachable here; fall back to the process-wide
  // default (see getSched2Options).
  if (TM->Options.EnableMachineFunctionSplitter ||
      getEffectiveEnableMachineFunctionSplitter(/*Sched1Ctx=*/nullptr))
    addPass(createMachineFunctionSplitterPass());

  if (getEffectiveSplitStaticData(/*Sched1Ctx=*/nullptr) ||
      TM->Options.EnableStaticDataPartitioning) {
    // The static data splitter pass is a machine function pass. and
    // static data annotator pass is a module-wide pass. See the file comment
    // in StaticDataAnnotator.cpp for the motivation.
    addPass(createStaticDataSplitterLegacyPass());
    addPass(createStaticDataAnnotatorLegacyPass());
  }
  // We run the BasicBlockSections pass if either we need BB sections or BB
  // address map (or both).
  if (TM->getBBSectionsType() != llvm::BasicBlockSection::None ||
      TM->Options.BBAddrMap) {
    // No Function/Module is reachable here; fall back to the process-wide
    // default (see getSched2Options).
    if (getEffectiveEmitBBHash(/*Sched1Ctx=*/nullptr) ||
        getEffectiveBasicBlockSectionMatchInfer(/*Sched1Ctx=*/nullptr))
      addPass(llvm::createMachineBlockHashInfoPass());
    if (TM->getBBSectionsType() == llvm::BasicBlockSection::List) {
      addPass(llvm::createBasicBlockSectionsProfileReaderWrapperPass(
          TM->getBBSectionsFuncListBuf()));
      if (getEffectiveBasicBlockSectionMatchInfer(/*Sched1Ctx=*/nullptr))
        addPass(llvm::createBasicBlockMatchingAndInferencePass());
      else {
        addPass(llvm::createBasicBlockPathCloningPass());
        addPass(llvm::createInsertCodePrefetchPass());
      }
    }
    addPass(llvm::createBasicBlockSectionsPass());
  }

  addPostBBSections();

  // No Function/Module is reachable here; fall back to the process-wide
  // default (see getSched2Options).
  if (!getEffectiveDisableCFIFixup(/*Sched1Ctx=*/nullptr) &&
      TM->Options.EnableCFIFixup)
    addPass(createCFIFixupLegacy());

  PM->add(createStackFrameLayoutAnalysisPass());

  // Add passes that directly emit MI after all other MI passes.
  addPreEmitPass2();

  AddingMachinePasses = false;
}

/// Add passes that optimize machine instructions in SSA form.
void TargetPassConfig::addMachineSSAOptimization() {
  // Pre-ra tail duplication.
  addPass(&EarlyTailDuplicateLegacyID);

  // Optimize PHIs before DCE: removing dead PHI cycles may make more
  // instructions dead.
  addPass(&OptimizePHIsLegacyID);

  // This pass merges large allocas. StackSlotColoring is a different pass
  // which merges spill slots.
  addPass(&StackColoringLegacyID);

  // If the target requests it, assign local variables to stack slots relative
  // to one another and simplify frame index references where possible.
  addPass(&LocalStackSlotAllocationID);

  // With optimization, dead code should already be eliminated. However
  // there is one known exception: lowered code for arguments that are only
  // used by tail calls, where the tail calls reuse the incoming stack
  // arguments directly (see t11 in test/CodeGen/X86/sibcall.ll).
  addPass(&DeadMachineInstructionElimID);

  // Allow targets to insert passes that improve instruction level parallelism,
  // like if-conversion. Such passes will typically need dominator trees and
  // loop info, just like LICM and CSE below.
  addILPOpts();

  addPass(&EarlyMachineLICMID);
  addPass(&MachineCSELegacyID);

  addPass(&MachineSinkingLegacyID);

  addPass(&PeepholeOptimizerLegacyID);
  // Clean-up the dead code that may have been generated by peephole
  // rewriting.
  addPass(&DeadMachineInstructionElimID);
}

//===---------------------------------------------------------------------===//
/// Register Allocation Pass Configuration
//===---------------------------------------------------------------------===//

static RegisterRegAlloc
    defaultRegAlloc("default", "pick register allocator based on -O option",
                    useDefaultRegisterAllocator);

bool TargetPassConfig::getOptimizeRegAlloc() const {
  // An explicit -regalloc choice implies its pipeline: only the fast
  // allocator uses the unoptimized one.
  RegisterRegAlloc::FunctionPassCtor Ctor =
      resolveRegAlloc(TM->getOptionsContext());
  if (Ctor != (RegisterRegAlloc::FunctionPassCtor)&useDefaultRegisterAllocator)
    return Ctor !=
           (RegisterRegAlloc::FunctionPassCtor)&createFastRegisterAllocator;
  return getOptLevel() != CodeGenOptLevel::None;
}

/// Instantiate the default register allocator pass for this target for either
/// the optimized or unoptimized allocation path. This will be added to the pass
/// manager by addFastRegAlloc in the unoptimized case or addOptimizedRegAlloc
/// in the optimized case.
///
/// A target that uses the standard regalloc pass order for fast or optimized
/// allocation may still override this for per-target regalloc
/// selection. But -regalloc=... always takes precedence.
FunctionPass *TargetPassConfig::createTargetRegisterAllocator(bool Optimized) {
  if (Optimized)
    return createGreedyRegisterAllocator();
  else
    return createFastRegisterAllocator();
}

/// Find and instantiate the register allocation pass requested by this target
/// at the current optimization level.  Different register allocators are
/// defined as separate passes because they may require different analysis.
///
/// This helper ensures that the regalloc= option is always available,
/// even for targets that override the default allocator.
///
/// FIXME: When MachinePassRegistry register pass IDs instead of function ptrs,
/// this can be folded into addPass.
FunctionPass *TargetPassConfig::createRegAllocPass(bool Optimized) {
  RegisterRegAlloc::FunctionPassCtor Ctor =
      resolveRegAlloc(TM->getOptionsContext());
  if (Ctor != useDefaultRegisterAllocator)
    return Ctor();

  // With no -regalloc= override, ask the target for a regalloc pass.
  return createTargetRegisterAllocator(Optimized);
}

bool TargetPassConfig::isCustomizedRegAlloc() {
  return resolveRegAlloc(TM->getOptionsContext()) !=
         useDefaultRegisterAllocator;
}

bool TargetPassConfig::addRegAssignAndRewriteFast() {
  RegisterRegAlloc::FunctionPassCtor Ctor =
      resolveRegAlloc(TM->getOptionsContext());
  if (Ctor != useDefaultRegisterAllocator &&
      Ctor != (RegisterRegAlloc::FunctionPassCtor)&createFastRegisterAllocator)
    reportFatalUsageError(
        "Must use fast (default) register allocator for unoptimized regalloc.");

  addPass(createRegAllocPass(false));

  // Allow targets to change the register assignments after
  // fast register allocation.
  addPostFastRegAllocRewrite();
  return true;
}

bool TargetPassConfig::addRegAssignAndRewriteOptimized() {
  // Add the selected register allocation pass.
  addPass(createRegAllocPass(true));

  // Allow targets to change the register assignments before rewriting.
  addPreRewrite();

  // Finally rewrite virtual registers.
  addPass(&VirtRegRewriterID);

  // Regalloc scoring for ML-driven eviction - noop except when learning a new
  // eviction policy.
  addPass(createRegAllocScoringPass());
  return true;
}

/// Return true if the default global register allocator is in use and
/// has not be overriden on the command line with '-regalloc=...'
bool TargetPassConfig::usingDefaultRegAlloc() const {
  return !clv2::wasOptSpecified<&RegAllocOptReg, &OI_RegAlloc>(
      TM->getOptionsContext());
}

/// Add the minimum set of target-independent passes that are required for
/// register allocation. No coalescing or scheduling.
void TargetPassConfig::addFastRegAlloc() {
  addPass(&PHIEliminationID);
  addPass(&TwoAddressInstructionPassID);

  addRegAssignAndRewriteFast();
}

/// Add standard target-independent passes that are tightly coupled with
/// optimized register allocation, including coalescing, machine instruction
/// scheduling, and register allocation itself.
void TargetPassConfig::addOptimizedRegAlloc() {
  addPass(&DetectDeadLanesID);

  addPass(&InitUndefID);

  addPass(&ProcessImplicitDefsID);

  // LiveVariables currently requires pure SSA form.
  //
  // FIXME: Once TwoAddressInstruction pass no longer uses kill flags,
  // LiveVariables can be removed completely, and LiveIntervals can be directly
  // computed. (We still either need to regenerate kill flags after regalloc, or
  // preferably fix the scavenger to not depend on them).
  // FIXME: UnreachableMachineBlockElim is a dependant pass of LiveVariables.
  // When LiveVariables is removed this has to be removed/moved either.
  // Explicit addition of UnreachableMachineBlockElim allows stopping before or
  // after it with -stop-before/-stop-after.
  addPass(&UnreachableMachineBlockElimID);
  addPass(&LiveVariablesID);

  // Edge splitting is smarter with machine loop info.
  addPass(&MachineLoopInfoID);
  addPass(&PHIEliminationID);

  // Eventually, we want to run LiveIntervals before PHI elimination.
  // No Function/Module is reachable here; fall back to the process-wide
  // default (see getSched2Options).
  if (getEffectiveEarlyLiveIntervals(/*Sched1Ctx=*/nullptr))
    addPass(&LiveIntervalsID);

  addPass(&TwoAddressInstructionPassID);
  addPass(&RegisterCoalescerID);

  // The machine scheduler may accidentally create disconnected components
  // when moving subregister definitions around, avoid this by splitting them to
  // separate vregs before. Splitting can also improve reg. allocation quality.
  addPass(&RenameIndependentSubregsID);

  // PreRA instruction scheduling.
  addPass(&MachineSchedulerID);

  if (addRegAssignAndRewriteOptimized()) {
    // Perform stack slot coloring and post-ra machine LICM.
    addPass(&StackSlotColoringID);

    // Allow targets to expand pseudo instructions depending on the choice of
    // registers before MachineCopyPropagation.
    addPostRewrite();

    // Copy propagate to forward register uses and try to eliminate COPYs that
    // were not coalesced.
    addPass(&MachineCopyPropagationID);

    // Run post-ra machine LICM to hoist reloads / remats.
    //
    // FIXME: can this move into MachineLateOptimization?
    addPass(&MachineLICMID);
  }
}

//===---------------------------------------------------------------------===//
/// Post RegAlloc Pass Configuration
//===---------------------------------------------------------------------===//

/// Add passes that optimize machine instructions after register allocation.
void TargetPassConfig::addMachineLateOptimization() {
  // Cleanup of redundant immediate/address loads.
  addPass(&MachineLateInstrsCleanupID);

  // Branch folding must be run after regalloc and prolog/epilog insertion.
  addPass(&BranchFolderPassID);

  // Tail duplication.
  // Note that duplicating tail just increases code size and degrades
  // performance for targets that require Structured Control Flow.
  // In addition it can also make CFG irreducible. Thus we disable it.
  if (!TM->requiresStructuredCFG())
    addPass(&TailDuplicateLegacyID);

  // Copy propagation.
  addPass(&MachineCopyPropagationID);
}

/// Add standard GC passes.
bool TargetPassConfig::addGCPasses() {
  addPass(&GCMachineCodeAnalysisID);
  return true;
}

/// Add standard basic block placement passes.
void TargetPassConfig::addBlockPlacement() {
  if (getEnableFSDiscriminator(TM->getOptionsContext())) {
    addPass(createMIRAddFSDiscriminatorsPass(
        sampleprof::FSDiscriminatorPass::Pass2));
    const std::string ProfileFile = getFSProfileFile(TM);
    // No Function/Module is reachable here; fall back to the process-wide
    // default (see getSched2Options).
    if (!ProfileFile.empty() &&
        !getEffectiveDisableLayoutFSProfileLoader(/*Sched1Ctx=*/nullptr))
      addPass(createMIRProfileLoaderPass(ProfileFile, getFSRemappingFile(TM),
                                         sampleprof::FSDiscriminatorPass::Pass2,
                                         nullptr));
  }
  if (addPass(&MachineBlockPlacementID)) {
    // Run a separate pass to collect block placement statistics.
    // No Function/Module is reachable here; fall back to the process-wide
    // default (see getSched2Options).
    if (getEffectiveEnableBlockPlacementStats(/*Sched1Ctx=*/nullptr))
      addPass(&MachineBlockPlacementStatsID);
  }
}

//===---------------------------------------------------------------------===//
/// GlobalISel Configuration
//===---------------------------------------------------------------------===//
bool TargetPassConfig::isGlobalISelAbortEnabled() const {
  return TM->Options.GlobalISelAbort == GlobalISelAbortMode::Enable;
}

bool TargetPassConfig::reportDiagnosticWhenGlobalISelFallback() const {
  return TM->Options.GlobalISelAbort == GlobalISelAbortMode::DisableWithDiag;
}

std::unique_ptr<CSEConfigBase> TargetPassConfig::getCSEConfig() const {
  return std::make_unique<CSEConfigBase>();
}
