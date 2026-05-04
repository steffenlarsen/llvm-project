//===-- AMDGPUTargetMachine.cpp - TargetMachine for hw codegen targets-----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file contains both AMDGPU target machine and the CodeGen pass builder.
/// The AMDGPU target machine contains all of the hardware specific information
/// needed to emit code for SI+ GPUs in the legacy pass manager pipeline. The
/// CodeGen pass builder handles the pass pipeline for new pass manager.
//
//===----------------------------------------------------------------------===//

#include "AMDGPUTargetMachine.h"
#include "AMDGPU.h"
#include "AMDGPUAliasAnalysis.h"
#include "AMDGPUAsmPrinter.h"
#include "AMDGPUBarrierLatency.h"
#include "AMDGPUCoExecSchedStrategy.h"
#include "AMDGPUCtorDtorLowering.h"
#include "AMDGPUExportClustering.h"
#include "AMDGPUExportKernelRuntimeHandles.h"
#include "AMDGPUHazardLatency.h"
#include "AMDGPUIGroupLP.h"
#include "AMDGPUISelDAGToDAG.h"
#include "AMDGPULowerVGPREncoding.h"
#include "AMDGPUMacroFusion.h"
#include "AMDGPUNextUseAnalysis.h"
#include "AMDGPUPerfHintAnalysis.h"
#include "AMDGPUPreloadKernArgProlog.h"
#include "AMDGPUPrepareAGPRAlloc.h"
#include "AMDGPURemoveIncompatibleFunctions.h"
#include "AMDGPUReserveWWMRegs.h"
#include "AMDGPUResourceUsageAnalysis.h"
#include "AMDGPUSplitModule.h"
#include "AMDGPUTargetObjectFile.h"
#include "AMDGPUTargetTransformInfo.h"
#include "AMDGPUUnifyDivergentExitNodes.h"
#include "AMDGPUWaitSGPRHazards.h"
#include "GCNDPPCombine.h"
#include "GCNIterativeScheduler.h"
#include "GCNNSAReassign.h"
#include "GCNPreRALongBranchReg.h"
#include "GCNPreRAOptimizations.h"
#include "GCNRewritePartialRegUses.h"
#include "GCNSchedStrategy.h"
#include "GCNVOPDUtils.h"
#include "R600.h"
#include "R600TargetMachine.h"
#include "SIFixSGPRCopies.h"
#include "SIFixVGPRCopies.h"
#include "SIFoldOperands.h"
#include "SIFormMemoryClauses.h"
#include "SILoadStoreOptimizer.h"
#include "SILowerControlFlow.h"
#include "SILowerSGPRSpills.h"
#include "SILowerWWMCopies.h"
#include "SIMachineFunctionInfo.h"
#include "SIMachineScheduler.h"
#include "SIOptimizeExecMasking.h"
#include "SIOptimizeExecMaskingPreRA.h"
#include "SIOptimizeVGPRLiveRange.h"
#include "SIPeepholeSDWA.h"
#include "SIPostRABundler.h"
#include "SIPreAllocateWWMRegs.h"
#include "SIShrinkInstructions.h"
#include "SIWholeQuadMode.h"
#include "TargetInfo/AMDGPUTargetInfo.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/KernelInfo.h"
#include "llvm/Analysis/UniformityAnalysis.h"
#include "llvm/CodeGen/AtomicExpand.h"
#include "llvm/CodeGen/BranchRelaxation.h"
#include "llvm/CodeGen/CodeGenPassOptionsOptInfos.h"
#include "llvm/CodeGen/DeadMachineInstructionElim.h"
#include "llvm/CodeGen/DetectDeadLanes.h"
#include "llvm/CodeGen/EarlyIfConversion.h"
#include "llvm/CodeGen/FuncletLayout.h"
#include "llvm/CodeGen/GCMetadata.h"
#include "llvm/CodeGen/GlobalISel/CSEInfo.h"
#include "llvm/CodeGen/GlobalISel/IRTranslator.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelect.h"
#include "llvm/CodeGen/GlobalISel/Legalizer.h"
#include "llvm/CodeGen/GlobalISel/Localizer.h"
#include "llvm/CodeGen/GlobalISel/RegBankSelect.h"
#include "llvm/CodeGen/MIRParser/MIParser.h"
#include "llvm/CodeGen/MachineCSE.h"
#include "llvm/CodeGen/MachineLICM.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/PHIElimination.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/PatchableFunction.h"
#include "llvm/CodeGen/PostRAHazardRecognizer.h"
#include "llvm/CodeGen/RegAllocFast.h"
#include "llvm/CodeGen/RegAllocGreedyPass.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/CodeGen/RenameIndependentSubregs.h"
#include "llvm/CodeGen/ShadowStackGCLowering.h"
#include "llvm/CodeGen/StackSlotColoring.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TwoAddressInstructionPass.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/CodeGenPassBuilder.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Target/AMDGPU/AMDGPUOptionsOptInfos.h"
#include "llvm/TargetParser/AMDGPUTargetParser.h"
#include "llvm/Transforms/HipStdPar/HipStdPar.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/AlwaysInliner.h"
#include "llvm/Transforms/IPO/ExpandVariadics.h"
#include "llvm/Transforms/IPO/GlobalDCE.h"
#include "llvm/Transforms/IPO/Internalize.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/EarlyCSE.h"
#include "llvm/Transforms/Scalar/FlattenCFG.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/InferAddressSpaces.h"
#include "llvm/Transforms/Scalar/LICM.h"
#include "llvm/Transforms/Scalar/LoopDataPrefetch.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Scalar/NaryReassociate.h"
#include "llvm/Transforms/Scalar/ScalarOptionsOptInfos.h"
#include "llvm/Transforms/Scalar/SeparateConstOffsetFromGEP.h"
#include "llvm/Transforms/Scalar/Sink.h"
#include "llvm/Transforms/Scalar/StraightLineStrengthReduce.h"
#include "llvm/Transforms/Scalar/StructurizeCFG.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/FixIrreducible.h"
#include "llvm/Transforms/Utils/LCSSA.h"
#include "llvm/Transforms/Utils/LowerSwitch.h"
#include "llvm/Transforms/Utils/SimplifyLibCalls.h"
#include "llvm/Transforms/Utils/UnifyLoopExits.h"
#include "llvm/Transforms/Vectorize/LoadStoreVectorizer.h"
#include <optional>

using namespace llvm;
using namespace llvm::PatternMatch;

namespace {
//===----------------------------------------------------------------------===//
// AMDGPU CodeGen Pass Builder interface.
//===----------------------------------------------------------------------===//

class AMDGPUCodeGenPassBuilder : public CodeGenPassBuilder {
  using Base = CodeGenPassBuilder;

  GCNTargetMachine &getTM() const {
    return static_cast<GCNTargetMachine &>(TM);
  }

public:
  AMDGPUCodeGenPassBuilder(GCNTargetMachine &TM,
                           const CGPassBuilderOption &Opts,
                           PassInstrumentationCallbacks *PIC);

  void addIRPasses(PassManagerWrapper &PMW) override;
  void addCodeGenPrepare(PassManagerWrapper &PMW) override;
  void addPreISel(PassManagerWrapper &PMW) override;
  void addILPOpts(PassManagerWrapper &PMW) override;
  void addAsmPrinterBegin(PassManagerWrapper &PMW) override;
  void addAsmPrinter(PassManagerWrapper &PMW) override;
  void addAsmPrinterEnd(PassManagerWrapper &PMW) override;
  Error addInstSelector(PassManagerWrapper &PMW) override;
  void addPreRewrite(PassManagerWrapper &PMW) override;
  void addMachineSSAOptimization(PassManagerWrapper &PMW) override;
  void addPostRegAlloc(PassManagerWrapper &PMW) override;
  void addPreEmitPass(PassManagerWrapper &PMW) override;
  Error addRegAssignAndRewriteFast(PassManagerWrapper &PMW) override;
  Expected<bool>
  addRegAssignAndRewriteOptimized(PassManagerWrapper &PMW) override;
  void addPreRegAlloc(PassManagerWrapper &PMW) override;
  Error addFastRegAlloc(PassManagerWrapper &PMW) override;
  Error addOptimizedRegAlloc(PassManagerWrapper &PMW) override;
  void addPreSched2(PassManagerWrapper &PMW) override;
  void addPostBBSections(PassManagerWrapper &PMW) override;

private:
  Error validateRegAllocOptions() const;

public:
  /// Check if a pass is enabled given \p Opt option. The option always
  /// overrides defaults if explicitly used. Otherwise its default will be used
  /// given that a pass shall work at an optimization \p Level minimum.
  bool isPassEnabled(bool Opt, bool WasSpecified,
                     CodeGenOptLevel Level = CodeGenOptLevel::Default) const;
  void addEarlyCSEOrGVNPass(PassManagerWrapper &PMW);
  void addStraightLineScalarOptimizationPasses(PassManagerWrapper &PMW);
};

class SGPRRegisterRegAlloc : public RegisterRegAllocBase<SGPRRegisterRegAlloc> {
public:
  SGPRRegisterRegAlloc(const char *N, const char *D, FunctionPassCtor C)
    : RegisterRegAllocBase(N, D, C) {}
};

class VGPRRegisterRegAlloc : public RegisterRegAllocBase<VGPRRegisterRegAlloc> {
public:
  VGPRRegisterRegAlloc(const char *N, const char *D, FunctionPassCtor C)
    : RegisterRegAllocBase(N, D, C) {}
};

class WWMRegisterRegAlloc : public RegisterRegAllocBase<WWMRegisterRegAlloc> {
public:
  WWMRegisterRegAlloc(const char *N, const char *D, FunctionPassCtor C)
      : RegisterRegAllocBase(N, D, C) {}
};

//===----------------------------------------------------------------------===//
// Legacy-PM register allocator selection options
//
// Hand-written rather than declared in AMDGPUOptions.td: the set of valid
// values is whatever is registered in the SGPR/VGPR/WWM registries above, which
// are file-local.  A .td descriptor is a header-inline constexpr, so anything
// its Validate body calls must be linkable from every TU that reads the option
// -- including tools that do not link this backend.  Keeping descriptor,
// validator and registry in one TU is what makes the check possible at all.
//===----------------------------------------------------------------------===//

template <typename RegAllocT>
static bool checkRegAllocName(const std::string &Value, StringRef OptName,
                              clv2::detail::ParseDiag &Diag) {
  if (Value.empty())
    return true;
  for (auto *I = RegAllocT::getList(); I; I = I->getNext())
    if (Value == I->getName())
      return true;
  // Resolution below falls back to the default allocator on an unknown name,
  // so without this a typo would change codegen with no diagnostic.
  return clv2::detail::rejectOptionValue(
      OptName, "Cannot find option named '" + Value + "'!", Diag);
}

static bool validateSGPRRegAlloc(const std::string &V, StringRef N,
                                 clv2::detail::ParseDiag &D) {
  return checkRegAllocName<SGPRRegisterRegAlloc>(V, N, D);
}
static bool validateVGPRRegAlloc(const std::string &V, StringRef N,
                                 clv2::detail::ParseDiag &D) {
  return checkRegAllocName<VGPRRegisterRegAlloc>(V, N, D);
}
static bool validateWWMRegAlloc(const std::string &V, StringRef N,
                                clv2::detail::ParseDiag &D) {
  return checkRegAllocName<WWMRegisterRegAlloc>(V, N, D);
}

static constexpr clv2::OptionInfo<std::string> OI_SGPRRegAlloc{
    "sgpr-regalloc", "SGPR register allocator to use", clv2::Hidden,
    clv2::Validate<std::string>{&validateSGPRRegAlloc}};
static constexpr clv2::OptionInfo<std::string> OI_VGPRRegAlloc{
    "vgpr-regalloc", "VGPR register allocator to use", clv2::Hidden,
    clv2::Validate<std::string>{&validateVGPRRegAlloc}};
static constexpr clv2::OptionInfo<std::string> OI_WWMRegAlloc{
    "wwm-regalloc", "WWM register allocator to use", clv2::Hidden,
    clv2::Validate<std::string>{&validateWWMRegAlloc}};
static constexpr clv2::OptionsRegistry<&OI_SGPRRegAlloc, &OI_VGPRRegAlloc,
                                       &OI_WWMRegAlloc>
    AMDGPURegAllocOptReg;
static const int RegisterAMDGPURegAllocDynamic = [] {
  clv2::registerDynamicRegistry<&AMDGPURegAllocOptReg>();
  return 0;
}();

static bool onlyAllocateSGPRs(const TargetRegisterInfo &TRI,
                              const MachineRegisterInfo &MRI,
                              const Register Reg) {
  const TargetRegisterClass *RC = MRI.getRegClass(Reg);
  return static_cast<const SIRegisterInfo &>(TRI).isSGPRClass(RC);
}

static bool onlyAllocateVGPRs(const TargetRegisterInfo &TRI,
                              const MachineRegisterInfo &MRI,
                              const Register Reg) {
  const TargetRegisterClass *RC = MRI.getRegClass(Reg);
  return !static_cast<const SIRegisterInfo &>(TRI).isSGPRClass(RC);
}

static bool onlyAllocateWWMRegs(const TargetRegisterInfo &TRI,
                                const MachineRegisterInfo &MRI,
                                const Register Reg) {
  const SIMachineFunctionInfo *MFI =
      MRI.getMF().getInfo<SIMachineFunctionInfo>();
  const TargetRegisterClass *RC = MRI.getRegClass(Reg);
  return !static_cast<const SIRegisterInfo &>(TRI).isSGPRClass(RC) &&
         MFI->checkFlag(Reg, AMDGPU::VirtRegFlag::WWM_REG);
}

/// -{sgpr|wwm|vgpr}-regalloc=... command line option.
static FunctionPass *useDefaultRegisterAllocator() { return nullptr; }

static SGPRRegisterRegAlloc
defaultSGPRRegAlloc("default",
                    "pick SGPR register allocator based on -O option",
                    useDefaultRegisterAllocator);

// New pass manager register allocator options for AMDGPU
static RegAllocType getSGPRRegAllocNPM(const Function *F,
                                       const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return static_cast<RegAllocType>(O->get<&clv2::AMDGPU_SGPRRegAllocNPM>());
  if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(Ctx))
    return static_cast<RegAllocType>(O->get<&clv2::AMDGPU_SGPRRegAllocNPM>());
  return RegAllocType::Default;
}

static RegAllocType getVGPRRegAllocNPM(const Function *F,
                                       const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return static_cast<RegAllocType>(O->get<&clv2::AMDGPU_VGPRRegAllocNPM>());
  if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(Ctx))
    return static_cast<RegAllocType>(O->get<&clv2::AMDGPU_VGPRRegAllocNPM>());
  return RegAllocType::Default;
}

static RegAllocType getWWMRegAllocNPM(const Function *F,
                                      const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return static_cast<RegAllocType>(O->get<&clv2::AMDGPU_WWMRegAllocNPM>());
  if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(Ctx))
    return static_cast<RegAllocType>(O->get<&clv2::AMDGPU_WWMRegAllocNPM>());
  return RegAllocType::Default;
}

/// Check if the given RegAllocType is supported for AMDGPU NPM register
/// allocation. Only Fast and Greedy are supported; Basic and PBQP are not.
static Error checkRegAllocSupported(RegAllocType RAType, StringRef RegName) {
  if (RAType == RegAllocType::Basic || RAType == RegAllocType::PBQP) {
    return make_error<StringError>(
        Twine("unsupported register allocator '") +
            (RAType == RegAllocType::Basic ? "basic" : "pbqp") + "' for " +
            RegName + " registers",
        inconvertibleErrorCode());
  }
  return Error::success();
}

Error AMDGPUCodeGenPassBuilder::validateRegAllocOptions() const {
  // 1. Generic --regalloc-npm is not supported for AMDGPU.
  if (Opt.RegAlloc != RegAllocType::Unset) {
    return make_error<StringError>(
        "-regalloc-npm not supported for amdgcn. Use -sgpr-regalloc-npm, "
        "-vgpr-regalloc-npm, and -wwm-regalloc-npm",
        inconvertibleErrorCode());
  }

  // 2. Legacy PM regalloc options are not compatible with NPM.
  if (clv2::wasOptSpecified<&AMDGPURegAllocOptReg, &OI_SGPRRegAlloc>(
          TM.getOptionsContext()) ||
      clv2::wasOptSpecified<&AMDGPURegAllocOptReg, &OI_VGPRRegAlloc>(
          TM.getOptionsContext()) ||
      clv2::wasOptSpecified<&AMDGPURegAllocOptReg, &OI_WWMRegAlloc>(
          TM.getOptionsContext())) {
    return make_error<StringError>(
        "-sgpr-regalloc, -vgpr-regalloc, and -wwm-regalloc are legacy PM "
        "options. Use -sgpr-regalloc-npm, -vgpr-regalloc-npm, and "
        "-wwm-regalloc-npm with the new pass manager",
        inconvertibleErrorCode());
  }

  // 3. Only Fast and Greedy allocators are supported for AMDGPU.
  if (auto Err = checkRegAllocSupported(
          getSGPRRegAllocNPM(nullptr, TM.getOptionsContext()), "SGPR"))
    return Err;
  if (auto Err = checkRegAllocSupported(
          getWWMRegAllocNPM(nullptr, TM.getOptionsContext()), "WWM"))
    return Err;
  if (auto Err = checkRegAllocSupported(
          getVGPRRegAllocNPM(nullptr, TM.getOptionsContext()), "VGPR"))
    return Err;

  return Error::success();
}


