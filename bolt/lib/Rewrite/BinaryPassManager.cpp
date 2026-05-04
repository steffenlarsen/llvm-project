//===- bolt/Rewrite/BinaryPassManager.cpp - Binary-level pass manager -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "bolt/Rewrite/BinaryPassManager.h"
#include "bolt/Core/BoltCoreOptionsOptInfos.h"
#include "bolt/Passes/AArch64RelaxationPass.h"
#include "bolt/Passes/Aligner.h"
#include "bolt/Passes/AllocCombiner.h"
#include "bolt/Passes/AsmDump.h"
#include "bolt/Passes/BoltPassesOptionsOptInfos.h"
#include "bolt/Passes/CMOVConversion.h"
#include "bolt/Passes/FixRISCVCallsPass.h"
#include "bolt/Passes/FixRelaxationPass.h"
#include "bolt/Passes/FrameOptimizer.h"
#include "bolt/Passes/Hugify.h"
#include "bolt/Passes/IdenticalCodeFolding.h"
#include "bolt/Passes/IndirectCallPromotion.h"
#include "bolt/Passes/Inliner.h"
#include "bolt/Passes/Instrumentation.h"
#include "bolt/Passes/JTFootprintReduction.h"
#include "bolt/Passes/LongJmp.h"
#include "bolt/Passes/LoopInversionPass.h"
#include "bolt/Passes/MCF.h"
#include "bolt/Passes/PLTCall.h"
#include "bolt/Passes/PatchEntries.h"
#include "bolt/Passes/PointerAuthCFIAnalyzer.h"
#include "bolt/Passes/PointerAuthCFIFixup.h"
#include "bolt/Passes/ProfileQualityStats.h"
#include "bolt/Passes/RegReAssign.h"
#include "bolt/Passes/ReorderData.h"
#include "bolt/Passes/ReorderFunctions.h"
#include "bolt/Passes/RetpolineInsertion.h"
#include "bolt/Passes/SplitFunctions.h"
#include "bolt/Passes/StokeInfo.h"
#include "bolt/Passes/TailDuplication.h"
#include "bolt/Passes/ThreeWayBranch.h"
#include "bolt/Passes/ValidateInternalCalls.h"
#include "bolt/Passes/ValidateMemRefs.h"
#include "bolt/Passes/VeneerElimination.h"
#include "bolt/Rewrite/BoltRewriteOptionsOptInfos.h"
#include "bolt/RuntimeLibs/BoltRuntimeLibsOptionsOptInfos.h"
#include "bolt/Utils/BoltUtilsOptionsOptInfos.h"
#include "bolt/Utils/CommandLineOpts.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <numeric>

using namespace llvm;

namespace opts {

extern bool PrintAll;
extern bool DumpDotAll;
extern std::string AsmDump;

bool KeepNopsSpecified = false;
bool NeverPrint = false;
bool PrintAfterBranchFixup = false;
bool PrintAArch64Relaxation = false;
bool PrintPAuthCFIAnalyzer = false;
bool PrintNormalized = false;
bool PrintProfileStats = false;
bool PrintReordered = false;
bool RegReAssign = false;
bool SimplifyConditionalTailCalls = true;
bool SimplifyRODataLoads = false;
std::vector<std::string> SpecializeMemcpy1;
bool StripRepRet = true;
bool UpdateBranchProtection = true;

} // namespace opts

