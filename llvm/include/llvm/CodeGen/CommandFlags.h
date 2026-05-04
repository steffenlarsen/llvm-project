//===-- CommandFlags.h - Command Line Flags Interface -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains codegen-specific flags that are shared between different
// command line tools. The tools "llc" and "opt" both use this file to prevent
// flag duplication.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_COMMANDFLAGS_H
#define LLVM_CODEGEN_COMMANDFLAGS_H

#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/CodeGen/SaveStatsMode.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Target/TargetOptions.h"
#include <optional>
#include <string>
#include <vector>

namespace llvm {

namespace clv2 {
class OptionsContext;
}

class Module;
class AttrBuilder;
class Function;
class Triple;
class TargetMachine;

namespace codegen {

LLVM_ABI std::string getMArch(const clv2::OptionsContext &Ctx);

LLVM_ABI std::string getMCPU(const clv2::OptionsContext &Ctx);

LLVM_ABI std::string getMTune(const clv2::OptionsContext &Ctx);

LLVM_ABI std::vector<std::string> getMAttrs(const clv2::OptionsContext &Ctx);

LLVM_ABI Reloc::Model getRelocModel(const clv2::OptionsContext &Ctx);
LLVM_ABI std::optional<Reloc::Model>
getExplicitRelocModel(const clv2::OptionsContext &Ctx);

LLVM_ABI ThreadModel::Model getThreadModel(const clv2::OptionsContext &Ctx);

LLVM_ABI CodeModel::Model getCodeModel(const clv2::OptionsContext &Ctx);
LLVM_ABI std::optional<CodeModel::Model>
getExplicitCodeModel(const clv2::OptionsContext &Ctx);

LLVM_ABI uint64_t getLargeDataThreshold(const clv2::OptionsContext &Ctx);
LLVM_ABI std::optional<uint64_t>
getExplicitLargeDataThreshold(const clv2::OptionsContext &Ctx);

LLVM_ABI llvm::ExceptionHandling
getExceptionModel(const clv2::OptionsContext &Ctx);

LLVM_ABI std::optional<CodeGenFileType>
getExplicitFileType(const clv2::OptionsContext &Ctx);

LLVM_ABI CodeGenFileType getFileType(const clv2::OptionsContext &Ctx);

LLVM_ABI FramePointerKind getFramePointerUsage(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getEnableNoSignedZerosFPMath(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getEnableNoTrappingFPMath(const clv2::OptionsContext &Ctx);

LLVM_ABI DenormalMode::DenormalModeKind
getDenormalFPMath(const clv2::OptionsContext &Ctx);
LLVM_ABI DenormalMode::DenormalModeKind
getDenormalFP32Math(const clv2::OptionsContext &Ctx);

LLVM_ABI bool
getEnableHonorSignDependentRoundingFPMath(const clv2::OptionsContext &Ctx);

LLVM_ABI llvm::FloatABI::ABIType
getFloatABIForCalls(const clv2::OptionsContext &Ctx);

LLVM_ABI llvm::FPOpFusion::FPOpFusionMode
getFuseFPOps(const clv2::OptionsContext &Ctx);

LLVM_ABI SwiftAsyncFramePointerMode
getSwiftAsyncFramePointer(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getDontPlaceZerosInBSS(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getEnableGuaranteedTailCallOpt(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getEnableAIXExtendedAltivecABI(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getDisableTailCalls(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getStackSymbolOrdering(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getStackRealign(const clv2::OptionsContext &Ctx);

LLVM_ABI std::string getTrapFuncName(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getUseCtors(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getDisableIntegratedAS(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getDataSections(const clv2::OptionsContext &Ctx);
LLVM_ABI std::optional<bool>
getExplicitDataSections(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getFunctionSections(const clv2::OptionsContext &Ctx);
LLVM_ABI std::optional<bool>
getExplicitFunctionSections(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getIgnoreXCOFFVisibility(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getXCOFFTracebackTable(const clv2::OptionsContext &Ctx);

LLVM_ABI std::string getBBSections(const clv2::OptionsContext &Ctx);

LLVM_ABI unsigned getTLSSize(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getEmulatedTLS(const clv2::OptionsContext &Ctx);
LLVM_ABI std::optional<bool>
getExplicitEmulatedTLS(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getEnableTLSDESC(const clv2::OptionsContext &Ctx);
LLVM_ABI std::optional<bool>
getExplicitEnableTLSDESC(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getUniqueSectionNames(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getUniqueBasicBlockSectionNames(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getSeparateNamedSections(const clv2::OptionsContext &Ctx);

LLVM_ABI llvm::EABI getEABIVersion(const clv2::OptionsContext &Ctx);

LLVM_ABI llvm::DebuggerKind
getDebuggerTuningOpt(const clv2::OptionsContext &Ctx);

LLVM_ABI llvm::VectorLibrary getVectorLibrary(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getEnableStackSizeSection(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getEnableAddrsig(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getEnableCallGraphSection(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getEmitCallSiteInfo(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getEnableMachineFunctionSplitter(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getEnableStaticDataPartitioning(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getEnableDebugEntryValues(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getForceDwarfFrameSection(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getXRayFunctionIndex(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getDebugStrictDwarf(const clv2::OptionsContext &Ctx);

LLVM_ABI unsigned getAlignLoops(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getJMCInstrument(const clv2::OptionsContext &Ctx);

LLVM_ABI bool getXCOFFReadOnlyPointers(const clv2::OptionsContext &Ctx);

LLVM_ABI SaveStatsMode getSaveStats(const clv2::OptionsContext &Ctx);

LLVM_ABI std::optional<std::string>
getBBSectionsColdTextPrefixOverride(const clv2::OptionsContext &Ctx);

} // namespace codegen
} // namespace llvm

#include "llvm/CodeGen/CommandFlagsOptInfos.h"

namespace llvm::codegen {

/// The parsed-options view type for the CodeGen library registry.
using ParsedOpts = decltype(clv2::CGOptsReg)::ParsedOptionsT;

/// Create this object with static storage to register codegen-related command
/// line options.
struct RegisterCodeGenFlags {
  LLVM_ABI RegisterCodeGenFlags();
};

/// Tools that support subtarget tuning should create this object with static
/// storage to register the -mtune command line option.
struct RegisterMTuneFlag {
  LLVM_ABI RegisterMTuneFlag();
};

/// Tools that support stats saving should create this object with static
/// storage to register the --save-stats command line option.
struct RegisterSaveStatsFlag {
  LLVM_ABI RegisterSaveStatsFlag();
};

LLVM_ABI bool getEnableBBAddrMap(const clv2::OptionsContext &Ctx);

LLVM_ABI llvm::BasicBlockSection
getBBSectionsMode(llvm::TargetOptions &Options,
                  const clv2::OptionsContext &Ctx);

/// Common utility function tightly tied to the options listed here. Initializes
/// a TargetOptions object with CodeGen flags and returns it.
/// \p TheTriple is used to determine the default value for options if
///    options are not explicitly specified. If those triple dependant options
///    value do not have effect for your component, a default Triple() could be
///    passed in.
LLVM_ABI TargetOptions InitTargetOptionsFromCodeGenFlags(
    const llvm::Triple &TheTriple, const clv2::OptionsContext &OptsCtx);

LLVM_ABI std::string getCPUStr(const clv2::OptionsContext &Ctx);

LLVM_ABI std::string getTuneCPUStr(const clv2::OptionsContext &Ctx);

LLVM_ABI std::string getFeaturesStr(const clv2::OptionsContext &Ctx);

LLVM_ABI std::vector<std::string>
getFeatureList(const clv2::OptionsContext &Ctx);

LLVM_ABI void renderBoolStringAttr(AttrBuilder &B, StringRef Name, bool Val);

/// Set function attributes of function \p F based on CPU, TuneCPU, Features,
/// and command line flags.
LLVM_ABI void setFunctionAttributes(Function &F, StringRef CPU,
                                    StringRef Features, StringRef TuneCPU = "");

/// Set function attributes of functions in Module M based on CPU,
/// TuneCPU, Features, and command line flags.
LLVM_ABI void setFunctionAttributes(Module &M, StringRef CPU,
                                    StringRef Features, StringRef TuneCPU = "");

/// Creates a TargetMachine instance with the options defined on the command
/// line. This can be used for tools that do not need further customization of
/// the TargetOptions.
LLVM_ABI Expected<std::unique_ptr<TargetMachine>> createTargetMachineForTriple(
    const Triple &TargetTriple, const clv2::OptionsContext &OptsCtx,
    CodeGenOptLevel OptLevel = CodeGenOptLevel::Default);

/// Conditionally enables the collection of LLVM statistics during the tool run,
/// based on the value of the flag. Must be called before the tool run to
/// actually collect data.
LLVM_ABI void MaybeEnableStatistics(const clv2::OptionsContext &Ctx);

/// Conditionally saves the collected LLVM statistics to the received output
/// file, based on the value of the flag. Should be called after the tool run,
/// and must follow a call to `MaybeEnableStatistics()` to actually have data to
/// write.
LLVM_ABI int MaybeSaveStatistics(StringRef OutputFilename, StringRef ToolName,
                                 const clv2::OptionsContext &Ctx);

} // namespace llvm::codegen

#endif // LLVM_CODEGEN_COMMANDFLAGS_H