static FunctionPass *createBasicSGPRRegisterAllocator() {
  return createBasicRegisterAllocator(onlyAllocateSGPRs);
}

static FunctionPass *createGreedySGPRRegisterAllocator() {
  return createGreedyRegisterAllocator(onlyAllocateSGPRs);
}

static FunctionPass *createFastSGPRRegisterAllocator() {
  return createFastRegisterAllocator(onlyAllocateSGPRs, false);
}

static FunctionPass *createBasicVGPRRegisterAllocator() {
  return createBasicRegisterAllocator(onlyAllocateVGPRs);
}

static FunctionPass *createGreedyVGPRRegisterAllocator() {
  return createGreedyRegisterAllocator(onlyAllocateVGPRs);
}

static FunctionPass *createFastVGPRRegisterAllocator() {
  return createFastRegisterAllocator(onlyAllocateVGPRs, true);
}

static FunctionPass *createBasicWWMRegisterAllocator() {
  return createBasicRegisterAllocator(onlyAllocateWWMRegs);
}

static FunctionPass *createGreedyWWMRegisterAllocator() {
  return createGreedyRegisterAllocator(onlyAllocateWWMRegs);
}

static FunctionPass *createFastWWMRegisterAllocator() {
  return createFastRegisterAllocator(onlyAllocateWWMRegs, false);
}

static SGPRRegisterRegAlloc basicRegAllocSGPR(
  "basic", "basic register allocator", createBasicSGPRRegisterAllocator);
static SGPRRegisterRegAlloc greedyRegAllocSGPR(
  "greedy", "greedy register allocator", createGreedySGPRRegisterAllocator);

static SGPRRegisterRegAlloc fastRegAllocSGPR(
  "fast", "fast register allocator", createFastSGPRRegisterAllocator);


static VGPRRegisterRegAlloc basicRegAllocVGPR(
  "basic", "basic register allocator", createBasicVGPRRegisterAllocator);
static VGPRRegisterRegAlloc greedyRegAllocVGPR(
  "greedy", "greedy register allocator", createGreedyVGPRRegisterAllocator);

static VGPRRegisterRegAlloc fastRegAllocVGPR(
  "fast", "fast register allocator", createFastVGPRRegisterAllocator);
static WWMRegisterRegAlloc basicRegAllocWWMReg("basic",
                                               "basic register allocator",
                                               createBasicWWMRegisterAllocator);
static WWMRegisterRegAlloc
    greedyRegAllocWWMReg("greedy", "greedy register allocator",
                         createGreedyWWMRegisterAllocator);
static WWMRegisterRegAlloc fastRegAllocWWMReg("fast", "fast register allocator",
                                              createFastWWMRegisterAllocator);

static bool isLTOPreLink(ThinOrFullLTOPhase Phase) {
  return Phase == ThinOrFullLTOPhase::FullLTOPreLink ||
         Phase == ThinOrFullLTOPhase::ThinLTOPreLink;
}
} // anonymous namespace

/// Resolve an AMDGPU register allocator from the OptionsContext.
/// Returns useDefaultRegisterAllocator if no option was specified or name
/// doesn't match any registered allocator.
template <auto *Opt, typename RegAllocT>
static typename RegAllocT::FunctionPassCtor
resolveAMDGPURegAlloc(const clv2::OptionsContext &Ctx) {
  std::string Name = clv2::getOptValIfSpecified<&AMDGPURegAllocOptReg, Opt>(
      Ctx, std::string{});
  if (Name.empty())
    return useDefaultRegisterAllocator;
  for (auto *I = RegAllocT::getList(); I; I = I->getNext())
    if (Name == I->getName())
      return I->getCtor();
  return useDefaultRegisterAllocator;
}

static bool getEnableEarlyIfConversion(const Function *F,
                                       const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_EnableEarlyIfConversion>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnableEarlyIfConversion>(Ctx);
}

static bool getOptExecMaskPreRA(const Function *F,
                                const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_OptExecMaskPreRA>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_OptExecMaskPreRA>(Ctx);
}

static bool getLowerCtorDtor(const Function *F,
                             const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_LowerCtorDtor>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_LowerCtorDtor>(Ctx);
}

static bool getEnableLoadStoreVectorizer(const Function *F,
                                         const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_EnableLoadStoreVectorizer>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnableLoadStoreVectorizer>(Ctx);
}

static bool
getEnableLoadStoreVectorizerWasSpecified(const Function *F,
                                         const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->specified<&clv2::AMDGPU_EnableLoadStoreVectorizer>();
  return clv2::wasOptSpecified<&clv2::AMDGPUOptsReg,
                               &clv2::AMDGPU_EnableLoadStoreVectorizer>(Ctx);
}

static bool getScalarizeGlobal(const Function *F,
                               const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_ScalarizeGlobal>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_ScalarizeGlobal>(Ctx);
}

static bool getInternalizeSymbols(const Function *F,
                                  const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_InternalizeSymbols>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_InternalizeSymbols>(Ctx);
}

static bool getEarlyInlineAll(const Function *F,
                              const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_EarlyInlineAll>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EarlyInlineAll>(Ctx);
}

static bool getRemoveIncompatibleFunctions(const Function *F,
                                           const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_RemoveIncompatibleFunctions>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_RemoveIncompatibleFunctions>(
      Ctx);
}

static bool getEnableSDWAPeephole(const Function *F,
                                  const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_EnableSDWAPeephole>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnableSDWAPeephole>(Ctx);
}

static bool getEnableSDWAPeepholeWasSpecified(const Function *F,
                                              const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->specified<&clv2::AMDGPU_EnableSDWAPeephole>();
  return clv2::wasOptSpecified<&clv2::AMDGPUOptsReg,
                               &clv2::AMDGPU_EnableSDWAPeephole>(Ctx);
}

static bool getEnableDPPCombine(const Function *F,
                                const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_EnableDPPCombine>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnableDPPCombine>(Ctx);
}

static bool getEnableAMDGPUAliasAnalysis(const Function *F,
                                         const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_EnableAMDGPUAliasAnalysis>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnableAMDGPUAliasAnalysis>(Ctx);
}

static bool getEnableLibCallSimplify(const Function *F,
                                     const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_EnableLibCallSimplify>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnableLibCallSimplify>(Ctx);
}

static bool getEnableLowerKernelArguments(const Function *F,
                                          const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_EnableLowerKernelArguments>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnableLowerKernelArguments>(
      Ctx);
}

static bool getEnableRegReassign(const Function *F,
                                 const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_EnableRegReassign>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnableRegReassign>(Ctx);
}

static bool getOptVGPRLiveRange(const Function *F,
                                const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_OptVGPRLiveRange>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_OptVGPRLiveRange>(Ctx);
}

static ScanOptions
getAMDGPUAtomicOptimizerStrategy(const Function *F,
                                 const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return static_cast<ScanOptions>(
          O->get<&clv2::AMDGPU_AtomicOptimizerStrategy>());
  if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(Ctx))
    return static_cast<ScanOptions>(
        O->get<&clv2::AMDGPU_AtomicOptimizerStrategy>());
  return ScanOptions::Iterative;
}

static bool getEnableSIModeRegisterPass(const Function *F,
                                        const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_EnableSIModeRegisterPass>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnableSIModeRegisterPass>(Ctx);
}

static bool getEnableInsertDelayAlu(const Function *F,
                                    const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_EnableInsertDelayAlu>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnableInsertDelayAlu>(Ctx);
}

static bool
getEnableInsertDelayAluWasSpecified(const Function *F,
                                    const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->specified<&clv2::AMDGPU_EnableInsertDelayAlu>();
  return clv2::wasOptSpecified<&clv2::AMDGPUOptsReg,
                               &clv2::AMDGPU_EnableInsertDelayAlu>(Ctx);
}

static bool getEnableVOPD(const Function *F, const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_EnableVOPD>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnableVOPD>(Ctx);
}

static bool getEnableVOPDWasSpecified(const Function *F,
                                      const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->specified<&clv2::AMDGPU_EnableVOPD>();
  return clv2::wasOptSpecified<&clv2::AMDGPUOptsReg, &clv2::AMDGPU_EnableVOPD>(
      Ctx);
}

static bool getEnableDCEInRA(const Function *F,
                             const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_EnableDCEInRA>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnableDCEInRA>(Ctx);
}

static bool getEnableSetWavePriority(const Function *F,
                                     const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_EnableSetWavePriority>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnableSetWavePriority>(Ctx);
}

static bool
getEnableSetWavePriorityWasSpecified(const Function *F,
                                     const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->specified<&clv2::AMDGPU_EnableSetWavePriority>();
  return clv2::wasOptSpecified<&clv2::AMDGPUOptsReg,
                               &clv2::AMDGPU_EnableSetWavePriority>(Ctx);
}

static bool getEnableScalarIRPasses(const Function *F,
                                    const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_EnableScalarIRPasses>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnableScalarIRPasses>(Ctx);
}

static bool
getEnableScalarIRPassesWasSpecified(const Function *F,
                                    const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->specified<&clv2::AMDGPU_EnableScalarIRPasses>();
  return clv2::wasOptSpecified<&clv2::AMDGPUOptsReg,
                               &clv2::AMDGPU_EnableScalarIRPasses>(Ctx);
}

static bool getEnableLowerExecSync(const Function *F,
                                   const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_EnableLowerExecSync>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnableLowerExecSync>(Ctx);
}

static bool getEnableSwLowerLDS(const Function *F,
                                const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_EnableSwLowerLDS>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnableSwLowerLDS>(Ctx);
}

static bool getEnablePreRAOptimizations(const Function *F,
                                        const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_EnablePreRAOptimizations>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnablePreRAOptimizations>(Ctx);
}

static bool
getEnablePreRAOptimizationsWasSpecified(const Function *F,
                                        const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->specified<&clv2::AMDGPU_EnablePreRAOptimizations>();
  return clv2::wasOptSpecified<&clv2::AMDGPUOptsReg,
                               &clv2::AMDGPU_EnablePreRAOptimizations>(Ctx);
}

static bool getEnablePromoteKernelArguments(const Function *F,
                                            const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_EnablePromoteKernelArguments>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnablePromoteKernelArguments>(
      Ctx);
}

static bool getEnableImageIntrinsicOptimizer(const Function *F,
                                             const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_EnableImageIntrinsicOptimizer>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnableImageIntrinsicOptimizer>(
      Ctx);
}

static bool
getEnableImageIntrinsicOptimizerWasSpecified(const Function *F,
                                             const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->specified<&clv2::AMDGPU_EnableImageIntrinsicOptimizer>();
  return clv2::wasOptSpecified<&clv2::AMDGPUOptsReg,
                               &clv2::AMDGPU_EnableImageIntrinsicOptimizer>(
      Ctx);
}

static bool getEnableLoopPrefetch(const Function *F,
                                  const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_EnableLoopPrefetch>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnableLoopPrefetch>(Ctx);
}

static bool getEnableLoopPrefetchWasSpecified(const Function *F,
                                              const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->specified<&clv2::AMDGPU_EnableLoopPrefetch>();
  return clv2::wasOptSpecified<&clv2::AMDGPUOptsReg,
                               &clv2::AMDGPU_EnableLoopPrefetch>(Ctx);
}

static std::string getAMDGPUSchedStrategy(const Function *F,
                                          const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_SchedStrategy>();
  if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(Ctx))
    return O->get<&clv2::AMDGPU_SchedStrategy>();
  static const std::string Default;
  return Default;
}

static bool getEnableRewritePartialRegUses(const Function *F,
                                           const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_EnableRewritePartialRegUses>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnableRewritePartialRegUses>(
      Ctx);
}

static bool getEnableHipStdPar(const Function *F,
                               const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_EnableHipStdPar>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnableHipStdPar>(Ctx);
}

static bool getEnableAMDGPUAttributor(const Function *F,
                                      const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_EnableAMDGPUAttributor>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnableAMDGPUAttributor>(Ctx);
}

static bool getNewRegBankSelect(const Function *F,
                                const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_NewRegBankSelect>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_NewRegBankSelect>(Ctx);
}

static bool getHasClosedWorldAssumption(const Function *F,
                                        const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_HasClosedWorldAssumption>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_HasClosedWorldAssumption>(Ctx);
}

static bool getEnableUniformIntrinsicCombine(const Function *F,
                                             const clv2::OptionsContext &Ctx) {
  if (F)
    if (auto *O = clv2::getView<&clv2::AMDGPUOptsReg>(
            F->getContext().getOptionsContext()))
      return O->get<&clv2::AMDGPU_EnableUniformIntrinsicCombine>();
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnableUniformIntrinsicCombine>(
      Ctx);
}

// Scheduler selection is consulted both when creating the scheduler and from
// overrideSchedPolicy(), so keep the attribute and global command line handling
// in one helper.
StringRef llvm::AMDGPU::getSchedStrategy(const Function &F) {
  Attribute SchedStrategyAttr = F.getFnAttribute("amdgpu-sched-strategy");
  if (SchedStrategyAttr.isValid())
    return SchedStrategyAttr.getValueAsString();

  static std::string CachedStrategy;
  CachedStrategy =
      getAMDGPUSchedStrategy(&F, F.getContext().getOptionsContext());
  if (!CachedStrategy.empty())
    return CachedStrategy;

  return "";
}

static void
diagnoseUnsupportedCoExecSchedulerSelection(const Function &F,
                                            const GCNSubtarget &ST) {
  if (ST.hasGFX1250Insts())
    return;

  F.getContext().diagnose(DiagnosticInfoUnsupported(
      F, "'amdgpu-sched-strategy'='coexec' is only supported for gfx1250",
      DiagnosticLocation(), DS_Warning));
}

static bool useNoopPostScheduler(const Function &F) {
  Attribute PostSchedStrategyAttr =
      F.getFnAttribute("amdgpu-post-sched-strategy");
  return PostSchedStrategyAttr.isValid() &&
         PostSchedStrategyAttr.getValueAsString() == "nop";
}

bool AMDGPUTargetMachine::getEnableObjectLinking(
    const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnableObjectLinking>(Ctx);
}

bool AMDGPUTargetMachine::getEnableLowerModuleLDS(
    const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnableLowerModuleLDS>(Ctx);
}

bool AMDGPUTargetMachine::getEnableFunctionCalls(
    const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnableFunctionCalls>(Ctx);
}

bool AMDGPUTargetMachine::getEnableFunctionCalls(
    const Triple &TT, const clv2::OptionsContext &Ctx) {
  if (!TT.isAMDGCN() && !getEnableFunctionCallsWasSpecified(Ctx))
    return false;
  return getEnableFunctionCalls(Ctx);
}

bool AMDGPUTargetMachine::getEnableFunctionCallsWasSpecified(
    const clv2::OptionsContext &Ctx) {
  return clv2::wasOptSpecified<&clv2::AMDGPUOptsReg,
                               &clv2::AMDGPU_EnableFunctionCalls>(Ctx);
}

