//===-- CommandFlags.cpp - Command Line Flags Interface ---------*- C++ -*-===//
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

#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/CommandFlagsOptInfos.h"
#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"
#include "llvm/CodeGen/MIR2Vec.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCTargetOptionsCommandFlags.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/TargetParser/Triple.h"
#include <cassert>
#include <memory>
#include <optional>
#include <system_error>
#include <type_traits>

using namespace llvm;

// Each getter reads the session's parsed options.  There is no process-wide
// snapshot, so two parses in one process see their own values and a second
// parse cannot inherit the first's.
// The value these getters produce when the option was not given: the
// descriptor's Init default, which is why these read through getOptValOr:
// it returns the parsed slot.
// getOptValIfSpecified would ignore the slot and hand back TY{}, silently
// flipping every option whose default is not the zero value (there are ~28,
// including -x86-relax-relocations and -unique-section-names).

// The value these getters must produce when the registry is absent from the
// context: the descriptor's Init default if it has one, else the zero value.
// This is what the old primed snapshot produced, and it matters --
// CG_BBSections defaults to "none", and TY{} ("") instead routes
// getBBSectionsMode() into the function-list branch and tries to open a file
// named "".

#define CGOPT_NAMED(TY, NAME, OPT)                                             \
  TY codegen::get##NAME(const clv2::OptionsContext &Ctx) {                     \
    return clv2::getOptValOr<&clv2::OPT>(                                      \
        Ctx, clv2::descriptorDefault<&clv2::OPT, TY>());                       \
  }

#define CGOPT(TY, NAME) CGOPT_NAMED(TY, NAME, CG_##NAME)

#define CGLIST(TY, NAME)                                                       \
  std::vector<TY> codegen::get##NAME(const clv2::OptionsContext &Ctx) {        \
    return clv2::getOptValOr<&clv2::CG_##NAME>(                                \
        Ctx, clv2::descriptorDefault<&clv2::CG_##NAME, std::vector<TY>>());    \
  }