namespace llvm {
namespace bolt {

using namespace opts;

const char BinaryFunctionPassManager::TimerGroupName[] = "passman";
const char BinaryFunctionPassManager::TimerGroupDesc[] =
    "Binary Function Pass Manager";

Error BinaryFunctionPassManager::runPasses() {
  const bool DoDynoStatsAll = bolt::bolt_rewrite_opts::getDynoStatsAll(BC);
  const bool DoVerifyCFG = bolt::bolt_rewrite_opts::getVerifyCfg(BC);

  auto &BFs = BC.getBinaryFunctions();
  for (size_t PassIdx = 0; PassIdx < Passes.size(); PassIdx++) {
    const std::pair<const bool, std::unique_ptr<BinaryFunctionPass>>
        &OptPassPair = Passes[PassIdx];
    if (!OptPassPair.first)
      continue;

    const std::unique_ptr<BinaryFunctionPass> &Pass = OptPassPair.second;
    std::string PassIdName =
        formatv("{0:2}_{1}", PassIdx, Pass->getName()).str();

    if (opts::getVerbosity(BC) > 0)
      BC.outs() << "BOLT-INFO: Starting pass: " << Pass->getName() << "\n";

    bool DoTimeOpts = false;
    if (auto *UO =
            bolt::bolt_utils_opts::getBoltUtilsOpts(BC.getOptionsContext()))
      DoTimeOpts = UO->get<&clv2::BOLT_TimeOpts>();
    NamedRegionTimer T(Pass->getName(), Pass->getName(), TimerGroupName,
                       TimerGroupDesc, DoTimeOpts);

    Error E = Error::success();
    callWithDynoStats(
        BC.outs(),
        [this, &E, &Pass] {
          E = joinErrors(std::move(E), Pass->runOnFunctions(BC));
        },
        BFs, Pass->getName(), DoDynoStatsAll, BC.isAArch64(),
        BC.getOptionsContext());
    if (E)
      return Error(std::move(E));

    if (DoVerifyCFG &&
        !std::accumulate(
            BFs.begin(), BFs.end(), true,
            [](const bool Valid,
               const std::pair<const uint64_t, BinaryFunction> &It) {
              return Valid && It.second.validateCFG();
            })) {
      return createFatalBOLTError(
          Twine("BOLT-ERROR: Invalid CFG detected after pass ") +
          Twine(Pass->getName()) + Twine("\n"));
    }

    if (opts::getVerbosity(BC) > 0)
      BC.outs() << "BOLT-INFO: Finished pass: " << Pass->getName() << "\n";

    {
      bool DoPrintAll = bolt_rewrite_opts::getPrintAll(BC);
      bool DoDumpDotAll = bolt_rewrite_opts::getDumpDotAll(BC);
      if (!DoPrintAll && !DoDumpDotAll && !Pass->printPass())
        continue;
    }

    const std::string Message = std::string("after ") + Pass->getName();

    for (auto &It : BFs) {
      BinaryFunction &Function = It.second;

      if (!Pass->shouldPrint(Function))
        continue;

      Function.print(BC.outs(), Message);

      if (shouldDumpDot(Function))
        Function.dumpGraphForPass(PassIdName);
    }
  }
  return Error::success();
}

Error BinaryFunctionPassManager::runAllPasses(BinaryContext &BC) {
  BinaryFunctionPassManager Manager(BC);

  // Read all BoltRewrite options from context into local variables.
  const bool DoDynoStatsAll = bolt::bolt_rewrite_opts::getDynoStatsAll(BC);
  const bool DoEliminateUnreachable =
      bolt::bolt_rewrite_opts::getEliminateUnreachable(BC);
  const bool DoJTFootprintReduction =
      bolt::bolt_rewrite_opts::getJtFootprintReduction(BC);
  const bool DoKeepNops = bolt::bolt_rewrite_opts::getKeepNops(BC);
  const bool DoNeverPrint = bolt::bolt_rewrite_opts::getNeverPrint(BC);
  const bool DoPrintAfterBranchFixup =
      bolt::bolt_rewrite_opts::getPrintAfterBranchFixup(BC);
  const bool DoPrintAfterLowering =
      bolt::bolt_rewrite_opts::getPrintAfterLowering(BC);
  const bool DoPrintEstimateEdgeCounts =
      bolt::bolt_rewrite_opts::getPrintEstimateEdgeCounts(BC);
  const bool DoPrintFinalized = bolt::bolt_rewrite_opts::getPrintFinalized(BC);
  const bool DoPrintFOP = bolt::bolt_rewrite_opts::getPrintFop(BC);
  const bool DoPrintICF = bolt::bolt_rewrite_opts::getPrintIcf(BC);
  const bool DoPrintICP = bolt::bolt_rewrite_opts::getPrintIcp(BC);
  const bool DoPrintInline = bolt::bolt_rewrite_opts::getPrintInline(BC);
  const bool DoPrintJTFootprintReduction =
      bolt::bolt_rewrite_opts::getPrintAfterJtFootprintReduction(BC);
  const bool DoPrintAArch64Relaxation =
      bolt::bolt_rewrite_opts::getPrintAdrLdrRelaxation(BC);
  const bool DoPrintPAuthCFIAnalyzer =
      bolt::bolt_rewrite_opts::getPrintPointerAuthCfiAnalyzer(BC);
  const bool DoPrintPAuthCFIFixup =
      bolt::bolt_rewrite_opts::getPrintPointerAuthCfiFixup(BC);
  const bool DoPrintLongJmp = bolt::bolt_rewrite_opts::getPrintLongjmp(BC);
  const bool DoPrintNormalized =
      bolt::bolt_rewrite_opts::getPrintNormalized(BC);
  const bool DoPrintPeepholes = bolt::bolt_rewrite_opts::getPrintPeepholes(BC);
  const bool DoPrintPLT = bolt::bolt_rewrite_opts::getPrintPlt(BC);
  const bool DoPrintProfileStats =
      bolt::bolt_rewrite_opts::getPrintProfileStats(BC);
  const bool DoPrintRegReAssign =
      bolt::bolt_rewrite_opts::getPrintRegreassign(BC);
  const bool DoPrintReordered = bolt::bolt_rewrite_opts::getPrintReordered(BC);
  const bool DoPrintReorderedFunctions =
      bolt::bolt_rewrite_opts::getPrintReorderedFunctions(BC);
  const bool DoPrintRetpolineInsertion =
      bolt::bolt_rewrite_opts::getPrintRetpolineInsertion(BC);
  const bool DoPrintSCTC = bolt::bolt_rewrite_opts::getPrintSctc(BC);
  const bool DoPrintSimplifyROLoads =
      bolt::bolt_rewrite_opts::getPrintSimplifyRodataLoads(BC);
  const bool DoPrintSplit = bolt::bolt_rewrite_opts::getPrintSplit(BC);
  const bool DoPrintStoke = bolt::bolt_rewrite_opts::getPrintStoke(BC);
  const bool DoPrintFixRelaxations =
      bolt::bolt_rewrite_opts::getPrintFixRelaxations(BC);
  const bool DoPrintFixRISCVCalls =
      bolt::bolt_rewrite_opts::getPrintFixRiscvCalls(BC);
  const bool DoPrintVeneerElimination =
      bolt::bolt_rewrite_opts::getPrintVeneerElimination(BC);
  const bool DoPrintUCE = bolt::bolt_rewrite_opts::getPrintUce(BC);
  const bool DoRegReAssign = bolt::bolt_rewrite_opts::getRegReassign(BC);
  const bool DoSimplifyConditionalTailCalls =
      bolt::bolt_rewrite_opts::getSimplifyConditionalTailCalls(BC);
  const bool DoSimplifyRODataLoads =
      bolt::bolt_rewrite_opts::getSimplifyRodataLoads(BC);
  const auto DoSpecializeMemcpy1 = bolt::bolt_rewrite_opts::getMemcpy1Spec(BC);
  const bool DoStoke = bolt::bolt_rewrite_opts::getStoke(BC);
  const bool DoStringOps = bolt::bolt_rewrite_opts::getInlineMemcpy(BC);
  const bool DoStripRepRet = bolt::bolt_rewrite_opts::getStripRepRet(BC);
  const bool DoThreeWayBranch = bolt::bolt_rewrite_opts::getThreeWayBranch(BC);
  const bool DoCMOVConversion = bolt::bolt_rewrite_opts::getCmovConversion(BC);
  const bool DoShortenInstructions =
      bolt::bolt_rewrite_opts::getShortenInstructions(BC);

  auto *PassOpts =
      bolt::bolt_passes_opts::getBoltPassesOpts(BC.getOptionsContext());
  const bool AsmDumpSpecified = PassOpts->specified<&clv2::BOLTPASS_AsmDump>();
  const auto ICF = static_cast<bolt::IdenticalCodeFolding::ICFLevel>(
      PassOpts->get<&clv2::BOLTPASS_ICF>());

  if (BC.isAArch64())
    Manager.registerPass(
        std::make_unique<PointerAuthCFIAnalyzer>(DoPrintPAuthCFIAnalyzer));

  Manager.registerPass(
      std::make_unique<EstimateEdgeCounts>(DoPrintEstimateEdgeCounts));

  Manager.registerPass(std::make_unique<DynoStatsSetPass>());

  Manager.registerPass(std::make_unique<AsmDumpPass>(), AsmDumpSpecified);

  if (BC.isAArch64()) {
    Manager.registerPass(
        std::make_unique<FixRelaxations>(DoPrintFixRelaxations));

    Manager.registerPass(
        std::make_unique<VeneerElimination>(DoPrintVeneerElimination));
  }

  if (BC.isRISCV()) {
    Manager.registerPass(
        std::make_unique<FixRISCVCallsPass>(DoPrintFixRISCVCalls));
  }

  // Here we manage dependencies/order manually, since passes are run in the
  // order they're registered.

  // Run this pass first to use stats for the original functions.
  Manager.registerPass(std::make_unique<PrintProgramStats>());

  if (DoPrintProfileStats)
    Manager.registerPass(std::make_unique<PrintProfileStats>(DoNeverPrint));

  Manager.registerPass(
      std::make_unique<PrintProfileQualityStats>(DoNeverPrint));

  Manager.registerPass(std::make_unique<ValidateInternalCalls>(DoNeverPrint));

  Manager.registerPass(std::make_unique<ValidateMemRefs>(DoNeverPrint));

  bool Instrument = bolt::bolt_utils_opts::getInstrument(BC);
  bool Hugify = bolt::bolt_rtlibs_opts::getHugify(BC);
  if (Instrument)
    Manager.registerPass(std::make_unique<Instrumentation>(DoNeverPrint));
  else if (Hugify)
    Manager.registerPass(std::make_unique<HugePage>(DoNeverPrint));

  Manager.registerPass(std::make_unique<ShortenInstructions>(DoNeverPrint),
                       DoShortenInstructions);

  Manager.registerPass(std::make_unique<RemoveNops>(DoNeverPrint), !DoKeepNops);

  Manager.registerPass(std::make_unique<NormalizeCFG>(DoPrintNormalized));

  if (BC.isX86())
    Manager.registerPass(std::make_unique<StripRepRet>(DoNeverPrint),
                         DoStripRepRet);

  Manager.registerPass(std::make_unique<IdenticalCodeFolding>(DoPrintICF),
                       ICF != bolt::IdenticalCodeFolding::ICFLevel::None);

  Manager.registerPass(
      std::make_unique<SpecializeMemcpy1>(DoNeverPrint, DoSpecializeMemcpy1),
      !DoSpecializeMemcpy1.empty());

  Manager.registerPass(std::make_unique<InlineMemcpy>(DoNeverPrint),
                       DoStringOps);

  Manager.registerPass(std::make_unique<IndirectCallPromotion>(DoPrintICP));

  Manager.registerPass(
      std::make_unique<JTFootprintReduction>(DoPrintJTFootprintReduction),
      DoJTFootprintReduction);

  Manager.registerPass(
      std::make_unique<SimplifyRODataLoads>(DoPrintSimplifyROLoads),
      DoSimplifyRODataLoads);

  Manager.registerPass(std::make_unique<RegReAssign>(DoPrintRegReAssign),
                       DoRegReAssign);

  Manager.registerPass(std::make_unique<Inliner>(DoPrintInline));

  Manager.registerPass(std::make_unique<IdenticalCodeFolding>(DoPrintICF),
                       ICF != bolt::IdenticalCodeFolding::ICFLevel::None);

  Manager.registerPass(std::make_unique<PLTCall>(DoPrintPLT));

  Manager.registerPass(std::make_unique<ThreeWayBranch>(), DoThreeWayBranch);

  Manager.registerPass(std::make_unique<ReorderBasicBlocks>(DoPrintReordered));

  Manager.registerPass(std::make_unique<EliminateUnreachableBlocks>(DoPrintUCE),
                       DoEliminateUnreachable);

  Manager.registerPass(std::make_unique<SplitFunctions>(DoPrintSplit));

  Manager.registerPass(std::make_unique<LoopInversionPass>());

  Manager.registerPass(std::make_unique<TailDuplication>());

  Manager.registerPass(std::make_unique<CMOVConversion>(), DoCMOVConversion);

  // This pass syncs local branches with CFG. If any of the following
  // passes breaks the sync - they either need to re-run the pass or
  // fix branches consistency internally.
  Manager.registerPass(
      std::make_unique<FixupBranches>(DoPrintAfterBranchFixup));

  // This pass should come close to last since it uses the estimated hot
  // size of a function to determine the order.  It should definitely
  // also happen after any changes to the call graph are made, e.g. inlining.
  Manager.registerPass(
      std::make_unique<ReorderFunctions>(DoPrintReorderedFunctions));

  // Produce the list of functions for the output file in a sorted order.
  Manager.registerPass(std::make_unique<PopulateOutputFunctions>());

  // This is the second run of the SplitFunctions pass required by certain
  // splitting strategies (e.g. cdsplit). Running the SplitFunctions pass again
  // after ReorderFunctions allows the finalized function order to be utilized
  // to make more sophisticated splitting decisions, like hot-warm-cold
  // splitting.
  Manager.registerPass(std::make_unique<SplitFunctions>(DoPrintSplit));

  // Print final dyno stats right while CFG and instruction analysis are intact.
  Manager.registerPass(
      std::make_unique<DynoStatsPrintPass>(
          "after all optimizations before SCTC and FOP"),
      [&] { return bolt::bolt_core_opts::getDynoStats(BC); }() ||
          DoDynoStatsAll);

  // Add the StokeInfo pass, which extract functions for stoke optimization and
  // get the liveness information for them
  Manager.registerPass(std::make_unique<StokeInfo>(DoPrintStoke), DoStoke);

  // This pass introduces conditional jumps into external functions.
  // Between extending CFG to support this and isolating this pass we chose
  // the latter. Thus this pass will do double jump removal and unreachable
  // code elimination if necessary and won't rely on peepholes/UCE for these
  // optimizations.
  // More generally this pass should be the last optimization pass that
  // modifies branches/control flow.  This pass is run after function
  // reordering so that it can tell whether calls are forward/backward
  // accurately.
  Manager.registerPass(
      std::make_unique<SimplifyConditionalTailCalls>(DoPrintSCTC),
      DoSimplifyConditionalTailCalls);

  Manager.registerPass(std::make_unique<Peepholes>(DoPrintPeepholes));

  // Assign each function an output section before AlignerPass and LongJmpPass,
  // so those passes can attribute per-section code alignment and tentative
  // layout to the final .text / .text.cold sections.
  Manager.registerPass(std::make_unique<AssignSections>());

  Manager.registerPass(std::make_unique<AlignerPass>());

  // Perform reordering on data contained in one or more sections using
  // memory profiling data.
  Manager.registerPass(std::make_unique<ReorderData>());

  // Patch original function entries
  if (BC.HasRelocations)
    Manager.registerPass(std::make_unique<PatchEntries>());

  // Assign each function an output section.
  Manager.registerPass(std::make_unique<AssignSections>());

  if (BC.isAArch64()) {
    Manager.registerPass(
        std::make_unique<AArch64RelaxationPass>(DoPrintAArch64Relaxation));

    // Tighten branches according to offset differences between branch and
    // targets. No extra instructions after this pass, otherwise we may have
    // relocations out of range and crash during linking.
    Manager.registerPass(std::make_unique<LongJmpPass>(DoPrintLongJmp));

    Manager.registerPass(
        std::make_unique<PointerAuthCFIFixup>(DoPrintPAuthCFIFixup));
  }

  // This pass should always run last.*
  Manager.registerPass(std::make_unique<FinalizeFunctions>(DoPrintFinalized));

  // FrameOptimizer has an implicit dependency on FinalizeFunctions.
  // FrameOptimizer move values around and needs to update CFIs. To do this, it
  // must read CFI, interpret it and rewrite it, so CFIs need to be correctly
  // placed according to the final layout.
  Manager.registerPass(std::make_unique<FrameOptimizerPass>(DoPrintFOP));

  Manager.registerPass(std::make_unique<AllocCombinerPass>(DoPrintFOP));

  Manager.registerPass(
      std::make_unique<RetpolineInsertion>(DoPrintRetpolineInsertion));

  // This pass turns tail calls into jumps which makes them invisible to
  // function reordering. It's unsafe to use any CFG or instruction analysis
  // after this point.
  Manager.registerPass(
      std::make_unique<InstructionLowering>(DoPrintAfterLowering));

  // In non-relocation mode, mark functions that do not fit into their original
  // space as non-simple if we have to (e.g. for correct debug info update).
  // NOTE: this pass depends on finalized code.
  if (!BC.HasRelocations)
    Manager.registerPass(std::make_unique<CheckLargeFunctions>(DoNeverPrint));

  Manager.registerPass(std::make_unique<LowerAnnotations>(DoNeverPrint));

  // Check for dirty state of MCSymbols caused by running calculateEmittedSize
  // in parallel and restore them
  Manager.registerPass(std::make_unique<CleanMCState>(DoNeverPrint));

  return Manager.runPasses();
}

} // namespace bolt
} // namespace llvm