static bool getEnableMachinePipeliner(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOrDefault<&clv2::AMDGPU_EnableMachinePipeliner>(Ctx);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeAMDGPUTarget() {
  // Register the target
  RegisterTargetMachine<R600TargetMachine> X(getTheR600Target());
  RegisterTargetMachine<GCNTargetMachine> Y(getTheGCNTarget());
  RegisterTargetMachine<GCNTargetMachine> YLegacy(getTheGCNLegacyTarget());

  PassRegistry *PR = PassRegistry::getPassRegistry();
  initializeR600ClauseMergePassPass(*PR);
  initializeR600ControlFlowFinalizerPass(*PR);
  initializeR600PacketizerPass(*PR);
  initializeR600ExpandSpecialInstrsPassPass(*PR);
  initializeR600VectorRegMergerPass(*PR);
  initializeR600EmitClauseMarkersPass(*PR);
  initializeR600MachineCFGStructurizerPass(*PR);
  initializeGlobalISel(*PR);
  initializeAMDGPUAsmPrinterPass(*PR);
  initializeAMDGPUDAGToDAGISelLegacyPass(*PR);
  initializeAMDGPUPrepareAGPRAllocLegacyPass(*PR);
  initializeGCNDPPCombineLegacyPass(*PR);
  initializeSILowerI1CopiesLegacyPass(*PR);
  initializeAMDGPUGlobalISelDivergenceLoweringLegacyPass(*PR);
  initializeAMDGPURegBankSelectPass(*PR);
  initializeAMDGPURegBankLegalizePass(*PR);
  initializeSILowerWWMCopiesLegacyPass(*PR);
  initializeAMDGPUMarkLastScratchLoadLegacyPass(*PR);
  initializeSILowerSGPRSpillsLegacyPass(*PR);
  initializeSIFixSGPRCopiesLegacyPass(*PR);
  initializeSIFixVGPRCopiesLegacyPass(*PR);
  initializeSIFoldOperandsLegacyPass(*PR);
  initializeSIPeepholeSDWALegacyPass(*PR);
  initializeSIShrinkInstructionsLegacyPass(*PR);
  initializeSIOptimizeExecMaskingPreRALegacyPass(*PR);
  initializeSIOptimizeVGPRLiveRangeLegacyPass(*PR);
  initializeAMDGPUNextUseAnalysisLegacyPassPass(*PR);
  initializeAMDGPUNextUseAnalysisPrinterLegacyPassPass(*PR);
  initializeSILoadStoreOptimizerLegacyPass(*PR);
  initializeAMDGPUCtorDtorLoweringLegacyPass(*PR);
  initializeAMDGPUAlwaysInlinePass(*PR);
  initializeAMDGPULowerExecSyncLegacyPass(*PR);
  initializeAMDGPUSwLowerLDSLegacyPass(*PR);
  initializeAMDGPUAnnotateUniformValuesLegacyPass(*PR);
  initializeAMDGPUAtomicOptimizerPass(*PR);
  initializeAMDGPULowerKernelArgumentsPass(*PR);
  initializeAMDGPUPromoteKernelArgumentsPass(*PR);
  initializeAMDGPULowerKernelAttributesPass(*PR);
  initializeAMDGPUExportKernelRuntimeHandlesLegacyPass(*PR);
  initializeAMDGPUPostLegalizerCombinerPass(*PR);
  initializeAMDGPUPreLegalizerCombinerPass(*PR);
  initializeAMDGPURegBankCombinerPass(*PR);
  initializeAMDGPUPromoteAllocaPass(*PR);
  initializeAMDGPUCodeGenPreparePass(*PR);
  initializeAMDGPULateCodeGenPrepareLegacyPass(*PR);
  initializeAMDGPURemoveIncompatibleFunctionsLegacyPass(*PR);
  initializeAMDGPULowerModuleLDSLegacyPass(*PR);
  initializeAMDGPULowerBufferFatPointersPass(*PR);
  initializeAMDGPULowerIntrinsicsLegacyPass(*PR);
  initializeAMDGPUReserveWWMRegsLegacyPass(*PR);
  initializeAMDGPURewriteAGPRCopyMFMALegacyPass(*PR);
  initializeAMDGPURewriteOutArgumentsPass(*PR);
  initializeAMDGPURewriteUndefForPHILegacyPass(*PR);
  initializeSIAnnotateControlFlowLegacyPass(*PR);
  initializeAMDGPUInsertDelayAluLegacyPass(*PR);
  initializeAMDGPULowerVGPREncodingLegacyPass(*PR);
  initializeSIInsertHardClausesLegacyPass(*PR);
  initializeSIInsertWaitcntsLegacyPass(*PR);
  initializeSIModeRegisterLegacyPass(*PR);
  initializeSIWholeQuadModeLegacyPass(*PR);
  initializeSILowerControlFlowLegacyPass(*PR);
  initializeSIPreEmitPeepholeLegacyPass(*PR);
  initializeSILateBranchLoweringLegacyPass(*PR);
  initializeSIMemoryLegalizerLegacyPass(*PR);
  initializeSIOptimizeExecMaskingLegacyPass(*PR);
  initializeSIPreAllocateWWMRegsLegacyPass(*PR);
  initializeSIFormMemoryClausesLegacyPass(*PR);
  initializeSIPostRABundlerLegacyPass(*PR);
  initializeGCNCreateVOPDLegacyPass(*PR);
  initializeAMDGPUUnifyDivergentExitNodesLegacyPass(*PR);
  initializeAMDGPUAAWrapperPassPass(*PR);
  initializeAMDGPUExternalAAWrapperPass(*PR);
  initializeAMDGPUImageIntrinsicOptimizerPass(*PR);
  initializeAMDGPUPrintfRuntimeBindingPass(*PR);
  initializeAMDGPUResourceUsageAnalysisWrapperPassPass(*PR);
  initializeGCNNSAReassignLegacyPass(*PR);
  initializeGCNPreRAOptimizationsLegacyPass(*PR);
  initializeGCNPreRALongBranchRegLegacyPass(*PR);
  initializeGCNRewritePartialRegUsesLegacyPass(*PR);
  initializeGCNRegPressurePrinterPass(*PR);
  initializeAMDGPUPreloadKernArgPrologLegacyPass(*PR);
  initializeAMDGPUWaitSGPRHazardsLegacyPass(*PR);
  initializeAMDGPUPreloadKernelArgumentsLegacyPass(*PR);
  initializeAMDGPUUniformIntrinsicCombineLegacyPass(*PR);
}

static std::unique_ptr<TargetLoweringObjectFile> createTLOF(const Triple &TT) {
  return std::make_unique<AMDGPUTargetObjectFile>();
}

static ScheduleDAGInstrs *createSIMachineScheduler(MachineSchedContext *C) {
  return new SIScheduleDAGMI(C);
}

static ScheduleDAGInstrs *
createGCNMaxOccupancyMachineScheduler(MachineSchedContext *C) {
  const GCNSubtarget &ST = C->MF->getSubtarget<GCNSubtarget>();
  ScheduleDAGMILive *DAG =
    new GCNScheduleDAGMILive(C, std::make_unique<GCNMaxOccupancySchedStrategy>(C));
  DAG->addMutation(createLoadClusterDAGMutation(DAG->TII, DAG->TRI));
  if (ST.shouldClusterStores())
    DAG->addMutation(createStoreClusterDAGMutation(DAG->TII, DAG->TRI));
  DAG->addMutation(createIGroupLPDAGMutation(AMDGPU::SchedulingPhase::Initial));
  DAG->addMutation(createAMDGPUMacroFusionDAGMutation(
      C->MF->getFunction().getContext().getOptionsContext()));
  DAG->addMutation(createAMDGPUExportClusteringDAGMutation());
  DAG->addMutation(createAMDGPUBarrierLatencyDAGMutation(C->MF));
  DAG->addMutation(createAMDGPUHazardLatencyDAGMutation(C->MF));
  return DAG;
}

static ScheduleDAGInstrs *
createGCNMaxILPMachineScheduler(MachineSchedContext *C) {
  ScheduleDAGMILive *DAG =
      new GCNScheduleDAGMILive(C, std::make_unique<GCNMaxILPSchedStrategy>(C));
  DAG->addMutation(createIGroupLPDAGMutation(AMDGPU::SchedulingPhase::Initial));
  return DAG;
}

static ScheduleDAGInstrs *
createGCNMaxMemoryClauseMachineScheduler(MachineSchedContext *C) {
  const GCNSubtarget &ST = C->MF->getSubtarget<GCNSubtarget>();
  ScheduleDAGMILive *DAG = new GCNScheduleDAGMILive(
      C, std::make_unique<GCNMaxMemoryClauseSchedStrategy>(C));
  DAG->addMutation(createLoadClusterDAGMutation(DAG->TII, DAG->TRI));
  if (ST.shouldClusterStores())
    DAG->addMutation(createStoreClusterDAGMutation(DAG->TII, DAG->TRI));
  DAG->addMutation(createAMDGPUExportClusteringDAGMutation());
  DAG->addMutation(createAMDGPUBarrierLatencyDAGMutation(C->MF));
  DAG->addMutation(createAMDGPUHazardLatencyDAGMutation(C->MF));
  return DAG;
}

static ScheduleDAGInstrs *
createIterativeGCNMaxOccupancyMachineScheduler(MachineSchedContext *C) {
  const GCNSubtarget &ST = C->MF->getSubtarget<GCNSubtarget>();
  auto *DAG = new GCNIterativeScheduler(
      C, GCNIterativeScheduler::SCHEDULE_LEGACYMAXOCCUPANCY);
  DAG->addMutation(createLoadClusterDAGMutation(DAG->TII, DAG->TRI));
  if (ST.shouldClusterStores())
    DAG->addMutation(createStoreClusterDAGMutation(DAG->TII, DAG->TRI));
  DAG->addMutation(createIGroupLPDAGMutation(AMDGPU::SchedulingPhase::Initial));
  return DAG;
}

static ScheduleDAGInstrs *createMinRegScheduler(MachineSchedContext *C) {
  auto *DAG = new GCNIterativeScheduler(
      C, GCNIterativeScheduler::SCHEDULE_MINREGFORCED);
  DAG->addMutation(createIGroupLPDAGMutation(AMDGPU::SchedulingPhase::Initial));
  return DAG;
}

static ScheduleDAGInstrs *
createIterativeILPMachineScheduler(MachineSchedContext *C) {
  const GCNSubtarget &ST = C->MF->getSubtarget<GCNSubtarget>();
  auto *DAG = new GCNIterativeScheduler(C, GCNIterativeScheduler::SCHEDULE_ILP);
  DAG->addMutation(createLoadClusterDAGMutation(DAG->TII, DAG->TRI));
  if (ST.shouldClusterStores())
    DAG->addMutation(createStoreClusterDAGMutation(DAG->TII, DAG->TRI));
  DAG->addMutation(createAMDGPUMacroFusionDAGMutation(
      C->MF->getFunction().getContext().getOptionsContext()));
  DAG->addMutation(createIGroupLPDAGMutation(AMDGPU::SchedulingPhase::Initial));
  return DAG;
}

static MachineSchedRegistry
SISchedRegistry("si", "Run SI's custom scheduler",
                createSIMachineScheduler);

static MachineSchedRegistry
GCNMaxOccupancySchedRegistry("gcn-max-occupancy",
                             "Run GCN scheduler to maximize occupancy",
                             createGCNMaxOccupancyMachineScheduler);

static MachineSchedRegistry
    GCNMaxILPSchedRegistry("gcn-max-ilp", "Run GCN scheduler to maximize ilp",
                           createGCNMaxILPMachineScheduler);

static MachineSchedRegistry GCNMaxMemoryClauseSchedRegistry(
    "gcn-max-memory-clause", "Run GCN scheduler to maximize memory clause",
    createGCNMaxMemoryClauseMachineScheduler);

static MachineSchedRegistry IterativeGCNMaxOccupancySchedRegistry(
    "gcn-iterative-max-occupancy-experimental",
    "Run GCN scheduler to maximize occupancy (experimental)",
    createIterativeGCNMaxOccupancyMachineScheduler);

static MachineSchedRegistry GCNMinRegSchedRegistry(
    "gcn-iterative-minreg",
    "Run GCN iterative scheduler for minimal register usage (experimental)",
    createMinRegScheduler);

static MachineSchedRegistry GCNILPSchedRegistry(
    "gcn-iterative-ilp",
    "Run GCN iterative scheduler for ILP scheduling (experimental)",
    createIterativeILPMachineScheduler);

LLVM_READNONE
static StringRef getGPUOrDefault(const Triple &TT, StringRef GPU) {
  if (!GPU.empty())
    return GPU;

  if (StringRef Name = AMDGPU::getArchNameFromSubArch(TT.getSubArch());
      !Name.empty())
    return Name;

  // Need to default to a target with flat support for HSA.
  if (TT.isAMDGCN())
    return TT.getOS() == Triple::AMDHSA ? "generic-hsa" : "generic";

  return "r600";
}

static Reloc::Model getEffectiveRelocModel() {
  // The AMDGPU toolchain only supports generating shared objects, so we
  // must always use PIC.
  return Reloc::PIC_;
}

AMDGPUTargetMachine::AMDGPUTargetMachine(const Target &T, const Triple &TT,
                                         StringRef CPU, StringRef FS,
                                         const TargetOptions &Options,
                                         std::optional<Reloc::Model> RM,
                                         std::optional<CodeModel::Model> CM,
                                         CodeGenOptLevel OptLevel)
    : CodeGenTargetMachineImpl(
          T, TT.computeDataLayout(), TT, getGPUOrDefault(TT, CPU), FS, Options,
          getEffectiveRelocModel(), getEffectiveCodeModel(CM, CodeModel::Small),
          OptLevel),
      TLOF(createTLOF(getTargetTriple())) {
  initAsmInfo();
  if (TT.isAMDGCN()) {
    // Triple is missing a representation for non-empty, but unrecognized
    // subarches. Only permit no subarch for any subtarget if it was really
    // empty.
    bool IsUnknownSubArch =
        TT.getSubArch() == Triple::NoSubArch && TT.getArchName().size() != 6;
    if (IsUnknownSubArch)
      reportFatalUsageError("unknown subarch " + TT.getArchName());

    if (TT.getSubArch() != Triple::NoSubArch) {
      AMDGPU::GPUKind Kind = AMDGPU::parseArchAMDGCN(CPU);
      Triple::SubArchType GPUSubArch = AMDGPU::getSubArch(Kind);
      if (Kind != AMDGPU::GK_NONE && GPUSubArch != TT.getSubArch() &&
          TT.getSubArch() != AMDGPU::getMajorSubArch(GPUSubArch)) {
        reportFatalUsageError("invalid cpu '" + CPU + "' for subarch " +
                              TT.getArchName());
      }
    }

    if (getMCSubtargetInfo().checkFeatures("+wavefrontsize64"))
      MRI.reset(llvm::createGCNMCRegisterInfo(AMDGPUDwarfFlavour::Wave64));
    else if (getMCSubtargetInfo().checkFeatures("+wavefrontsize32"))
      MRI.reset(llvm::createGCNMCRegisterInfo(AMDGPUDwarfFlavour::Wave32));
  }
  LLT::setUseExtended(true);
}

AMDGPUTargetMachine::~AMDGPUTargetMachine() = default;

StringRef AMDGPUTargetMachine::getGPUName(const Function &F) const {
  Attribute GPUAttr = F.getFnAttribute("target-cpu");
  return GPUAttr.isValid() ? GPUAttr.getValueAsString() : getTargetCPU();
}

StringRef AMDGPUTargetMachine::getFeatureString(const Function &F) const {
  Attribute FSAttr = F.getFnAttribute("target-features");

  return FSAttr.isValid() ? FSAttr.getValueAsString()
                          : getTargetFeatureString();
}

llvm::ScheduleDAGInstrs *
AMDGPUTargetMachine::createMachineScheduler(MachineSchedContext *C) const {
  const GCNSubtarget &ST = C->MF->getSubtarget<GCNSubtarget>();
  ScheduleDAGMILive *DAG = createSchedLive(C);
  DAG->addMutation(createLoadClusterDAGMutation(DAG->TII, DAG->TRI));
  if (ST.shouldClusterStores())
    DAG->addMutation(createStoreClusterDAGMutation(DAG->TII, DAG->TRI));
  return DAG;
}

/// Predicate for Internalize pass.
static bool mustPreserveGV(const GlobalValue &GV) {
  if (const Function *F = dyn_cast<Function>(&GV))
    return F->isDeclaration() || F->getName().starts_with("__asan_") ||
           F->getName().starts_with("__sanitizer_") ||
           AMDGPU::isEntryFunctionCC(F->getCallingConv());

  GV.removeDeadConstantUsers();
  return !GV.use_empty();
}

void AMDGPUTargetMachine::registerDefaultAliasAnalyses(AAManager &AAM) {
  if (getEnableAMDGPUAliasAnalysis(nullptr, getOptionsContext()))
    AAM.registerFunctionAnalysis<AMDGPUAA>();
}

static Expected<ScanOptions>
parseAMDGPUAtomicOptimizerStrategy(StringRef Params) {
  if (Params.empty())
    return ScanOptions::Iterative;
  Params.consume_front("strategy=");
  auto Result = StringSwitch<std::optional<ScanOptions>>(Params)
                    .Case("dpp", ScanOptions::DPP)
                    .Cases({"iterative", ""}, ScanOptions::Iterative)
                    .Case("none", ScanOptions::None)
                    .Default(std::nullopt);
  if (Result)
    return *Result;
  return make_error<StringError>("invalid parameter", inconvertibleErrorCode());
}

Expected<AMDGPUAttributorOptions>
parseAMDGPUAttributorPassOptions(StringRef Params) {
  AMDGPUAttributorOptions Result;
  while (!Params.empty()) {
    StringRef ParamName;
    std::tie(ParamName, Params) = Params.split(';');
    if (ParamName == "closed-world") {
      Result.IsClosedWorld = true;
    } else {
      return make_error<StringError>(
          formatv("invalid AMDGPUAttributor pass parameter '{0}' ", ParamName)
              .str(),
          inconvertibleErrorCode());
    }
  }
  return Result;
}

void AMDGPUTargetMachine::registerPassBuilderCallbacks(PassBuilder &PB) {

#define GET_PASS_REGISTRY "AMDGPUPassRegistry.def"
#include "llvm/Passes/TargetPassRegistry.inc"

  PB.registerPipelineParsingCallback(
      [this](StringRef Name, CGSCCPassManager &PM,
             ArrayRef<PassBuilder::PipelineElement> Pipeline) {
        if (Name == "amdgpu-attributor-cgscc" && getTargetTriple().isAMDGCN()) {
          PM.addPass(AMDGPUAttributorCGSCCPass(
              *static_cast<GCNTargetMachine *>(this)));
          return true;
        }
        return false;
      });

  PB.registerScalarOptimizerLateEPCallback(
      [](FunctionPassManager &FPM, OptimizationLevel Level) {
        if (Level == OptimizationLevel::O0)
          return;

        FPM.addPass(InferAddressSpacesPass());
      });

  PB.registerVectorizerEndEPCallback(
      [](FunctionPassManager &FPM, OptimizationLevel Level) {
        if (Level == OptimizationLevel::O0)
          return;

        FPM.addPass(InferAddressSpacesPass());
      });

  PB.registerPipelineEarlySimplificationEPCallback(
      [this](ModulePassManager &PM, OptimizationLevel Level,
             ThinOrFullLTOPhase Phase) {
        if (!isLTOPreLink(Phase) && getTargetTriple().isAMDGCN()) {
          // When we are not using -fgpu-rdc, we can run accelerator code
          // selection relatively early, but still after linking to prevent
          // eager removal of potentially reachable symbols.
          if (getEnableHipStdPar(nullptr, getOptionsContext())) {
            PM.addPass(HipStdParMathFixupPass());
            PM.addPass(HipStdParAcceleratorCodeSelectionPass());
          }

          PM.addPass(AMDGPUPrintfRuntimeBindingPass());
        }

        if (Level == OptimizationLevel::O0)
          return;

        // We don't want to run internalization at per-module stage.
        if (getInternalizeSymbols(nullptr, getOptionsContext()) &&
            !isLTOPreLink(Phase)) {
          PM.addPass(InternalizePass(mustPreserveGV));
          PM.addPass(GlobalDCEPass());
        }

        if (getEarlyInlineAll(nullptr, getOptionsContext()) &&
            !AMDGPUTargetMachine::getEnableFunctionCalls(getTargetTriple(),
                                                         getOptionsContext()))
          PM.addPass(AMDGPUAlwaysInlinePass());
      });

  PB.registerPeepholeEPCallback(
      [this](FunctionPassManager &FPM, OptimizationLevel Level) {
        if (Level == OptimizationLevel::O0)
          return;

        FPM.addPass(AMDGPUUseNativeCallsPass());
        if (getEnableLibCallSimplify(nullptr, getOptionsContext()))
          FPM.addPass(AMDGPUSimplifyLibCallsPass());

        if (getEnableUniformIntrinsicCombine(nullptr, getOptionsContext()))
          FPM.addPass(AMDGPUUniformIntrinsicCombinePass());
      });

  PB.registerCGSCCOptimizerLateEPCallback(
      [this](CGSCCPassManager &PM, OptimizationLevel Level) {
        if (Level == OptimizationLevel::O0)
          return;

        FunctionPassManager FPM;

        // Add promote kernel arguments pass to the opt pipeline right before
        // infer address spaces which is needed to do actual address space
        // rewriting.
        if (Level > OptimizationLevel::O1 &&
            getEnablePromoteKernelArguments(nullptr, getOptionsContext()))
          FPM.addPass(AMDGPUPromoteKernelArgumentsPass());

        // Add infer address spaces pass to the opt pipeline after inlining
        // but before SROA to increase SROA opportunities.
        FPM.addPass(InferAddressSpacesPass());

        // This should run after inlining to have any chance of doing
        // anything, and before other cleanup optimizations.
        FPM.addPass(AMDGPULowerKernelAttributesPass());

        // Promote alloca to vector before SROA and loop unroll. If we
        // manage to eliminate allocas before unroll we may choose to unroll
        // less.
        FPM.addPass(AMDGPUPromoteAllocaToVectorPass(*this));

        PM.addPass(createCGSCCToFunctionPassAdaptor(std::move(FPM)));
      });

  // FIXME: Why is AMDGPUAttributor not in CGSCC?
  PB.registerOptimizerLastEPCallback([this](ModulePassManager &MPM,
                                            OptimizationLevel Level,
                                            ThinOrFullLTOPhase Phase) {
    if (Level != OptimizationLevel::O0) {
      if (!isLTOPreLink(Phase)) {
        if (getEnableAMDGPUAttributor(nullptr, getOptionsContext()) &&
            getTargetTriple().isAMDGCN()) {
          AMDGPUAttributorOptions Opts;
          MPM.addPass(AMDGPUAttributorPass(*this, Opts, Phase));
        }
      }
    }
  });

  PB.registerFullLinkTimeOptimizationLastEPCallback(
      [this](ModulePassManager &PM, OptimizationLevel Level) {
        // Clean up redundant memory round-trips that the full-LTO pipeline,
        // unlike the non-LTO/ThinLTO ones, otherwise leaves for codegen.
        if (Level != OptimizationLevel::O0) {
          PM.addPass(createModuleToFunctionPassAdaptor(
              EarlyCSEPass(/*UseMemorySSA=*/true)));
        }

        // When we are using -fgpu-rdc, we can only run accelerator code
        // selection after linking to prevent, otherwise we end up removing
        // potentially reachable symbols that were exported as external in other
        // modules.
        if (getEnableHipStdPar(nullptr, getOptionsContext())) {
          PM.addPass(HipStdParMathFixupPass());
          PM.addPass(HipStdParAcceleratorCodeSelectionPass());
        }
        // We want to support the -lto-partitions=N option as "best effort".
        // For that, we need to lower LDS earlier in the pipeline before the
        // module is partitioned for codegen.
        if (getEnableLowerExecSync(nullptr, getOptionsContext()))
          PM.addPass(AMDGPULowerExecSyncPass());
        if (getEnableSwLowerLDS(nullptr, getOptionsContext()))
          PM.addPass(AMDGPUSwLowerLDSPass());
        if (AMDGPUTargetMachine::getEnableLowerModuleLDS(getOptionsContext()))
          PM.addPass(AMDGPULowerModuleLDSPass(*this));
        if (Level != OptimizationLevel::O0) {
          // We only want to run this with O2 or higher since inliner and SROA
          // don't run in O1.
          if (Level != OptimizationLevel::O1) {
            PM.addPass(
                createModuleToFunctionPassAdaptor(InferAddressSpacesPass()));
          }
          // Do we really need internalization in LTO?
          if (getInternalizeSymbols(nullptr, getOptionsContext())) {
            PM.addPass(InternalizePass(mustPreserveGV));
            PM.addPass(GlobalDCEPass());
          }
          if (getEnableAMDGPUAttributor(nullptr, getOptionsContext()) &&
              getTargetTriple().isAMDGCN()) {
            AMDGPUAttributorOptions Opt;
            if (getHasClosedWorldAssumption(nullptr, getOptionsContext()))
              Opt.IsClosedWorld = true;
            PM.addPass(AMDGPUAttributorPass(
                *this, Opt, ThinOrFullLTOPhase::FullLTOPostLink));
          }
        }
        {
          if (!clv2::getOptValOrDefault<&clv2::CGPASS_NoKernelInfoEndLto>(
                  getOptionsContext())) {
            FunctionPassManager FPM;
            FPM.addPass(KernelInfoPrinter(this));
            PM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
          }
        }
      });

  PB.registerRegClassFilterParsingCallback(
      [](StringRef FilterName) -> RegAllocFilterFunc {
        if (FilterName == "sgpr")
          return onlyAllocateSGPRs;
        if (FilterName == "vgpr")
          return onlyAllocateVGPRs;
        if (FilterName == "wwm")
          return onlyAllocateWWMRegs;
        return nullptr;
      });
}

bool AMDGPUTargetMachine::isNoopAddrSpaceCast(unsigned SrcAS,
                                              unsigned DestAS) const {
  return AMDGPU::isFlatGlobalAddrSpace(SrcAS) &&
         AMDGPU::isFlatGlobalAddrSpace(DestAS);
}

unsigned AMDGPUTargetMachine::getAssumedAddrSpace(const Value *V) const {
  if (auto *Arg = dyn_cast<Argument>(V);
      Arg &&
      AMDGPU::isModuleEntryFunctionCC(Arg->getParent()->getCallingConv()) &&
      !Arg->hasByRefAttr())
    return AMDGPUAS::GLOBAL_ADDRESS;

  const auto *LD = dyn_cast<LoadInst>(V);
  if (!LD) // TODO: Handle invariant load like constant.
    return AMDGPUAS::UNKNOWN_ADDRESS_SPACE;

  // It must be a generic pointer loaded.
  assert(V->getType()->getPointerAddressSpace() == AMDGPUAS::FLAT_ADDRESS);

  const auto *Ptr = LD->getPointerOperand();
  if (Ptr->getType()->getPointerAddressSpace() != AMDGPUAS::CONSTANT_ADDRESS)
    return AMDGPUAS::UNKNOWN_ADDRESS_SPACE;
  // For a generic pointer loaded from the constant memory, it could be assumed
  // as a global pointer since the constant memory is only populated on the
  // host side. As implied by the offload programming model, only global
  // pointers could be referenced on the host side.
  return AMDGPUAS::GLOBAL_ADDRESS;
}

std::pair<const Value *, unsigned>
AMDGPUTargetMachine::getPredicatedAddrSpace(const Value *V) const {
  if (auto *II = dyn_cast<IntrinsicInst>(V)) {
    switch (II->getIntrinsicID()) {
    case Intrinsic::amdgcn_is_shared:
      return std::pair(II->getArgOperand(0), AMDGPUAS::LOCAL_ADDRESS);
    case Intrinsic::amdgcn_is_private:
      return std::pair(II->getArgOperand(0), AMDGPUAS::PRIVATE_ADDRESS);
    default:
      break;
    }
    return std::pair(nullptr, -1);
  }
  // Check the global pointer predication based on
  // (!is_share(p) && !is_private(p)). Note that logic 'and' is commutative and
  // the order of 'is_shared' and 'is_private' is not significant.
  Value *Ptr;
  if (match(
          const_cast<Value *>(V),
          m_c_And(m_Not(m_Intrinsic<Intrinsic::amdgcn_is_shared>(m_Value(Ptr))),
                  m_Not(m_Intrinsic<Intrinsic::amdgcn_is_private>(
                      m_Deferred(Ptr))))))
    return std::pair(Ptr, AMDGPUAS::GLOBAL_ADDRESS);

  return std::pair(nullptr, -1);
}

unsigned
AMDGPUTargetMachine::getAddressSpaceForPseudoSourceKind(unsigned Kind) const {
  switch (Kind) {
  case PseudoSourceValue::Stack:
  case PseudoSourceValue::FixedStack:
    return AMDGPUAS::PRIVATE_ADDRESS;
  case PseudoSourceValue::ConstantPool:
  case PseudoSourceValue::GOT:
  case PseudoSourceValue::JumpTable:
  case PseudoSourceValue::GlobalValueCallEntry:
  case PseudoSourceValue::ExternalSymbolCallEntry:
    return AMDGPUAS::CONSTANT_ADDRESS;
  }
  return AMDGPUAS::FLAT_ADDRESS;
}

bool AMDGPUTargetMachine::splitModule(
    Module &M, unsigned NumParts,
    function_ref<void(std::unique_ptr<Module> MPart)> ModuleCallback,
    const AMDGPUSplitModuleOptions *Opts) {
  // FIXME(?): Would be better to use an already existing Analysis/PassManager,
  // but all current users of this API don't have one ready and would need to
  // create one anyway. Let's hide the boilerplate for now to keep it simple.

  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;

  PassBuilder PB(this->getOptionsContext(), this,
                 PipelineTuningOptions(this->getOptionsContext()), std::nullopt,
                 /*PIC=*/nullptr, vfs::getRealFileSystem());
  PB.registerModuleAnalyses(MAM);
  PB.registerFunctionAnalyses(FAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  ModulePassManager MPM;
  AMDGPUSplitModuleOptions DefaultOpts;
  MPM.addPass(AMDGPUSplitModulePass(NumParts, ModuleCallback,
                                    Opts ? *Opts : DefaultOpts));
  MPM.run(M, MAM);
  return true;
}

//===----------------------------------------------------------------------===//
// GCN Target Machine (SI+)
//===----------------------------------------------------------------------===//

GCNTargetMachine::GCNTargetMachine(const Target &T, const Triple &TT,
                                   StringRef CPU, StringRef FS,
                                   const TargetOptions &Options,
                                   std::optional<Reloc::Model> RM,
                                   std::optional<CodeModel::Model> CM,
                                   CodeGenOptLevel OL, bool JIT)
    : AMDGPUTargetMachine(T, TT, CPU, FS, Options, RM, CM, OL) {
  setEnableDefaultMachineVerifier(false);
}

enum class OOBFlagValue {
  Any = 0,
  Relaxed = 1,
  Strict = 2,
};

/// Returns the OOB mode encoded by a module flag.
/// An absent flag defaults to Any.
static OOBFlagValue getOOBFlagValue(const Module &M, StringRef FlagName) {
  const auto *Flag =
      mdconst::dyn_extract_or_null<ConstantInt>(M.getModuleFlag(FlagName));
  if (!Flag)
    return OOBFlagValue::Any;
  return static_cast<OOBFlagValue>(Flag->getZExtValue());
}

/// Returns the xnack/sramecc setting encoded by a module flag.
/// Module flag values: 0 = disabled, 1 = enabled.
/// An absent flag defaults to Any.
AMDGPU::TargetIDSetting
GCNTargetMachine::getTargetIDSettingFromModuleFlag(const Module &M,
                                                   StringRef FlagName) {
  using AMDGPU::TargetIDSetting;

  const auto &Ctx = M.getContext().getOptionsContext();
  if (clv2::wasOptSpecified<&clv2::AMDGPUOptsReg, &clv2::AMDGPU_XnackSetting>(
          Ctx) &&
      FlagName == "amdgpu.xnack")
    return clv2::getOptValOr<&clv2::AMDGPUOptsReg, &clv2::AMDGPU_XnackSetting>(
               Ctx, false)
               ? TargetIDSetting::On
               : TargetIDSetting::Off;
  if (clv2::wasOptSpecified<&clv2::AMDGPUOptsReg, &clv2::AMDGPU_SramEccSetting>(
          Ctx) &&
      FlagName == "amdgpu.sramecc")
    return clv2::getOptValOr<&clv2::AMDGPUOptsReg,
                             &clv2::AMDGPU_SramEccSetting>(Ctx, false)
               ? TargetIDSetting::On
               : TargetIDSetting::Off;

  const auto *Flag =
      mdconst::dyn_extract_or_null<ConstantInt>(M.getModuleFlag(FlagName));
  if (!Flag)
    return TargetIDSetting::Any;
  return Flag->getZExtValue() == 0 ? TargetIDSetting::Off : TargetIDSetting::On;
}

const TargetSubtargetInfo *
GCNTargetMachine::getSubtargetImpl(const Function &F) const {
  StringRef GPU = getGPUName(F);
  StringRef FS = getFeatureString(F);

  const Module &M = *F.getParent();
  OOBFlagValue BufOOB = getOOBFlagValue(M, AMDGPUOOBMode::BufferFlag);
  OOBFlagValue TBufOOB = getOOBFlagValue(M, AMDGPUOOBMode::TBufferFlag);
  bool BufRelaxed = BufOOB == OOBFlagValue::Relaxed;
  bool TBufRelaxed = TBufOOB == OOBFlagValue::Relaxed;

  using AMDGPU::TargetIDSetting;
  TargetIDSetting Xnack = getTargetIDSettingFromModuleFlag(M, "amdgpu.xnack");
  TargetIDSetting SramEcc =
      getTargetIDSettingFromModuleFlag(M, "amdgpu.sramecc");

  SmallString<128> SubtargetKey(GPU);
  SubtargetKey.append(FS);
  if (BufRelaxed)
    SubtargetKey.append(",buf-oob=1");
  if (TBufRelaxed)
    SubtargetKey.append(",tbuf-oob=1");
  if (Xnack != TargetIDSetting::Any) {
    SubtargetKey.append(",xnack=");
    SubtargetKey.push_back(Xnack == TargetIDSetting::On ? '1' : '0');
  }
  if (SramEcc != TargetIDSetting::Any) {
    SubtargetKey.append(",sramecc=");
    SubtargetKey.push_back(Xnack == TargetIDSetting::On ? '1' : '0');
  }

  auto &I = SubtargetMap[SubtargetKey];
  if (!I) {
    AMDGPU::GPUKind Kind = AMDGPU::parseArchAMDGCN(GPU);
    Triple::SubArchType GPUSubArch = AMDGPU::getSubArch(Kind);

    // Enforce the subtarget is covered by the subarch. Tolerate no subarch for
    // legacy compatibility.
    const Triple &TT = M.getTargetTriple();
    if (GPUSubArch != TT.getSubArch() && Kind != AMDGPU::GK_NONE) {
      // Check if this is a generic subarch which has subtargets. Ignore
      // unknown subtargets with a known subarch, since for whatever reason
      // the convention is to just print a warning and ignore unrecognized
      // subtargets.
      bool IsLegacyEmptySubArch = TT.getSubArch() == Triple::NoSubArch;
      if (!IsLegacyEmptySubArch &&
          AMDGPU::getMajorSubArch(GPUSubArch) != TT.getSubArch()) {
        F.getContext().emitError("invalid subtarget '" + Twine(GPU) +
                                 "' for subarch " + TT.getArchName());
      }
    }

    I = std::make_unique<GCNSubtarget>(TargetTriple, GPU, FS, *this, BufRelaxed,
                                       TBufRelaxed, Xnack, SramEcc);
  }

  I->setScalarizeGlobalBehavior(getScalarizeGlobal(&F, getOptionsContext()));

  return I.get();
}

TargetTransformInfo
GCNTargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(std::make_unique<GCNTTIImpl>(this, F));
}

Error GCNTargetMachine::buildCodeGenPipeline(
    ModulePassManager &MPM, ModuleAnalysisManager &MAM, raw_pwrite_stream &Out,
    raw_pwrite_stream *DwoOut, CodeGenFileType FileType,
    const CGPassBuilderOption &Opts, MCContext &Ctx,
    PassInstrumentationCallbacks *PIC) {
  AMDGPUCodeGenPassBuilder CGPB(*this, Opts, PIC);
  return CGPB.buildPipeline(MPM, MAM, Out, DwoOut, FileType, Ctx);
}

ScheduleDAGInstrs *
GCNTargetMachine::createMachineScheduler(MachineSchedContext *C) const {
  const GCNSubtarget &ST = C->MF->getSubtarget<GCNSubtarget>();
  if (ST.enableSIScheduler())
    return createSIMachineScheduler(C);

  StringRef SchedStrategy = AMDGPU::getSchedStrategy(C->MF->getFunction());

  if (SchedStrategy == "max-ilp")
    return createGCNMaxILPMachineScheduler(C);

  if (SchedStrategy == "max-memory-clause")
    return createGCNMaxMemoryClauseMachineScheduler(C);

  if (SchedStrategy == "iterative-ilp")
    return createIterativeILPMachineScheduler(C);

  if (SchedStrategy == "iterative-minreg")
    return createMinRegScheduler(C);

  if (SchedStrategy == "iterative-maxocc")
    return createIterativeGCNMaxOccupancyMachineScheduler(C);

  if (SchedStrategy == "coexec") {
    diagnoseUnsupportedCoExecSchedulerSelection(C->MF->getFunction(), ST);
    return createGCNCoExecMachineScheduler(C);
  }

  return createGCNMaxOccupancyMachineScheduler(C);
}

ScheduleDAGInstrs *
GCNTargetMachine::createPostMachineScheduler(MachineSchedContext *C) const {
  if (useNoopPostScheduler(C->MF->getFunction()))
    return createGCNNoopPostMachineScheduler(C);

  ScheduleDAGMI *DAG =
      new GCNPostScheduleDAGMILive(C, std::make_unique<PostGenericScheduler>(C),
                                   /*RemoveKillFlags=*/true);
  const GCNSubtarget &ST = C->MF->getSubtarget<GCNSubtarget>();
  DAG->addMutation(createLoadClusterDAGMutation(DAG->TII, DAG->TRI));
  if (ST.shouldClusterStores())
    DAG->addMutation(createStoreClusterDAGMutation(DAG->TII, DAG->TRI));
  DAG->addMutation(createIGroupLPDAGMutation(AMDGPU::SchedulingPhase::PostRA));
  const Function &F = C->MF->getFunction();
  if ((getEnableVOPDWasSpecified(&F, getOptionsContext()) ||
       getOptLevel() >= CodeGenOptLevel::Less) &&
      getEnableVOPD(&F, getOptionsContext()))
    DAG->addMutation(createVOPDPairingMutation());
  DAG->addMutation(createAMDGPUExportClusteringDAGMutation());
  DAG->addMutation(createAMDGPUBarrierLatencyDAGMutation(C->MF));
  DAG->addMutation(createAMDGPUHazardLatencyDAGMutation(C->MF));
  return DAG;
}
//===----------------------------------------------------------------------===//
// AMDGPU Legacy Pass Setup
//===----------------------------------------------------------------------===//

std::unique_ptr<CSEConfigBase> llvm::AMDGPUPassConfig::getCSEConfig() const {
  return getStandardCSEConfigForOpt(TM->getOptLevel());
}

namespace {

class GCNPassConfig final : public AMDGPUPassConfig {
public:
  GCNPassConfig(TargetMachine &TM, PassManagerBase &PM)
      : AMDGPUPassConfig(TM, PM) {
    substitutePass(&PostRASchedulerID, &PostMachineSchedulerID);
  }

  GCNTargetMachine &getGCNTargetMachine() const {
    return getTM<GCNTargetMachine>();
  }

  bool addPreISel() override;
  void addMachineSSAOptimization() override;
  bool addILPOpts() override;
  bool addInstSelector() override;
  bool addIRTranslator() override;
  void addPreLegalizeMachineIR() override;
  bool addLegalizeMachineIR() override;
  void addPreRegBankSelect() override;
  bool addRegBankSelect() override;
  void addPreGlobalInstructionSelect() override;
  bool addGlobalInstructionSelect() override;
  void addPreRegAlloc() override;
  void addFastRegAlloc() override;
  void addOptimizedRegAlloc() override;

  FunctionPass *createSGPRAllocPass(bool Optimized);
  FunctionPass *createVGPRAllocPass(bool Optimized);
  FunctionPass *createWWMRegAllocPass(bool Optimized);
  FunctionPass *createRegAllocPass(bool Optimized) override;

  bool addRegAssignAndRewriteFast() override;
  bool addRegAssignAndRewriteOptimized() override;

  bool addPreRewrite() override;
  void addPostRegAlloc() override;
  void addPreSched2() override;
  void addPreEmitPass() override;
  void addPostBBSections() override;
};

} // end anonymous namespace

AMDGPUPassConfig::AMDGPUPassConfig(TargetMachine &TM, PassManagerBase &PM)
    : TargetPassConfig(TM, PM) {
  // Exceptions and StackMaps are not supported, so these passes will never do
  // anything.
  disablePass(&StackMapLivenessID);
  disablePass(&FuncletLayoutID);
  // Garbage collection is not supported.
  disablePass(&GCLoweringID);
  disablePass(&ShadowStackGCLoweringID);
}

void AMDGPUPassConfig::addEarlyCSEOrGVNPass() {
  if (getOptLevel() == CodeGenOptLevel::Aggressive)
    addPass(createGVNPass());
  else
    addPass(createEarlyCSEPass());
}

void AMDGPUPassConfig::addStraightLineScalarOptimizationPasses() {
  if (isPassEnabled(
          getEnableLoopPrefetch(nullptr, TM->getOptionsContext()),
          getEnableLoopPrefetchWasSpecified(nullptr, TM->getOptionsContext()),
          CodeGenOptLevel::Aggressive))
    addPass(createLoopDataPrefetchPass());
  addPass(createSeparateConstOffsetFromGEPPass());
  // ReassociateGEPs exposes more opportunities for SLSR. See
  // the example in reassociate-geps-and-slsr.ll.
  addPass(createStraightLineStrengthReducePass());
  // SeparateConstOffsetFromGEP and SLSR creates common expressions which GVN or
  // EarlyCSE can reuse.
  addEarlyCSEOrGVNPass();
  // Run NaryReassociate after EarlyCSE/GVN to be more effective.
  addPass(createNaryReassociatePass());
  // NaryReassociate on GEPs creates redundant common expressions, so run
  // EarlyCSE after it.
  addPass(createEarlyCSEPass());
}

void AMDGPUPassConfig::addIRPasses() {
  const AMDGPUTargetMachine &TM = getAMDGPUTargetMachine();

  if (getRemoveIncompatibleFunctions(nullptr, TM.getOptionsContext()) &&
      TM.getTargetTriple().isAMDGCN())
    addPass(createAMDGPURemoveIncompatibleFunctionsPass(&TM));

  // There is no reason to run these.
  disablePass(&StackMapLivenessID);
  disablePass(&FuncletLayoutID);
  disablePass(&PatchableFunctionID);

  if (TM.getTargetTriple().isAMDGCN())
    addPass(createAMDGPUPrintfRuntimeBinding());

  if (getLowerCtorDtor(nullptr, TM.getOptionsContext()))
    addPass(createAMDGPUCtorDtorLoweringLegacyPass());

  if (TM.getTargetTriple().isAMDGCN() &&
      isPassEnabled(
          getEnableImageIntrinsicOptimizer(nullptr, TM.getOptionsContext()),
          getEnableImageIntrinsicOptimizerWasSpecified(nullptr,
                                                       TM.getOptionsContext())))
    addPass(createAMDGPUImageIntrinsicOptimizerPass(&TM));

  if (getEnableUniformIntrinsicCombine(nullptr, TM.getOptionsContext()))
    addPass(createAMDGPUUniformIntrinsicCombineLegacyPass());

  // This can be disabled by passing ::Disable here or on the command line
  // with --expand-variadics-override=disable.
  addPass(createExpandVariadicsPass(ExpandVariadicsMode::Lowering));

  // Function calls are not supported, so make sure we inline everything.
  addPass(createAMDGPUAlwaysInlinePass());
  addPass(createAlwaysInlinerLegacyPass());

  // Handle uses of OpenCL image2d_t, image3d_t and sampler_t arguments.
  if (TM.getTargetTriple().getArch() == Triple::r600)
    addPass(createR600OpenCLImageTypeLoweringPass());

  // Make enqueued block runtime handles externally visible.
  addPass(createAMDGPUExportKernelRuntimeHandlesLegacyPass());

  // Lower special LDS accesses.
  if (getEnableLowerExecSync(nullptr, TM.getOptionsContext()))
    addPass(createAMDGPULowerExecSyncLegacyPass());

  // Lower LDS accesses to global memory pass if address sanitizer is enabled.
  if (getEnableSwLowerLDS(nullptr, TM.getOptionsContext()))
    addPass(createAMDGPUSwLowerLDSLegacyPass());

  // Runs before PromoteAlloca so the latter can account for function uses
  if (AMDGPUTargetMachine::getEnableLowerModuleLDS(TM.getOptionsContext())) {
    addPass(createAMDGPULowerModuleLDSLegacyPass(&TM));
  }

  // Run atomic optimizer before Atomic Expand
  if ((TM.getTargetTriple().isAMDGCN()) &&
      (TM.getOptLevel() >= CodeGenOptLevel::Less) &&
      (getAMDGPUAtomicOptimizerStrategy(nullptr, TM.getOptionsContext()) !=
       ScanOptions::None)) {
    addPass(createAMDGPUAtomicOptimizerPass(
        getAMDGPUAtomicOptimizerStrategy(nullptr, TM.getOptionsContext())));
  }

  addPass(createAtomicExpandLegacyPass());

  if (TM.getOptLevel() > CodeGenOptLevel::None) {
    addPass(createAMDGPUPromoteAlloca());

    if (isPassEnabled(getEnableScalarIRPasses(nullptr, TM.getOptionsContext()),
                      getEnableScalarIRPassesWasSpecified(
                          nullptr, TM.getOptionsContext())))
      addStraightLineScalarOptimizationPasses();

    if (getEnableAMDGPUAliasAnalysis(nullptr, TM.getOptionsContext())) {
      addPass(createAMDGPUAAWrapperPass());
      addPass(createExternalAAWrapperPass([](Pass &P, Function &,
                                             AAResults &AAR) {
        if (auto *WrapperPass = P.getAnalysisIfAvailable<AMDGPUAAWrapperPass>())
          AAR.addAAResult(WrapperPass->getResult());
        }));
    }

    if (TM.getTargetTriple().isAMDGCN()) {
      // TODO: May want to move later or split into an early and late one.
      {
        bool ExpandDiv =
            clv2::getOptValOrDefault<&clv2::AMDGPU_ExpandDiv64InIR>(
                TM.getOptionsContext());
        addPass(createAMDGPUCodeGenPreparePass(ExpandDiv));
      }
    }

    // Try to hoist loop invariant parts of divisions AMDGPUCodeGenPrepare may
    // have expanded.
    if (TM.getOptLevel() > CodeGenOptLevel::Less)
      addPass(createLICMPass());
  }

  TargetPassConfig::addIRPasses();

  // EarlyCSE is not always strong enough to clean up what LSR produces. For
  // example, GVN can combine
  //
  //   %0 = add %a, %b
  //   %1 = add %b, %a
  //
  // and
  //
  //   %0 = shl nsw %a, 2
  //   %1 = shl %a, 2
  //
  // but EarlyCSE can do neither of them.
  if (isPassEnabled(
          getEnableScalarIRPasses(nullptr, TM.getOptionsContext()),
          getEnableScalarIRPassesWasSpecified(nullptr, TM.getOptionsContext())))
    addEarlyCSEOrGVNPass();
}

void AMDGPUPassConfig::addCodeGenPrepare() {
  if (TM->getTargetTriple().isAMDGCN() &&
      TM->getOptLevel() > CodeGenOptLevel::None)
    addPass(createAMDGPUPreloadKernelArgumentsLegacyPass(TM));

  if (TM->getTargetTriple().isAMDGCN() &&
      getEnableLowerKernelArguments(nullptr, TM->getOptionsContext()))
    addPass(createAMDGPULowerKernelArgumentsPass());

  TargetPassConfig::addCodeGenPrepare();

  if (isPassEnabled(
          getEnableLoadStoreVectorizer(nullptr, TM->getOptionsContext()),
          getEnableLoadStoreVectorizerWasSpecified(nullptr,
                                                   TM->getOptionsContext())))
    addPass(createLoadStoreVectorizerPass());

  if (TM->getTargetTriple().isAMDGCN()) {
    // This lowering has been placed after codegenprepare to take advantage of
    // address mode matching (which is why it isn't put with the LDS lowerings).
    // It could be placed anywhere before uniformity annotations (an analysis
    // that it changes by splitting up fat pointers into their components)
    // but has been put before switch lowering and CFG flattening so that those
    // passes can run on the more optimized control flow this pass creates in
    // many cases.
    addPass(createAMDGPULowerBufferFatPointersPass());
    addPass(createAMDGPULowerIntrinsicsLegacyPass());
  }

  // LowerSwitch pass may introduce unreachable blocks that can
  // cause unexpected behavior for subsequent passes. Placing it
  // here seems better that these blocks would get cleaned up by
  // UnreachableBlockElim inserted next in the pass flow.
  addPass(createLowerSwitchPass());
}

bool AMDGPUPassConfig::addPreISel() {
  if (TM->getOptLevel() > CodeGenOptLevel::None)
    addPass(createFlattenCFGPass());
  return false;
}

bool AMDGPUPassConfig::addInstSelector() {
  addPass(createAMDGPUISelDag(getAMDGPUTargetMachine(), getOptLevel()));
  return false;
}

bool AMDGPUPassConfig::addGCPasses() {
  // Do nothing. GC is not supported.
  return false;
}

//===----------------------------------------------------------------------===//
// GCN Legacy Pass Setup
//===----------------------------------------------------------------------===//

bool GCNPassConfig::addPreISel() {
  AMDGPUPassConfig::addPreISel();

  if (TM->getOptLevel() > CodeGenOptLevel::None) {
    addPass(createSinkingPass());
    addPass(createAMDGPULateCodeGenPrepareLegacyPass());
  }

  // Merge divergent exit nodes. StructurizeCFG won't recognize the multi-exit
  // regions formed by them.
  addPass(createAMDGPUUnifyDivergentExitNodesPass(TM->getOptionsContext()));
  addPass(createFixIrreduciblePass());
  addPass(createUnifyLoopExitsPass());
  {
    bool SkipUniform =
        clv2::getOptValIfSpecified<&clv2::ScalarOptsReg,
                                   &clv2::SC_StructurizecfgSkipUniformRegions>(
            TM->getOptionsContext(), false);
    addPass(createStructurizeCFGPass(SkipUniform));
  }

  addPass(createAMDGPUAnnotateUniformValuesLegacy());
  addPass(createSIAnnotateControlFlowLegacyPass());
  // TODO: Move this right after structurizeCFG to avoid extra divergence
  // analysis. This depends on stopping SIAnnotateControlFlow from making
  // control flow modifications.
  addPass(createAMDGPURewriteUndefForPHILegacyPass());

  // SDAG requires LCSSA, GlobalISel does not. Disable LCSSA for -global-isel
  // without any of the fallback options.
  if (!getCGPassBuilderOption(TM->getOptionsContext())
           .EnableGlobalISelOption.value_or(false) ||
      !isGlobalISelAbortEnabled())
    addPass(createLCSSAPass());

  if (TM->getOptLevel() > CodeGenOptLevel::Less)
    addPass(&AMDGPUPerfHintAnalysisLegacyID);

  return false;
}

void GCNPassConfig::addMachineSSAOptimization() {
  TargetPassConfig::addMachineSSAOptimization();

  // We want to fold operands after PeepholeOptimizer has run (or as part of
  // it), because it will eliminate extra copies making it easier to fold the
  // real source operand. We want to eliminate dead instructions after, so that
  // we see fewer uses of the copies. We then need to clean up the dead
  // instructions leftover after the operands are folded as well.
  //
  // XXX - Can we get away without running DeadMachineInstructionElim again?
  addPass(&SIFoldOperandsLegacyID);
  if (getEnableDPPCombine(nullptr, TM->getOptionsContext()))
    addPass(&GCNDPPCombineLegacyID);
  addPass(&SILoadStoreOptimizerLegacyID);
  if (isPassEnabled(getEnableSDWAPeephole(nullptr, TM->getOptionsContext()),
                    getEnableSDWAPeepholeWasSpecified(
                        nullptr, TM->getOptionsContext()))) {
    addPass(&SIPeepholeSDWALegacyID);
    addPass(&EarlyMachineLICMID);
    addPass(&MachineCSELegacyID);
    addPass(&SIFoldOperandsLegacyID);
  }
  addPass(&DeadMachineInstructionElimID);
  addPass(createSIShrinkInstructionsLegacyPass());
}

bool GCNPassConfig::addILPOpts() {
  if (getEnableEarlyIfConversion(nullptr, TM->getOptionsContext()))
    addPass(&EarlyIfConverterLegacyID);

  TargetPassConfig::addILPOpts();
  return false;
}

bool GCNPassConfig::addInstSelector() {
  AMDGPUPassConfig::addInstSelector();
  addPass(&SIFixSGPRCopiesLegacyID);
  addPass(createSILowerI1CopiesLegacyPass());
  return false;
}

bool GCNPassConfig::addIRTranslator() {
  addPass(new IRTranslatorLegacy(getOptLevel()));
  return false;
}

void GCNPassConfig::addPreLegalizeMachineIR() {
  bool IsOptNone = getOptLevel() == CodeGenOptLevel::None;
  addPass(createAMDGPUPreLegalizeCombiner(IsOptNone));
  addPass(new LocalizerLegacy());
}

bool GCNPassConfig::addLegalizeMachineIR() {
  addPass(new LegalizerLegacy());
  return false;
}

void GCNPassConfig::addPreRegBankSelect() {
  bool IsOptNone = getOptLevel() == CodeGenOptLevel::None;
  addPass(createAMDGPUPostLegalizeCombiner(IsOptNone));
  addPass(createAMDGPUGlobalISelDivergenceLoweringPass());
}

bool GCNPassConfig::addRegBankSelect() {
  addPass(createAMDGPURegBankSelectPass());
  addPass(createAMDGPURegBankLegalizePass());
  return false;
}

void GCNPassConfig::addPreGlobalInstructionSelect() {
  bool IsOptNone = getOptLevel() == CodeGenOptLevel::None;
  addPass(createAMDGPURegBankCombiner(IsOptNone));
}

bool GCNPassConfig::addGlobalInstructionSelect() {
  addPass(new InstructionSelectLegacy(getOptLevel()));
  return false;
}

void GCNPassConfig::addFastRegAlloc() {
  // FIXME: We have to disable the verifier here because of PHIElimination +
  // TwoAddressInstructions disabling it.

  // This must be run immediately after phi elimination and before
  // TwoAddressInstructions, otherwise the processing of the tied operand of
  // SI_ELSE will introduce a copy of the tied operand source after the else.
  insertPass(&PHIEliminationID, &SILowerControlFlowLegacyID);

  insertPass(&TwoAddressInstructionPassID, &SIWholeQuadModeID);

  TargetPassConfig::addFastRegAlloc();
}

void GCNPassConfig::addPreRegAlloc() {
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(&AMDGPUPrepareAGPRAllocLegacyID);
  if (getOptLevel() >= CodeGenOptLevel::Default &&
      getEnableMachinePipeliner(TM->getOptionsContext()))
    addPass(&MachinePipelinerID);
}

void GCNPassConfig::addOptimizedRegAlloc() {
  if (getEnableDCEInRA(nullptr, TM->getOptionsContext()))
    insertPass(&DetectDeadLanesID, &DeadMachineInstructionElimID);

  // FIXME: when an instruction has a Killed operand, and the instruction is
  // inside a bundle, seems only the BUNDLE instruction appears as the Kills of
  // the register in LiveVariables, this would trigger a failure in verifier,
  // we should fix it and enable the verifier.
  if (getOptVGPRLiveRange(nullptr, TM->getOptionsContext()))
    insertPass(&LiveVariablesID, &SIOptimizeVGPRLiveRangeLegacyID);

  // This must be run immediately after phi elimination and before
  // TwoAddressInstructions, otherwise the processing of the tied operand of
  // SI_ELSE will introduce a copy of the tied operand source after the else.
  insertPass(&PHIEliminationID, &SILowerControlFlowLegacyID);

  if (getEnableRewritePartialRegUses(nullptr, TM->getOptionsContext()))
    insertPass(&RenameIndependentSubregsID, &GCNRewritePartialRegUsesID);

  if (isPassEnabled(
          getEnablePreRAOptimizations(nullptr, TM->getOptionsContext()),
          getEnablePreRAOptimizationsWasSpecified(nullptr,
                                                  TM->getOptionsContext())))
    insertPass(&MachineSchedulerID, &GCNPreRAOptimizationsID);

  // Allow the scheduler to run before SIWholeQuadMode inserts exec manipulation
  // instructions that cause scheduling barriers.
  insertPass(&MachineSchedulerID, &SIWholeQuadModeID);

  if (getOptExecMaskPreRA(nullptr, TM->getOptionsContext()))
    insertPass(&MachineSchedulerID, &SIOptimizeExecMaskingPreRAID);

  // This is not an essential optimization and it has a noticeable impact on
  // compilation time, so we only enable it from O2.
  if (TM->getOptLevel() > CodeGenOptLevel::Less)
    insertPass(&MachineSchedulerID, &SIFormMemoryClausesID);

  TargetPassConfig::addOptimizedRegAlloc();
}

bool GCNPassConfig::addPreRewrite() {
  if (getEnableRegReassign(nullptr, TM->getOptionsContext()))
    addPass(&GCNNSAReassignID);

  addPass(&AMDGPURewriteAGPRCopyMFMALegacyID);
  return true;
}

FunctionPass *GCNPassConfig::createSGPRAllocPass(bool Optimized) {
  auto Ctor = resolveAMDGPURegAlloc<&OI_SGPRRegAlloc, SGPRRegisterRegAlloc>(
      TM->getOptionsContext());
  if (Ctor != useDefaultRegisterAllocator)
    return Ctor();

  if (Optimized)
    return createGreedyRegisterAllocator(onlyAllocateSGPRs);

  return createFastRegisterAllocator(onlyAllocateSGPRs, false);
}

FunctionPass *GCNPassConfig::createVGPRAllocPass(bool Optimized) {
  auto Ctor = resolveAMDGPURegAlloc<&OI_VGPRRegAlloc, VGPRRegisterRegAlloc>(
      TM->getOptionsContext());
  if (Ctor != useDefaultRegisterAllocator)
    return Ctor();

  if (Optimized)
    return createGreedyVGPRRegisterAllocator();

  return createFastVGPRRegisterAllocator();
}

FunctionPass *GCNPassConfig::createWWMRegAllocPass(bool Optimized) {
  auto Ctor = resolveAMDGPURegAlloc<&OI_WWMRegAlloc, WWMRegisterRegAlloc>(
      TM->getOptionsContext());
  if (Ctor != useDefaultRegisterAllocator)
    return Ctor();

  if (Optimized)
    return createGreedyWWMRegisterAllocator();

  return createFastWWMRegisterAllocator();
}

FunctionPass *GCNPassConfig::createRegAllocPass(bool Optimized) {
  llvm_unreachable("should not be used");
}

static const char RegAllocOptNotSupportedMessage[] =
    "-regalloc not supported with amdgcn. Use -sgpr-regalloc, -wwm-regalloc, "
    "and -vgpr-regalloc";

bool GCNPassConfig::addRegAssignAndRewriteFast() {
  if (!usingDefaultRegAlloc())
    reportFatalUsageError(RegAllocOptNotSupportedMessage);

  addPass(&GCNPreRALongBranchRegID);

  addPass(createSGPRAllocPass(false));

  // Equivalent of PEI for SGPRs.
  addPass(&SILowerSGPRSpillsLegacyID);

  // To Allocate wwm registers used in whole quad mode operations (for shaders).
  addPass(&SIPreAllocateWWMRegsLegacyID);

  // For allocating other wwm register operands.
  addPass(createWWMRegAllocPass(false));

  addPass(&SILowerWWMCopiesLegacyID);
  addPass(&AMDGPUReserveWWMRegsLegacyID);

  // For allocating per-thread VGPRs.
  addPass(createVGPRAllocPass(false));

  return true;
}

bool GCNPassConfig::addRegAssignAndRewriteOptimized() {
  if (!usingDefaultRegAlloc())
    reportFatalUsageError(RegAllocOptNotSupportedMessage);

  addPass(&GCNPreRALongBranchRegID);

  addPass(createSGPRAllocPass(true));

  // Commit allocated register changes. This is mostly necessary because too
  // many things rely on the use lists of the physical registers, such as the
  // verifier. This is only necessary with allocators which use LiveIntervals,
  // since FastRegAlloc does the replacements itself.
  addPass(createVirtRegRewriter(false));

  // At this point, the sgpr-regalloc has been done and it is good to have the
  // stack slot coloring to try to optimize the SGPR spill stack indices before
  // attempting the custom SGPR spill lowering.
  addPass(&StackSlotColoringID);

  // Equivalent of PEI for SGPRs.
  addPass(&SILowerSGPRSpillsLegacyID);

  // To Allocate wwm registers used in whole quad mode operations (for shaders).
  addPass(&SIPreAllocateWWMRegsLegacyID);

  // For allocating other whole wave mode registers.
  addPass(createWWMRegAllocPass(true));
  addPass(&SILowerWWMCopiesLegacyID);
  addPass(createVirtRegRewriter(false));
  addPass(&AMDGPUReserveWWMRegsLegacyID);

  // For allocating per-thread VGPRs.
  addPass(createVGPRAllocPass(true));

  addPreRewrite();
  addPass(&VirtRegRewriterID);

  addPass(&AMDGPUMarkLastScratchLoadID);

  return true;
}

void GCNPassConfig::addPostRegAlloc() {
  addPass(&SIFixVGPRCopiesID);
  if (getOptLevel() > CodeGenOptLevel::None)
    addPass(&SIOptimizeExecMaskingLegacyID);
  TargetPassConfig::addPostRegAlloc();
}

void GCNPassConfig::addPreSched2() {
  if (TM->getOptLevel() > CodeGenOptLevel::None)
    addPass(createSIShrinkInstructionsLegacyPass());
  addPass(&SIPostRABundlerLegacyID);
}

void GCNPassConfig::addPreEmitPass() {
  if (isPassEnabled(getEnableVOPD(nullptr, TM->getOptionsContext()),
                    getEnableVOPDWasSpecified(nullptr, TM->getOptionsContext()),
                    CodeGenOptLevel::Less))
    addPass(&GCNCreateVOPDID);
  addPass(createSIMemoryLegalizerPass());
  addPass(createSIInsertWaitcntsPass());

  addPass(createSIModeRegisterPass());

  if (getOptLevel() > CodeGenOptLevel::None)
    addPass(&SIInsertHardClausesID);

  addPass(&SILateBranchLoweringPassID);
  if (isPassEnabled(getEnableSetWavePriority(nullptr, TM->getOptionsContext()),
                    getEnableSetWavePriorityWasSpecified(
                        nullptr, TM->getOptionsContext()),
                    CodeGenOptLevel::Less))
    addPass(createAMDGPUSetWavePriorityPass());
  if (getOptLevel() > CodeGenOptLevel::None)
    addPass(&SIPreEmitPeepholeID);
  // The hazard recognizer that runs as part of the post-ra scheduler does not
  // guarantee to be able handle all hazards correctly. This is because if there
  // are multiple scheduling regions in a basic block, the regions are scheduled
  // bottom up, so when we begin to schedule a region we don't know what
  // instructions were emitted directly before it.
  //
  // Here we add a stand-alone hazard recognizer pass which can handle all
  // cases.
  addPass(&PostRAHazardRecognizerID);

  addPass(&AMDGPUWaitSGPRHazardsLegacyID);

  addPass(&AMDGPULowerVGPREncodingLegacyID);

  if (isPassEnabled(
          getEnableInsertDelayAlu(nullptr, TM->getOptionsContext()),
          getEnableInsertDelayAluWasSpecified(nullptr, TM->getOptionsContext()),
          CodeGenOptLevel::Less))
    addPass(&AMDGPUInsertDelayAluID);

  addPass(&BranchRelaxationPassID);
}

void GCNPassConfig::addPostBBSections() {
  // We run this later to avoid passes like livedebugvalues and BBSections
  // having to deal with the apparent multi-entry functions we may generate.
  addPass(createAMDGPUPreloadKernArgPrologLegacyPass());
}

TargetPassConfig *GCNTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new GCNPassConfig(*this, PM);
}