#define CGOPT_EXP(TY, NAME)                                                    \
  CGOPT(TY, NAME)                                                              \
  std::optional<TY> codegen::getExplicit##NAME(                                \
      const clv2::OptionsContext &Ctx) {                                       \
    if (clv2::wasOptSpecified<&clv2::CG_##NAME>(Ctx))                          \
      return clv2::getOptValOr<&clv2::CG_##NAME>(                              \
          Ctx, clv2::descriptorDefault<&clv2::CG_##NAME, TY>());               \
    return std::nullopt;                                                       \
  }

CGOPT(std::string, MArch)
CGOPT(std::string, MCPU)
CGOPT(std::string, MTune)
CGLIST(std::string, MAttrs)
CGOPT_EXP(Reloc::Model, RelocModel)
CGOPT(ThreadModel::Model, ThreadModel)
CGOPT_EXP(CodeModel::Model, CodeModel)
CGOPT_EXP(uint64_t, LargeDataThreshold)
CGOPT(ExceptionHandling, ExceptionModel)
CGOPT_EXP(CodeGenFileType, FileType)
CGOPT_NAMED(FramePointerKind, FramePointerUsage, CG_FramePointer)
CGOPT(bool, EnableNoTrappingFPMath)
CGOPT(bool, EnableAIXExtendedAltivecABI)
CGOPT(DenormalMode::DenormalModeKind, DenormalFPMath)
CGOPT(DenormalMode::DenormalModeKind, DenormalFP32Math)
CGOPT(bool, EnableHonorSignDependentRoundingFPMath)
CGOPT(FloatABI::ABIType, FloatABIForCalls)
CGOPT(FPOpFusion::FPOpFusionMode, FuseFPOps)
CGOPT(SwiftAsyncFramePointerMode, SwiftAsyncFramePointer)
CGOPT(bool, DontPlaceZerosInBSS)
CGOPT(bool, EnableGuaranteedTailCallOpt)
CGOPT(bool, DisableTailCalls)
CGOPT(bool, StackSymbolOrdering)
CGOPT(bool, StackRealign)
CGOPT(std::string, TrapFuncName)
CGOPT(bool, UseCtors)
CGOPT(bool, DisableIntegratedAS)
CGOPT_EXP(bool, DataSections)
CGOPT_EXP(bool, FunctionSections)
CGOPT(bool, IgnoreXCOFFVisibility)
CGOPT(bool, XCOFFTracebackTable)
CGOPT(bool, EnableBBAddrMap)
CGOPT(std::string, BBSections)
CGOPT(unsigned, TLSSize)
CGOPT_EXP(bool, EmulatedTLS)
CGOPT_EXP(bool, EnableTLSDESC)
CGOPT(bool, UniqueSectionNames)
CGOPT(bool, UniqueBasicBlockSectionNames)
CGOPT(bool, SeparateNamedSections)
CGOPT(EABI, EABIVersion)
CGOPT_NAMED(DebuggerKind, DebuggerTuningOpt, CG_DebuggerTuning)
// VectorLibrary getter is manually defined to avoid field/type name collision.
VectorLibrary codegen::getVectorLibrary(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOr<&clv2::CG_VectorLibrary>(
      Ctx, clv2::descriptorDefault<&clv2::CG_VectorLibrary, VectorLibrary>());
}
CGOPT(bool, EnableStackSizeSection)
CGOPT(bool, EnableAddrsig)
CGOPT(bool, EnableCallGraphSection)
CGOPT(bool, EmitCallSiteInfo)
CGOPT(bool, EnableMachineFunctionSplitter)
CGOPT(bool, EnableStaticDataPartitioning)
CGOPT(bool, EnableDebugEntryValues)
CGOPT(bool, ForceDwarfFrameSection)
CGOPT(bool, XRayFunctionIndex)
CGOPT(bool, DebugStrictDwarf)
CGOPT(unsigned, AlignLoops)
CGOPT(bool, JMCInstrument)
CGOPT(bool, XCOFFReadOnlyPointers)
CGOPT(codegen::SaveStatsMode, SaveStats)

std::optional<std::string>
codegen::getBBSectionsColdTextPrefixOverride(const clv2::OptionsContext &Ctx) {
  if (!clv2::wasOptSpecified<&clv2::CG_BbsectionsColdTextPrefix>(Ctx))
    return std::nullopt;
  return clv2::getOptValOr<&clv2::CG_BbsectionsColdTextPrefix>(Ctx,
                                                               std::string{});
}

// Retained as a no-op: tools instantiate it to declare that they want the
// codegen options registered.  There is no longer a snapshot for it to prime.
codegen::RegisterCodeGenFlags::RegisterCodeGenFlags() {
  mc::RegisterMCTargetOptionsFlags();
}

codegen::RegisterMTuneFlag::RegisterMTuneFlag() {}

codegen::RegisterSaveStatsFlag::RegisterSaveStatsFlag() {}

llvm::BasicBlockSection
codegen::getBBSectionsMode(llvm::TargetOptions &Options,
                           const clv2::OptionsContext &Ctx) {
  if (getBBSections(Ctx) == "all")
    return BasicBlockSection::All;
  else if (getBBSections(Ctx) == "none")
    return BasicBlockSection::None;
  else {
    ErrorOr<std::unique_ptr<MemoryBuffer>> MBOrErr =
        MemoryBuffer::getFile(getBBSections(Ctx));
    if (!MBOrErr) {
      errs() << "Error loading basic block sections function list file: "
             << MBOrErr.getError().message() << "\n";
    } else {
      Options.BBSectionsFuncListBuf = std::move(*MBOrErr);
    }
    return BasicBlockSection::List;
  }
}

// Common utility function tightly tied to the options listed here. Initializes
// a TargetOptions object with CodeGen flags and returns it.
TargetOptions codegen::InitTargetOptionsFromCodeGenFlags(
    const Triple &TheTriple, const clv2::OptionsContext &OptsCtx) {
  TargetOptions Options;
  Options.AllowFPOpFusion = getFuseFPOps(OptsCtx);
  Options.NoTrappingFPMath = getEnableNoTrappingFPMath(OptsCtx);

  Options.HonorSignDependentRoundingFPMathOption =
      getEnableHonorSignDependentRoundingFPMath(OptsCtx);
  Options.EnableAIXExtendedAltivecABI = getEnableAIXExtendedAltivecABI(OptsCtx);
  Options.NoZerosInBSS = getDontPlaceZerosInBSS(OptsCtx);
  Options.GuaranteedTailCallOpt = getEnableGuaranteedTailCallOpt(OptsCtx);
  Options.StackSymbolOrdering = getStackSymbolOrdering(OptsCtx);
  Options.UseInitArray = !getUseCtors(OptsCtx);
  Options.DisableIntegratedAS = getDisableIntegratedAS(OptsCtx);
  Options.DataSections = getExplicitDataSections(OptsCtx).value_or(
      TheTriple.hasDefaultDataSections());
  Options.FunctionSections = getFunctionSections(OptsCtx);
  Options.IgnoreXCOFFVisibility = getIgnoreXCOFFVisibility(OptsCtx);
  Options.XCOFFTracebackTable = getXCOFFTracebackTable(OptsCtx);
  Options.BBAddrMap = getEnableBBAddrMap(OptsCtx);
  Options.BBSections = getBBSectionsMode(Options, OptsCtx);
  Options.UniqueSectionNames = getUniqueSectionNames(OptsCtx);
  Options.UniqueBasicBlockSectionNames =
      getUniqueBasicBlockSectionNames(OptsCtx);
  Options.SeparateNamedSections = getSeparateNamedSections(OptsCtx);
  Options.TLSSize = getTLSSize(OptsCtx);
  Options.EmulatedTLS = getExplicitEmulatedTLS(OptsCtx).value_or(
      TheTriple.hasDefaultEmulatedTLS());
  Options.EnableTLSDESC =
      getExplicitEnableTLSDESC(OptsCtx).value_or(TheTriple.hasDefaultTLSDESC());
  Options.ExceptionModel = getExceptionModel(OptsCtx);
  Options.VecLib = getVectorLibrary(OptsCtx);
  Options.EmitStackSizeSection = getEnableStackSizeSection(OptsCtx);
  Options.EnableMachineFunctionSplitter =
      getEnableMachineFunctionSplitter(OptsCtx);
  Options.EnableStaticDataPartitioning =
      getEnableStaticDataPartitioning(OptsCtx);
  Options.EmitAddrsig = getEnableAddrsig(OptsCtx);
  Options.EmitCallGraphSection = getEnableCallGraphSection(OptsCtx);
  Options.EmitCallSiteInfo = getEmitCallSiteInfo(OptsCtx);
  Options.EnableDebugEntryValues = getEnableDebugEntryValues(OptsCtx);
  Options.ForceDwarfFrameSection = getForceDwarfFrameSection(OptsCtx);
  Options.XRayFunctionIndex = getXRayFunctionIndex(OptsCtx);
  Options.DebugStrictDwarf = getDebugStrictDwarf(OptsCtx);
  Options.LoopAlignment = getAlignLoops(OptsCtx);
  Options.JMCInstrument = getJMCInstrument(OptsCtx);
  Options.XCOFFReadOnlyPointers = getXCOFFReadOnlyPointers(OptsCtx);

  Options.MCOptions = mc::InitMCTargetOptionsFromFlags(OptsCtx);
  Options.OptsCtx = &OptsCtx;

  Options.ThreadModel = getThreadModel(OptsCtx);
  Options.EABIVersion = getEABIVersion(OptsCtx);
  Options.DebuggerTuning = getDebuggerTuningOpt(OptsCtx);
  Options.SwiftAsyncFramePointer = getSwiftAsyncFramePointer(OptsCtx);
  return Options;
}

std::string codegen::getCPUStr(const clv2::OptionsContext &Ctx) {
  std::string MCPU = getMCPU(Ctx);

  // If user asked for the 'native' CPU, autodetect here. If auto-detection
  // fails, this will set the CPU to an empty string which tells the target to
  // pick a basic default.
  if (MCPU == "native")
    return std::string(sys::getHostCPUName());

  return MCPU;
}

std::string codegen::getTuneCPUStr(const clv2::OptionsContext &Ctx) {
  std::string TuneCPU = getMTune(Ctx);

  // If user asked for the 'native' tune CPU, autodetect here. If auto-detection
  // fails, this will set the tune CPU to an empty string which tells the target
  // to pick a basic default.
  if (TuneCPU == "native")
    return std::string(sys::getHostCPUName());

  return TuneCPU;
}

std::string codegen::getFeaturesStr(const clv2::OptionsContext &Ctx) {
  SubtargetFeatures Features;

  // If user asked for the 'native' CPU, we need to autodetect features.
  // This is necessary for x86 where the CPU might not support all the
  // features the autodetected CPU name lists in the target. For example,
  // not all Sandybridge processors support AVX.
  if (getMCPU(Ctx) == "native")
    for (const auto &[Feature, IsEnabled] : sys::getHostCPUFeatures())
      Features.AddFeature(Feature, IsEnabled);

  for (auto const &MAttr : getMAttrs(Ctx))
    Features.AddFeature(MAttr);

  return Features.getString();
}

std::vector<std::string>
codegen::getFeatureList(const clv2::OptionsContext &Ctx) {
  SubtargetFeatures Features;

  // If user asked for the 'native' CPU, we need to autodetect features.
  // This is necessary for x86 where the CPU might not support all the
  // features the autodetected CPU name lists in the target. For example,
  // not all Sandybridge processors support AVX.
  if (getMCPU(Ctx) == "native")
    for (const auto &[Feature, IsEnabled] : sys::getHostCPUFeatures())
      Features.AddFeature(Feature, IsEnabled);

  for (auto const &MAttr : getMAttrs(Ctx))
    Features.AddFeature(MAttr);

  return Features.getFeatures();
}

void codegen::renderBoolStringAttr(AttrBuilder &B, StringRef Name, bool Val) {
  B.addAttribute(Name, Val ? "true" : "false");
}

void codegen::setFunctionAttributes(Function &F, StringRef CPU,
                                    StringRef Features, StringRef TuneCPU) {
  auto &Ctx = F.getContext();
  AttributeList Attrs = F.getAttributes();
  AttrBuilder NewAttrs(Ctx);

  if (!CPU.empty() && !F.hasFnAttribute("target-cpu"))
    NewAttrs.addAttribute("target-cpu", CPU);
  if (!TuneCPU.empty() && !F.hasFnAttribute("tune-cpu"))
    NewAttrs.addAttribute("tune-cpu", TuneCPU);
  if (!Features.empty()) {
    // Append the command line features to any that are already on the function.
    StringRef OldFeatures =
        F.getFnAttribute("target-features").getValueAsString();
    if (OldFeatures.empty())
      NewAttrs.addAttribute("target-features", Features);
    else {
      SmallString<256> Appended(OldFeatures);
      Appended.push_back(',');
      Appended.append(Features);
      NewAttrs.addAttribute("target-features", Appended);
    }
  }
  const clv2::OptionsContext &OptsCtx = F.getContext().getOptionsContext();
  if (clv2::wasOptSpecified<&clv2::CG_FramePointer>(OptsCtx) &&
      !F.hasFnAttribute("frame-pointer")) {
    if (getFramePointerUsage(OptsCtx) == FramePointerKind::All)
      NewAttrs.addAttribute("frame-pointer", "all");
    else if (getFramePointerUsage(OptsCtx) == FramePointerKind::NonLeaf)
      NewAttrs.addAttribute("frame-pointer", "non-leaf");
    else if (getFramePointerUsage(OptsCtx) ==
             FramePointerKind::NonLeafNoReserve)
      NewAttrs.addAttribute("frame-pointer", "non-leaf-no-reserve");
    else if (getFramePointerUsage(OptsCtx) == FramePointerKind::Reserved)
      NewAttrs.addAttribute("frame-pointer", "reserved");
    else if (getFramePointerUsage(OptsCtx) == FramePointerKind::None)
      NewAttrs.addAttribute("frame-pointer", "none");
  }
  if (clv2::wasOptSpecified<&clv2::CG_DisableTailCalls>(OptsCtx))
    NewAttrs.addAttribute("disable-tail-calls",
                          toStringRef(getDisableTailCalls(OptsCtx)));
  if (getStackRealign(OptsCtx))
    NewAttrs.addAttribute("stackrealign");

  if ((clv2::wasOptSpecified<&clv2::CG_DenormalFPMath>(OptsCtx) ||
       clv2::wasOptSpecified<&clv2::CG_DenormalFP32Math>(OptsCtx)) &&
      !F.hasFnAttribute(Attribute::DenormalFPEnv)) {
    DenormalMode::DenormalModeKind DenormKind = getDenormalFPMath(OptsCtx);
    DenormalMode::DenormalModeKind DenormKindF32 = getDenormalFP32Math(OptsCtx);

    DenormalFPEnv FPEnv(DenormalMode{DenormKind, DenormKind},
                        DenormalMode{DenormKindF32, DenormKindF32});
    // FIXME: Command line flag should expose separate input/output modes.
    NewAttrs.addDenormalFPEnvAttr(FPEnv);
  }

  if (clv2::wasOptSpecified<&clv2::CG_TrapFuncName>(OptsCtx))
    for (auto &B : F)
      for (auto &I : B)
        if (auto *Call = dyn_cast<CallInst>(&I))
          if (const auto *F = Call->getCalledFunction())
            if (F->getIntrinsicID() == Intrinsic::debugtrap ||
                F->getIntrinsicID() == Intrinsic::trap)
              Call->addFnAttr(Attribute::get(Ctx, "trap-func-name",
                                             getTrapFuncName(OptsCtx)));

  // Let NewAttrs override Attrs.
  F.setAttributes(Attrs.addFnAttributes(Ctx, NewAttrs));
}

void codegen::setFunctionAttributes(Module &M, StringRef CPU,
                                    StringRef Features, StringRef TuneCPU) {
  // Synthesize the "float-abi" module flag from the -float-abi option.
  FloatABI::ABIType ABI =
      getFloatABIForCalls(M.getContext().getOptionsContext());
  if (ABI != FloatABI::Default) {
    if (auto *Existing =
            dyn_cast_or_null<MDString>(M.getModuleFlag("float-abi"))) {
      // The module already records a float ABI; -float-abi must not contradict
      // it.
      if (Existing->getString() != FloatABI::getABITypeName(ABI))
        reportFatalUsageError(
            "-float-abi=" + FloatABI::getABITypeName(ABI) +
            " conflicts with the \"float-abi\" module flag \"" +
            Existing->getString() + "\"");
    } else {
      M.addModuleFlag(
          Module::Error, "float-abi",
          MDString::get(M.getContext(), FloatABI::getABITypeName(ABI)));
    }
  }

  for (Function &F : M)
    setFunctionAttributes(F, CPU, Features, TuneCPU);
}

Expected<std::unique_ptr<TargetMachine>>
codegen::createTargetMachineForTriple(const Triple &TargetTriple,
                                      const clv2::OptionsContext &OptsCtx,
                                      CodeGenOptLevel OptLevel) {
  // lookupTarget may mutate the triple, so we need a copy.
  Triple TheTriple(TargetTriple);
  std::string Error;
  const auto *TheTarget = TargetRegistry::lookupTarget(
      codegen::getMArch(OptsCtx), TheTriple, Error);
  if (!TheTarget)
    return createStringError(inconvertibleErrorCode(), Error);
  auto Options = codegen::InitTargetOptionsFromCodeGenFlags(TheTriple, OptsCtx);
  Options.MCOptions.OptsCtx = &OptsCtx;
  Options.OptsCtx = &OptsCtx;
  auto *Target = TheTarget->createTargetMachine(
      TheTriple, codegen::getCPUStr(OptsCtx), codegen::getFeaturesStr(OptsCtx),
      Options, codegen::getExplicitRelocModel(OptsCtx),
      codegen::getExplicitCodeModel(OptsCtx), OptLevel);
  if (!Target)
    return createStringError(inconvertibleErrorCode(),
                             Twine("could not allocate target machine for ") +
                                 TheTriple.str());
  return std::unique_ptr<TargetMachine>(Target);
}

void codegen::MaybeEnableStatistics(const clv2::OptionsContext &Ctx) {
  if (getSaveStats(Ctx) == SaveStatsMode::None)
    return;

  llvm::EnableStatistics(false);
}

int codegen::MaybeSaveStatistics(StringRef OutputFilename, StringRef ToolName,
                                 const clv2::OptionsContext &Ctx) {
  auto SaveStatsValue = getSaveStats(Ctx);
  if (SaveStatsValue == codegen::SaveStatsMode::None)
    return 0;

  SmallString<128> StatsFilename;
  if (SaveStatsValue == codegen::SaveStatsMode::Obj) {
    StatsFilename = OutputFilename;
    llvm::sys::path::remove_filename(StatsFilename);
  } else {
    assert(SaveStatsValue == codegen::SaveStatsMode::Cwd &&
           "Should have been a valid --save-stats value");
  }

  auto BaseName = llvm::sys::path::filename(OutputFilename);
  llvm::sys::path::append(StatsFilename, BaseName);
  llvm::sys::path::replace_extension(StatsFilename, "stats");

  auto FileFlags = llvm::sys::fs::OF_TextWithCRLF;
  std::error_code EC;
  auto StatsOS =
      std::make_unique<llvm::raw_fd_ostream>(StatsFilename, EC, FileFlags);
  if (EC) {
    WithColor::error(errs(), ToolName)
        << "Unable to open statistics file: " << EC.message() << "\n";
    return 1;
  }

  llvm::PrintStatisticsJSON(*StatsOS);
  return 0;
}