void GCNTargetMachine::registerMachineRegisterInfoCallback(
    MachineFunction &MF) const {
  SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  MF.getRegInfo().addDelegate(MFI);
}

MachineFunctionInfo *GCNTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return SIMachineFunctionInfo::create<SIMachineFunctionInfo>(
      Allocator, F, static_cast<const GCNSubtarget *>(STI));
}

yaml::MachineFunctionInfo *GCNTargetMachine::createDefaultFuncInfoYAML() const {
  return new yaml::SIMachineFunctionInfo();
}

yaml::MachineFunctionInfo *
GCNTargetMachine::convertFuncInfoToYAML(const MachineFunction &MF) const {
  const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  return new yaml::SIMachineFunctionInfo(
      *MFI, *MF.getSubtarget<GCNSubtarget>().getRegisterInfo(), MF);
}

bool GCNTargetMachine::parseMachineFunctionInfo(
    const yaml::MachineFunctionInfo &MFI_, PerFunctionMIParsingState &PFS,
    SMDiagnostic &Error, SMRange &SourceRange) const {
  const yaml::SIMachineFunctionInfo &YamlMFI =
      static_cast<const yaml::SIMachineFunctionInfo &>(MFI_);
  MachineFunction &MF = PFS.MF;
  SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();

  if (MFI->initializeBaseYamlFields(YamlMFI, MF, PFS, Error, SourceRange))
    return true;

  if (MFI->Occupancy == 0) {
    // Fixup the subtarget dependent default value.
    MFI->Occupancy = ST.getOccupancyWithWorkGroupSizes(MF).second;
  }

  auto parseRegister = [&](const yaml::StringValue &RegName, Register &RegVal) {
    Register TempReg;
    if (parseNamedRegisterReference(PFS, TempReg, RegName.Value, Error)) {
      SourceRange = RegName.SourceRange;
      return true;
    }
    RegVal = TempReg;

    return false;
  };

  auto parseOptionalRegister = [&](const yaml::StringValue &RegName,
                                   Register &RegVal) {
    return !RegName.Value.empty() && parseRegister(RegName, RegVal);
  };

  if (parseOptionalRegister(YamlMFI.VGPRForAGPRCopy, MFI->VGPRForAGPRCopy))
    return true;

  if (parseOptionalRegister(YamlMFI.SGPRForEXECCopy, MFI->SGPRForEXECCopy))
    return true;

  if (parseOptionalRegister(YamlMFI.LongBranchReservedReg,
                            MFI->LongBranchReservedReg))
    return true;

  auto diagnoseRegisterClass = [&](const yaml::StringValue &RegName) {
    // Create a diagnostic for a the register string literal.
    const MemoryBuffer &Buffer =
        *PFS.SM->getMemoryBuffer(PFS.SM->getMainFileID());
    Error = SMDiagnostic(*PFS.SM, SMLoc(), Buffer.getBufferIdentifier(), 1,
                         RegName.Value.size(), SourceMgr::DK_Error,
                         "incorrect register class for field", RegName.Value,
                         {}, {});
    SourceRange = RegName.SourceRange;
    return true;
  };

  if (parseRegister(YamlMFI.ScratchRSrcReg, MFI->ScratchRSrcReg) ||
      parseRegister(YamlMFI.FrameOffsetReg, MFI->FrameOffsetReg) ||
      parseRegister(YamlMFI.StackPtrOffsetReg, MFI->StackPtrOffsetReg))
    return true;

  if (MFI->ScratchRSrcReg != AMDGPU::PRIVATE_RSRC_REG &&
      !AMDGPU::SGPR_128RegClass.contains(MFI->ScratchRSrcReg)) {
    return diagnoseRegisterClass(YamlMFI.ScratchRSrcReg);
  }

  if (MFI->FrameOffsetReg != AMDGPU::FP_REG &&
      !AMDGPU::SGPR_32RegClass.contains(MFI->FrameOffsetReg)) {
    return diagnoseRegisterClass(YamlMFI.FrameOffsetReg);
  }

  if (MFI->StackPtrOffsetReg != AMDGPU::SP_REG &&
      !AMDGPU::SGPR_32RegClass.contains(MFI->StackPtrOffsetReg)) {
    return diagnoseRegisterClass(YamlMFI.StackPtrOffsetReg);
  }

  for (const auto &YamlReg : YamlMFI.WWMReservedRegs) {
    Register ParsedReg;
    if (parseRegister(YamlReg, ParsedReg))
      return true;

    MFI->reserveWWMRegister(ParsedReg);
  }

  for (const auto &[_, Info] : PFS.VRegInfosNamed) {
    MFI->setFlag(Info->VReg, Info->Flags);
  }
  for (const auto &[_, Info] : PFS.VRegInfos) {
    MFI->setFlag(Info->VReg, Info->Flags);
  }

  for (const auto &YamlRegStr : YamlMFI.SpillPhysVGPRS) {
    Register ParsedReg;
    if (parseRegister(YamlRegStr, ParsedReg))
      return true;
    MFI->SpillPhysVGPRs.push_back(ParsedReg);
  }

  auto parseAndCheckArgument = [&](const std::optional<yaml::SIArgument> &A,
                                   const TargetRegisterClass &RC,
                                   ArgDescriptor &Arg, unsigned UserSGPRs,
                                   unsigned SystemSGPRs) {
    // Skip parsing if it's not present.
    if (!A)
      return false;

    if (A->IsRegister) {
      Register Reg;
      if (parseNamedRegisterReference(PFS, Reg, A->RegisterName.Value, Error)) {
        SourceRange = A->RegisterName.SourceRange;
        return true;
      }
      if (!RC.contains(Reg))
        return diagnoseRegisterClass(A->RegisterName);
      Arg = ArgDescriptor::createRegister(Reg);
    } else
      Arg = ArgDescriptor::createStack(A->StackOffset);
    // Check and apply the optional mask.
    if (A->Mask)
      Arg = ArgDescriptor::createArg(Arg, *A->Mask);

    MFI->NumUserSGPRs += UserSGPRs;
    MFI->NumSystemSGPRs += SystemSGPRs;
    return false;
  };

  if (YamlMFI.ArgInfo &&
      (parseAndCheckArgument(YamlMFI.ArgInfo->PrivateSegmentBuffer,
                             AMDGPU::SGPR_128RegClass,
                             MFI->ArgInfo.PrivateSegmentBuffer, 4, 0) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->DispatchPtr,
                             AMDGPU::SReg_64RegClass, MFI->ArgInfo.DispatchPtr,
                             2, 0) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->QueuePtr, AMDGPU::SReg_64RegClass,
                             MFI->ArgInfo.QueuePtr, 2, 0) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->KernargSegmentPtr,
                             AMDGPU::SReg_64RegClass,
                             MFI->ArgInfo.KernargSegmentPtr, 2, 0) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->DispatchID,
                             AMDGPU::SReg_64RegClass, MFI->ArgInfo.DispatchID,
                             2, 0) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->FlatScratchInit,
                             AMDGPU::SReg_64RegClass,
                             MFI->ArgInfo.FlatScratchInit, 2, 0) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->PrivateSegmentSize,
                             AMDGPU::SGPR_32RegClass,
                             MFI->ArgInfo.PrivateSegmentSize, 0, 0) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->LDSKernelId,
                             AMDGPU::SGPR_32RegClass,
                             MFI->ArgInfo.LDSKernelId, 0, 1) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->WorkGroupIDX,
                             AMDGPU::SGPR_32RegClass, MFI->ArgInfo.WorkGroupIDX,
                             0, 1) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->WorkGroupIDY,
                             AMDGPU::SGPR_32RegClass, MFI->ArgInfo.WorkGroupIDY,
                             0, 1) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->WorkGroupIDZ,
                             AMDGPU::SGPR_32RegClass, MFI->ArgInfo.WorkGroupIDZ,
                             0, 1) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->WorkGroupInfo,
                             AMDGPU::SGPR_32RegClass,
                             MFI->ArgInfo.WorkGroupInfo, 0, 1) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->PrivateSegmentWaveByteOffset,
                             AMDGPU::SGPR_32RegClass,
                             MFI->ArgInfo.PrivateSegmentWaveByteOffset, 0, 1) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->ImplicitArgPtr,
                             AMDGPU::SReg_64RegClass,
                             MFI->ArgInfo.ImplicitArgPtr, 0, 0) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->ImplicitBufferPtr,
                             AMDGPU::SReg_64RegClass,
                             MFI->ArgInfo.ImplicitBufferPtr, 2, 0) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->WorkItemIDX,
                             AMDGPU::VGPR_32RegClass,
                             MFI->ArgInfo.WorkItemIDX, 0, 0) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->WorkItemIDY,
                             AMDGPU::VGPR_32RegClass,
                             MFI->ArgInfo.WorkItemIDY, 0, 0) ||
       parseAndCheckArgument(YamlMFI.ArgInfo->WorkItemIDZ,
                             AMDGPU::VGPR_32RegClass,
                             MFI->ArgInfo.WorkItemIDZ, 0, 0)))
    return true;

  // Parse FirstKernArgPreloadReg separately, since it's a Register,
  // not ArgDescriptor.
  if (YamlMFI.ArgInfo && YamlMFI.ArgInfo->FirstKernArgPreloadReg) {
    const yaml::SIArgument &A = *YamlMFI.ArgInfo->FirstKernArgPreloadReg;

    if (!A.IsRegister) {
      // For stack arguments, we don't have RegisterName.SourceRange,
      // but we should have some location info from the YAML parser
      const MemoryBuffer &Buffer =
          *PFS.SM->getMemoryBuffer(PFS.SM->getMainFileID());
      // Create a minimal valid source range
      SMLoc Loc = SMLoc::getFromPointer(Buffer.getBufferStart());
      SMRange Range(Loc, Loc);

      Error = SMDiagnostic(
          *PFS.SM, Loc, Buffer.getBufferIdentifier(), 1, 0, SourceMgr::DK_Error,
          "firstKernArgPreloadReg must be a register, not a stack location", "",
          {}, {});

      SourceRange = Range;
      return true;
    }

    Register Reg;
    if (parseNamedRegisterReference(PFS, Reg, A.RegisterName.Value, Error)) {
      SourceRange = A.RegisterName.SourceRange;
      return true;
    }

    if (!AMDGPU::SGPR_32RegClass.contains(Reg))
      return diagnoseRegisterClass(A.RegisterName);

    MFI->ArgInfo.FirstKernArgPreloadReg = Reg;
    MFI->NumUserSGPRs += YamlMFI.NumKernargPreloadSGPRs;
  }

  if (ST.hasFeature(AMDGPU::FeatureDX10ClampAndIEEEMode)) {
    MFI->Mode.IEEE = YamlMFI.Mode.IEEE;
    MFI->Mode.DX10Clamp = YamlMFI.Mode.DX10Clamp;
  }

  // FIXME: Move proper support for denormal-fp-math into base MachineFunction
  MFI->Mode.FP32Denormals.Input = YamlMFI.Mode.FP32InputDenormals
                                      ? DenormalMode::IEEE
                                      : DenormalMode::PreserveSign;
  MFI->Mode.FP32Denormals.Output = YamlMFI.Mode.FP32OutputDenormals
                                       ? DenormalMode::IEEE
                                       : DenormalMode::PreserveSign;

  MFI->Mode.FP64FP16Denormals.Input = YamlMFI.Mode.FP64FP16InputDenormals
                                          ? DenormalMode::IEEE
                                          : DenormalMode::PreserveSign;
  MFI->Mode.FP64FP16Denormals.Output = YamlMFI.Mode.FP64FP16OutputDenormals
                                           ? DenormalMode::IEEE
                                           : DenormalMode::PreserveSign;

  if (YamlMFI.HasInitWholeWave)
    MFI->setInitWholeWave();

  return false;
}

//===----------------------------------------------------------------------===//
// AMDGPU CodeGen Pass Builder interface.
//===----------------------------------------------------------------------===//

AMDGPUCodeGenPassBuilder::AMDGPUCodeGenPassBuilder(
    GCNTargetMachine &TM, const CGPassBuilderOption &Opts,
    PassInstrumentationCallbacks *PIC)
    : CodeGenPassBuilder(TM, Opts, PIC) {
  Opt.MISchedPostRA = true;
  Opt.RequiresCodeGenSCCOrder = true;
  // Exceptions and StackMaps are not supported, so these passes will never do
  // anything.
  // Garbage collection is not supported.
  disablePass<StackMapLivenessPass, FuncletLayoutPass, PatchableFunctionPass,
              ShadowStackGCLoweringPass, GCLoweringPass>();
}

void AMDGPUCodeGenPassBuilder::addIRPasses(PassManagerWrapper &PMW) {
  if (getRemoveIncompatibleFunctions(nullptr, TM.getOptionsContext()) &&
      TM.getTargetTriple().isAMDGCN()) {
    flushFPMsToMPM(PMW);
    addModulePass(AMDGPURemoveIncompatibleFunctionsPass(TM), PMW);
  }

  flushFPMsToMPM(PMW);

  if (TM.getTargetTriple().isAMDGCN())
    addModulePass(AMDGPUPrintfRuntimeBindingPass(), PMW);

  if (getLowerCtorDtor(nullptr, TM.getOptionsContext()))
    addModulePass(AMDGPUCtorDtorLoweringPass(), PMW);

  if (isPassEnabled(
          getEnableImageIntrinsicOptimizer(nullptr, TM.getOptionsContext()),
          getEnableImageIntrinsicOptimizerWasSpecified(nullptr,
                                                       TM.getOptionsContext())))
    addFunctionPass(AMDGPUImageIntrinsicOptimizerPass(TM), PMW);

  if (getEnableUniformIntrinsicCombine(nullptr, TM.getOptionsContext()))
    addFunctionPass(AMDGPUUniformIntrinsicCombinePass(), PMW);
  // This can be disabled by passing ::Disable here or on the command line
  // with --expand-variadics-override=disable.
  flushFPMsToMPM(PMW);
  addModulePass(ExpandVariadicsPass(ExpandVariadicsMode::Lowering), PMW);

  addModulePass(AMDGPUAlwaysInlinePass(), PMW);
  addModulePass(AlwaysInlinerPass(), PMW);

  addModulePass(AMDGPUExportKernelRuntimeHandlesPass(), PMW);

  if (getEnableLowerExecSync(nullptr, TM.getOptionsContext()))
    addModulePass(AMDGPULowerExecSyncPass(), PMW);

  if (getEnableSwLowerLDS(nullptr, TM.getOptionsContext()))
    addModulePass(AMDGPUSwLowerLDSPass(), PMW);

  // Runs before PromoteAlloca so the latter can account for function uses
  if (AMDGPUTargetMachine::getEnableLowerModuleLDS(TM.getOptionsContext()))
    addModulePass(AMDGPULowerModuleLDSPass(getTM()), PMW);

  // Run atomic optimizer before Atomic Expand
  if (TM.getOptLevel() >= CodeGenOptLevel::Less &&
      (getAMDGPUAtomicOptimizerStrategy(nullptr, TM.getOptionsContext()) !=
       ScanOptions::None))
    addFunctionPass(
        AMDGPUAtomicOptimizerPass(TM, getAMDGPUAtomicOptimizerStrategy(
                                          nullptr, TM.getOptionsContext())),
        PMW);

  addFunctionPass(AtomicExpandPass(TM), PMW);

  if (TM.getOptLevel() > CodeGenOptLevel::None) {
    addFunctionPass(AMDGPUPromoteAllocaPass(TM), PMW);
    if (isPassEnabled(getEnableScalarIRPasses(nullptr, TM.getOptionsContext()),
                      getEnableScalarIRPassesWasSpecified(
                          nullptr, TM.getOptionsContext())))
      addStraightLineScalarOptimizationPasses(PMW);

    // TODO: Handle EnableAMDGPUAliasAnalysis

    // TODO: May want to move later or split into an early and late one.
    addFunctionPass(AMDGPUCodeGenPreparePass(TM), PMW);

    // Try to hoist loop invariant parts of divisions AMDGPUCodeGenPrepare may
    // have expanded.
    if (TM.getOptLevel() > CodeGenOptLevel::Less) {
      addFunctionPass(createFunctionToLoopPassAdaptor(LICMPass(LICMOptions()),
                                                      /*UseMemorySSA=*/true),
                      PMW);
    }
  }

  Base::addIRPasses(PMW);

  // EarlyCSE is not always strong enough to clean up what LSR produces. For
  // example, GVN can combine
  //
  //   %0 = add %a, %b
  //   %1 = add %b, %a
  //
  // and
  //
  //   %0 = shl nsw %a, 2
  //   %1 = shl %a, 2
  //
  // but EarlyCSE can do neither of them.
  if (isPassEnabled(
          getEnableScalarIRPasses(nullptr, TM.getOptionsContext()),
          getEnableScalarIRPassesWasSpecified(nullptr, TM.getOptionsContext())))
    addEarlyCSEOrGVNPass(PMW);
}

void AMDGPUCodeGenPassBuilder::addCodeGenPrepare(PassManagerWrapper &PMW) {
  if (TM.getOptLevel() > CodeGenOptLevel::None) {
    flushFPMsToMPM(PMW);
    addModulePass(AMDGPUPreloadKernelArgumentsPass(TM), PMW);
  }

  if (getEnableLowerKernelArguments(nullptr, TM.getOptionsContext()))
    addFunctionPass(AMDGPULowerKernelArgumentsPass(TM), PMW);

  Base::addCodeGenPrepare(PMW);

  if (isPassEnabled(
          getEnableLoadStoreVectorizer(nullptr, TM.getOptionsContext()),
          getEnableLoadStoreVectorizerWasSpecified(nullptr,
                                                   TM.getOptionsContext())))
    addFunctionPass(LoadStoreVectorizerPass(), PMW);

  // This lowering has been placed after codegenprepare to take advantage of
  // address mode matching (which is why it isn't put with the LDS lowerings).
  // It could be placed anywhere before uniformity annotations (an analysis
  // that it changes by splitting up fat pointers into their components)
  // but has been put before switch lowering and CFG flattening so that those
  // passes can run on the more optimized control flow this pass creates in
  // many cases.
  flushFPMsToMPM(PMW);
  addModulePass(AMDGPULowerBufferFatPointersPass(TM), PMW);
  flushFPMsToMPM(PMW);
  requireCGSCCOrder(PMW);

  addModulePass(AMDGPULowerIntrinsicsPass(getTM()), PMW);

  // LowerSwitch pass may introduce unreachable blocks that can cause unexpected
  // behavior for subsequent passes. Placing it here seems better that these
  // blocks would get cleaned up by UnreachableBlockElim inserted next in the
  // pass flow.
  addFunctionPass(LowerSwitchPass(), PMW);
}

void AMDGPUCodeGenPassBuilder::addPreISel(PassManagerWrapper &PMW) {

  if (TM.getOptLevel() > CodeGenOptLevel::None) {
    addFunctionPass(FlattenCFGPass(), PMW);
    addFunctionPass(SinkingPass(), PMW);
    addFunctionPass(AMDGPULateCodeGenPreparePass(getTM()), PMW);
  }

  // Merge divergent exit nodes. StructurizeCFG won't recognize the multi-exit
  // regions formed by them.

  addFunctionPass(AMDGPUUnifyDivergentExitNodesPass(), PMW);
  addFunctionPass(FixIrreduciblePass(), PMW);
  addFunctionPass(UnifyLoopExitsPass(), PMW);
  addFunctionPass(StructurizeCFGPass(/*SkipUniformRegions=*/false), PMW);

  addFunctionPass(AMDGPUAnnotateUniformValuesPass(), PMW);

  addFunctionPass(SIAnnotateControlFlowPass(getTM()), PMW);

  // TODO: Move this right after structurizeCFG to avoid extra divergence
  // analysis. This depends on stopping SIAnnotateControlFlow from making
  // control flow modifications.
  addFunctionPass(AMDGPURewriteUndefForPHIPass(), PMW);

  if (!getCGPassBuilderOption(TM.getOptionsContext())
           .EnableGlobalISelOption.value_or(false) ||
      !isGlobalISelAbortEnabled())
    addFunctionPass(LCSSAPass(), PMW);

  if (TM.getOptLevel() > CodeGenOptLevel::Less) {
    flushFPMsToMPM(PMW);
    addModulePass(AMDGPUPerfHintAnalysisPass(getTM()), PMW);
  }
}

void AMDGPUCodeGenPassBuilder::addILPOpts(PassManagerWrapper &PMW) {
  if (getEnableEarlyIfConversion(nullptr, TM.getOptionsContext()))
    addMachineFunctionPass(EarlyIfConverterPass(), PMW);

  Base::addILPOpts(PMW);
}

void AMDGPUCodeGenPassBuilder::addAsmPrinterBegin(PassManagerWrapper &PMW) {
  addModulePass(AMDGPUAsmPrinterBeginPass(), PMW,
                /*Force=*/true);
}

void AMDGPUCodeGenPassBuilder::addAsmPrinter(PassManagerWrapper &PMW) {
  addMachineFunctionPass(AMDGPUAsmPrinterPass(), PMW);
}

void AMDGPUCodeGenPassBuilder::addAsmPrinterEnd(PassManagerWrapper &PMW) {
  addModulePass(AMDGPUAsmPrinterEndPass(), PMW);
}

Error AMDGPUCodeGenPassBuilder::addInstSelector(PassManagerWrapper &PMW) {
  addMachineFunctionPass(AMDGPUISelDAGToDAGPass(TM), PMW);
  addMachineFunctionPass(SIFixSGPRCopiesPass(), PMW);
  addMachineFunctionPass(SILowerI1CopiesPass(), PMW);
  return Error::success();
}

void AMDGPUCodeGenPassBuilder::addPreRewrite(PassManagerWrapper &PMW) {
  if (getEnableRegReassign(nullptr, TM.getOptionsContext())) {
    addMachineFunctionPass(GCNNSAReassignPass(), PMW);
  }

  addMachineFunctionPass(AMDGPURewriteAGPRCopyMFMAPass(), PMW);
}

void AMDGPUCodeGenPassBuilder::addMachineSSAOptimization(
    PassManagerWrapper &PMW) {
  Base::addMachineSSAOptimization(PMW);

  addMachineFunctionPass(SIFoldOperandsPass(), PMW);
  if (getEnableDPPCombine(nullptr, TM.getOptionsContext())) {
    addMachineFunctionPass(GCNDPPCombinePass(), PMW);
  }
  addMachineFunctionPass(SILoadStoreOptimizerPass(), PMW);
  if (isPassEnabled(
          getEnableSDWAPeephole(nullptr, TM.getOptionsContext()),
          getEnableSDWAPeepholeWasSpecified(nullptr, TM.getOptionsContext()))) {
    addMachineFunctionPass(SIPeepholeSDWAPass(), PMW);
    addMachineFunctionPass(EarlyMachineLICMPass(), PMW);
    addMachineFunctionPass(MachineCSEPass(), PMW);
    addMachineFunctionPass(SIFoldOperandsPass(), PMW);
  }
  addMachineFunctionPass(DeadMachineInstructionElimPass(), PMW);
  addMachineFunctionPass(SIShrinkInstructionsPass(), PMW);
}

Error AMDGPUCodeGenPassBuilder::addFastRegAlloc(PassManagerWrapper &PMW) {
  insertPass<PHIEliminationPass>(SILowerControlFlowPass());

  insertPass<TwoAddressInstructionPass>(SIWholeQuadModePass());

  return Base::addFastRegAlloc(PMW);
}

Error AMDGPUCodeGenPassBuilder::addRegAssignAndRewriteFast(
    PassManagerWrapper &PMW) {
  if (auto Err = validateRegAllocOptions())
    return Err;

  addMachineFunctionPass(GCNPreRALongBranchRegPass(), PMW);

  // SGPR allocation - default to fast at -O0.
  if (getSGPRRegAllocNPM(nullptr, TM.getOptionsContext()) ==
      RegAllocType::Greedy)
    addMachineFunctionPass(RAGreedyPass({onlyAllocateSGPRs, "sgpr"}), PMW);
  else
    addMachineFunctionPass(RegAllocFastPass({onlyAllocateSGPRs, "sgpr", false}),
                           PMW);

  // Equivalent of PEI for SGPRs.
  addMachineFunctionPass(SILowerSGPRSpillsPass(), PMW);

  // To Allocate wwm registers used in whole quad mode operations (for shaders).
  addMachineFunctionPass(SIPreAllocateWWMRegsPass(), PMW);

  // WWM allocation - default to fast at -O0.
  if (getWWMRegAllocNPM(nullptr, TM.getOptionsContext()) ==
      RegAllocType::Greedy)
    addMachineFunctionPass(RAGreedyPass({onlyAllocateWWMRegs, "wwm"}), PMW);
  else
    addMachineFunctionPass(
        RegAllocFastPass({onlyAllocateWWMRegs, "wwm", false}), PMW);

  addMachineFunctionPass(SILowerWWMCopiesPass(), PMW);
  addMachineFunctionPass(AMDGPUReserveWWMRegsPass(), PMW);

  // VGPR allocation - default to fast at -O0.
  if (getVGPRRegAllocNPM(nullptr, TM.getOptionsContext()) ==
      RegAllocType::Greedy)
    addMachineFunctionPass(RAGreedyPass({onlyAllocateVGPRs, "vgpr"}), PMW);
  else
    addMachineFunctionPass(RegAllocFastPass({onlyAllocateVGPRs, "vgpr"}), PMW);

  return Error::success();
}

Error AMDGPUCodeGenPassBuilder::addOptimizedRegAlloc(PassManagerWrapper &PMW) {
  if (getEnableDCEInRA(nullptr, TM.getOptionsContext()))
    insertPass<DetectDeadLanesPass>(DeadMachineInstructionElimPass());

  // FIXME: when an instruction has a Killed operand, and the instruction is
  // inside a bundle, seems only the BUNDLE instruction appears as the Kills of
  // the register in LiveVariables, this would trigger a failure in verifier,
  // we should fix it and enable the verifier.
  if (getOptVGPRLiveRange(nullptr, TM.getOptionsContext()))
    insertPass<RequireAnalysisPass<LiveVariablesAnalysis, MachineFunction>>(
        SIOptimizeVGPRLiveRangePass());

  // This must be run immediately after phi elimination and before
  // TwoAddressInstructions, otherwise the processing of the tied operand of
  // SI_ELSE will introduce a copy of the tied operand source after the else.
  insertPass<PHIEliminationPass>(SILowerControlFlowPass());

  if (getEnableRewritePartialRegUses(nullptr, TM.getOptionsContext()))
    insertPass<RenameIndependentSubregsPass>(GCNRewritePartialRegUsesPass());

  if (isPassEnabled(
          getEnablePreRAOptimizations(nullptr, TM.getOptionsContext()),
          getEnablePreRAOptimizationsWasSpecified(nullptr,
                                                  TM.getOptionsContext())))
    insertPass<MachineSchedulerPass>(GCNPreRAOptimizationsPass());

  // Allow the scheduler to run before SIWholeQuadMode inserts exec manipulation
  // instructions that cause scheduling barriers.
  insertPass<MachineSchedulerPass>(SIWholeQuadModePass());

  if (getOptExecMaskPreRA(nullptr, TM.getOptionsContext()))
    insertPass<MachineSchedulerPass>(SIOptimizeExecMaskingPreRAPass());

  // This is not an essential optimization and it has a noticeable impact on
  // compilation time, so we only enable it from O2.
  if (TM.getOptLevel() > CodeGenOptLevel::Less)
    insertPass<MachineSchedulerPass>(SIFormMemoryClausesPass());

  return Base::addOptimizedRegAlloc(PMW);
}

void AMDGPUCodeGenPassBuilder::addPreRegAlloc(PassManagerWrapper &PMW) {
  if (getOptLevel() != CodeGenOptLevel::None)
    addMachineFunctionPass(AMDGPUPrepareAGPRAllocPass(), PMW);
}

Expected<bool> AMDGPUCodeGenPassBuilder::addRegAssignAndRewriteOptimized(
    PassManagerWrapper &PMW) {
  if (auto Err = validateRegAllocOptions())
    return Err;

  addMachineFunctionPass(GCNPreRALongBranchRegPass(), PMW);

  // SGPR allocation - default to greedy at -O1 and above.
  if (getSGPRRegAllocNPM(nullptr, TM.getOptionsContext()) == RegAllocType::Fast)
    addMachineFunctionPass(RegAllocFastPass({onlyAllocateSGPRs, "sgpr", false}),
                           PMW);
  else
    addMachineFunctionPass(RAGreedyPass({onlyAllocateSGPRs, "sgpr"}), PMW);

  // Commit allocated register changes. This is mostly necessary because too
  // many things rely on the use lists of the physical registers, such as the
  // verifier. This is only necessary with allocators which use LiveIntervals,
  // since FastRegAlloc does the replacements itself.
  addMachineFunctionPass(VirtRegRewriterPass(false), PMW);

  // At this point, the sgpr-regalloc has been done and it is good to have the
  // stack slot coloring to try to optimize the SGPR spill stack indices before
  // attempting the custom SGPR spill lowering.
  addMachineFunctionPass(StackSlotColoringPass(), PMW);

  // Equivalent of PEI for SGPRs.
  addMachineFunctionPass(SILowerSGPRSpillsPass(), PMW);

  // To Allocate wwm registers used in whole quad mode operations (for shaders).
  addMachineFunctionPass(SIPreAllocateWWMRegsPass(), PMW);

  // WWM allocation - default to greedy at -O1 and above.
  if (getWWMRegAllocNPM(nullptr, TM.getOptionsContext()) == RegAllocType::Fast)
    addMachineFunctionPass(
        RegAllocFastPass({onlyAllocateWWMRegs, "wwm", false}), PMW);
  else
    addMachineFunctionPass(RAGreedyPass({onlyAllocateWWMRegs, "wwm"}), PMW);
  addMachineFunctionPass(SILowerWWMCopiesPass(), PMW);
  addMachineFunctionPass(VirtRegRewriterPass(false), PMW);
  addMachineFunctionPass(AMDGPUReserveWWMRegsPass(), PMW);

  // VGPR allocation - default to greedy at -O1 and above.
  if (getVGPRRegAllocNPM(nullptr, TM.getOptionsContext()) == RegAllocType::Fast)
    addMachineFunctionPass(RegAllocFastPass({onlyAllocateVGPRs, "vgpr"}), PMW);
  else
    addMachineFunctionPass(RAGreedyPass({onlyAllocateVGPRs, "vgpr"}), PMW);

  addPreRewrite(PMW);
  addMachineFunctionPass(VirtRegRewriterPass(true), PMW);

  addMachineFunctionPass(AMDGPUMarkLastScratchLoadPass(), PMW);
  return true;
}

void AMDGPUCodeGenPassBuilder::addPostRegAlloc(PassManagerWrapper &PMW) {
  addMachineFunctionPass(SIFixVGPRCopiesPass(), PMW);
  if (TM.getOptLevel() > CodeGenOptLevel::None)
    addMachineFunctionPass(SIOptimizeExecMaskingPass(), PMW);
  Base::addPostRegAlloc(PMW);
}

void AMDGPUCodeGenPassBuilder::addPreSched2(PassManagerWrapper &PMW) {
  if (TM.getOptLevel() > CodeGenOptLevel::None)
    addMachineFunctionPass(SIShrinkInstructionsPass(), PMW);
  addMachineFunctionPass(SIPostRABundlerPass(), PMW);
}

void AMDGPUCodeGenPassBuilder::addPostBBSections(PassManagerWrapper &PMW) {
  // We run this later to avoid passes like livedebugvalues and BBSections
  // having to deal with the apparent multi-entry functions we may generate.
  addMachineFunctionPass(AMDGPUPreloadKernArgPrologPass(), PMW);
}

void AMDGPUCodeGenPassBuilder::addPreEmitPass(PassManagerWrapper &PMW) {
  if (isPassEnabled(getEnableVOPD(nullptr, TM.getOptionsContext()),
                    getEnableVOPDWasSpecified(nullptr, TM.getOptionsContext()),
                    CodeGenOptLevel::Less)) {
    addMachineFunctionPass(GCNCreateVOPDPass(), PMW);
  }

  addMachineFunctionPass(SIMemoryLegalizerPass(), PMW);
  addMachineFunctionPass(SIInsertWaitcntsPass(), PMW);

  addMachineFunctionPass(SIModeRegisterPass(), PMW);

  if (TM.getOptLevel() > CodeGenOptLevel::None)
    addMachineFunctionPass(SIInsertHardClausesPass(), PMW);

  addMachineFunctionPass(SILateBranchLoweringPass(), PMW);

  if (isPassEnabled(
          getEnableSetWavePriority(nullptr, TM.getOptionsContext()),
          getEnableSetWavePriorityWasSpecified(nullptr, TM.getOptionsContext()),
          CodeGenOptLevel::Less))
    addMachineFunctionPass(AMDGPUSetWavePriorityPass(), PMW);

  if (TM.getOptLevel() > CodeGenOptLevel::None)
    addMachineFunctionPass(SIPreEmitPeepholePass(), PMW);

  // The hazard recognizer that runs as part of the post-ra scheduler does not
  // guarantee to be able handle all hazards correctly. This is because if there
  // are multiple scheduling regions in a basic block, the regions are scheduled
  // bottom up, so when we begin to schedule a region we don't know what
  // instructions were emitted directly before it.
  //
  // Here we add a stand-alone hazard recognizer pass which can handle all
  // cases.
  addMachineFunctionPass(PostRAHazardRecognizerPass(), PMW);
  addMachineFunctionPass(AMDGPUWaitSGPRHazardsPass(), PMW);
  addMachineFunctionPass(AMDGPULowerVGPREncodingPass(), PMW);

  if (isPassEnabled(
          getEnableInsertDelayAlu(nullptr, TM.getOptionsContext()),
          getEnableInsertDelayAluWasSpecified(nullptr, TM.getOptionsContext()),
          CodeGenOptLevel::Less)) {
    addMachineFunctionPass(AMDGPUInsertDelayAluPass(), PMW);
  }

  addMachineFunctionPass(BranchRelaxationPass(), PMW);
}

bool AMDGPUCodeGenPassBuilder::isPassEnabled(bool Opt, bool WasSpecified,
                                             CodeGenOptLevel Level) const {
  if (WasSpecified)
    return Opt;
  if (TM.getOptLevel() < Level)
    return false;
  return Opt;
}

void AMDGPUCodeGenPassBuilder::addEarlyCSEOrGVNPass(PassManagerWrapper &PMW) {
  if (TM.getOptLevel() == CodeGenOptLevel::Aggressive)
    addFunctionPass(GVNPass(), PMW);
  else
    addFunctionPass(EarlyCSEPass(), PMW);
}

void AMDGPUCodeGenPassBuilder::addStraightLineScalarOptimizationPasses(
    PassManagerWrapper &PMW) {
  if (isPassEnabled(
          getEnableLoopPrefetch(nullptr, TM.getOptionsContext()),
          getEnableLoopPrefetchWasSpecified(nullptr, TM.getOptionsContext()),
          CodeGenOptLevel::Aggressive))
    addFunctionPass(LoopDataPrefetchPass(), PMW);

  addFunctionPass(SeparateConstOffsetFromGEPPass(), PMW);

  // ReassociateGEPs exposes more opportunities for SLSR. See
  // the example in reassociate-geps-and-slsr.ll.
  addFunctionPass(StraightLineStrengthReducePass(), PMW);

  // SeparateConstOffsetFromGEP and SLSR creates common expressions which GVN or
  // EarlyCSE can reuse.
  addEarlyCSEOrGVNPass(PMW);

  // Run NaryReassociate after EarlyCSE/GVN to be more effective.
  addFunctionPass(NaryReassociatePass(), PMW);

  // NaryReassociate on GEPs creates redundant common expressions, so run
  // EarlyCSE after it.
  addFunctionPass(EarlyCSEPass(), PMW);
}
