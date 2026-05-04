//===- NewPMDriver.cpp - Driver for opt with new PM -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file is just a split of the code that logically belongs in opt.cpp but
/// that includes the new pass manager headers.
///
//===----------------------------------------------------------------------===//

#include "NewPMDriver.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/RuntimeLibcallInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Bitcode/BitcodeWriterPass.h"
#include "llvm/CodeGen/LibcallLoweringInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRPrinter/IRPrintingPasses.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassesOptionsOptInfos.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/IPO/ThinLTOBitcodeWriter.h"
#include "llvm/Transforms/Instrumentation/AddressSanitizer.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Utils/AssignGUID.h"
#include "llvm/Transforms/Utils/Debugify.h"
#include "llvm/Transforms/Utils/ProfileVerify.h"

using namespace llvm;
using namespace opt_tool;

template <typename PassManagerT>
bool tryParsePipelineText(PassBuilder &PB, StringRef OptName,
                          StringRef Pipeline) {
  if (Pipeline.empty())
    return false;

  // Verify the pipeline is parseable:
  PassManagerT PM;
  if (auto Err = PB.parsePassPipeline(PM, Pipeline)) {
    errs() << "Could not parse -" << OptName
           << " pipeline: " << toString(std::move(Err))
           << "... I'm going to ignore it.\n";
    return false;
  }
  return true;
}

/// If one of the EPPipeline options was given, register callbacks for parsing
/// and inserting the given pipeline.
static void registerEPCallbacks(PassBuilder &PB, const NPMOptions &NPMOpts) {
  if (tryParsePipelineText<FunctionPassManager>(PB, "passes-ep-peephole",
                                                NPMOpts.PeepholeEPPipeline)) {
    std::string Pipeline = NPMOpts.PeepholeEPPipeline;
    PB.registerPeepholeEPCallback(
        [&PB, Pipeline](FunctionPassManager &PM, OptimizationLevel) {
          ExitOnError Err("Unable to parse PeepholeEP pipeline: ");
          Err(PB.parsePassPipeline(PM, Pipeline));
        });
  }
  if (tryParsePipelineText<LoopPassManager>(
          PB, "passes-ep-late-loop-optimizations",
          NPMOpts.LateLoopOptimizationsEPPipeline)) {
    std::string Pipeline = NPMOpts.LateLoopOptimizationsEPPipeline;
    PB.registerLateLoopOptimizationsEPCallback(
        [&PB, Pipeline](LoopPassManager &PM, OptimizationLevel) {
          ExitOnError Err("Unable to parse LateLoopOptimizationsEP pipeline: ");
          Err(PB.parsePassPipeline(PM, Pipeline));
        });
  }
  if (tryParsePipelineText<LoopPassManager>(
          PB, "passes-ep-loop-optimizer-end",
          NPMOpts.LoopOptimizerEndEPPipeline)) {
    std::string Pipeline = NPMOpts.LoopOptimizerEndEPPipeline;
    PB.registerLoopOptimizerEndEPCallback(
        [&PB, Pipeline](LoopPassManager &PM, OptimizationLevel) {
          ExitOnError Err("Unable to parse LoopOptimizerEndEP pipeline: ");
          Err(PB.parsePassPipeline(PM, Pipeline));
        });
  }
  if (tryParsePipelineText<FunctionPassManager>(
          PB, "passes-ep-scalar-optimizer-late",
          NPMOpts.ScalarOptimizerLateEPPipeline)) {
    std::string Pipeline = NPMOpts.ScalarOptimizerLateEPPipeline;
    PB.registerScalarOptimizerLateEPCallback(
        [&PB, Pipeline](FunctionPassManager &PM, OptimizationLevel) {
          ExitOnError Err("Unable to parse ScalarOptimizerLateEP pipeline: ");
          Err(PB.parsePassPipeline(PM, Pipeline));
        });
  }
  if (tryParsePipelineText<CGSCCPassManager>(
          PB, "passes-ep-cgscc-optimizer-late",
          NPMOpts.CGSCCOptimizerLateEPPipeline)) {
    std::string Pipeline = NPMOpts.CGSCCOptimizerLateEPPipeline;
    PB.registerCGSCCOptimizerLateEPCallback(
        [&PB, Pipeline](CGSCCPassManager &PM, OptimizationLevel) {
          ExitOnError Err("Unable to parse CGSCCOptimizerLateEP pipeline: ");
          Err(PB.parsePassPipeline(PM, Pipeline));
        });
  }
  if (tryParsePipelineText<FunctionPassManager>(
          PB, "passes-ep-vectorizer-start",
          NPMOpts.VectorizerStartEPPipeline)) {
    std::string Pipeline = NPMOpts.VectorizerStartEPPipeline;
    PB.registerVectorizerStartEPCallback(
        [&PB, Pipeline](FunctionPassManager &PM, OptimizationLevel) {
          ExitOnError Err("Unable to parse VectorizerStartEP pipeline: ");
          Err(PB.parsePassPipeline(PM, Pipeline));
        });
  }
  if (tryParsePipelineText<FunctionPassManager>(
          PB, "passes-ep-vectorizer-end", NPMOpts.VectorizerEndEPPipeline)) {
    std::string Pipeline = NPMOpts.VectorizerEndEPPipeline;
    PB.registerVectorizerEndEPCallback(
        [&PB, Pipeline](FunctionPassManager &PM, OptimizationLevel) {
          ExitOnError Err("Unable to parse VectorizerEndEP pipeline: ");
          Err(PB.parsePassPipeline(PM, Pipeline));
        });
  }
  if (tryParsePipelineText<ModulePassManager>(
          PB, "passes-ep-pipeline-start", NPMOpts.PipelineStartEPPipeline)) {
    std::string Pipeline = NPMOpts.PipelineStartEPPipeline;
    PB.registerPipelineStartEPCallback(
        [&PB, Pipeline](ModulePassManager &PM, OptimizationLevel) {
          ExitOnError Err("Unable to parse PipelineStartEP pipeline: ");
          Err(PB.parsePassPipeline(PM, Pipeline));
        });
  }
  if (tryParsePipelineText<ModulePassManager>(
          PB, "passes-ep-pipeline-early-simplification",
          NPMOpts.PipelineEarlySimplificationEPPipeline)) {
    std::string Pipeline = NPMOpts.PipelineEarlySimplificationEPPipeline;
    PB.registerPipelineEarlySimplificationEPCallback(
        [&PB, Pipeline](ModulePassManager &PM, OptimizationLevel,
                        ThinOrFullLTOPhase) {
          ExitOnError Err("Unable to parse EarlySimplification pipeline: ");
          Err(PB.parsePassPipeline(PM, Pipeline));
        });
  }
  if (tryParsePipelineText<ModulePassManager>(
          PB, "passes-ep-optimizer-early", NPMOpts.OptimizerEarlyEPPipeline)) {
    std::string Pipeline = NPMOpts.OptimizerEarlyEPPipeline;
    PB.registerOptimizerEarlyEPCallback([&PB, Pipeline](ModulePassManager &PM,
                                                        OptimizationLevel,
                                                        ThinOrFullLTOPhase) {
      ExitOnError Err("Unable to parse OptimizerEarlyEP pipeline: ");
      Err(PB.parsePassPipeline(PM, Pipeline));
    });
  }
  if (tryParsePipelineText<ModulePassManager>(
          PB, "passes-ep-optimizer-last", NPMOpts.OptimizerLastEPPipeline)) {
    std::string Pipeline = NPMOpts.OptimizerLastEPPipeline;
    PB.registerOptimizerLastEPCallback([&PB, Pipeline](ModulePassManager &PM,
                                                       OptimizationLevel,
                                                       ThinOrFullLTOPhase) {
      ExitOnError Err("Unable to parse OptimizerLastEP pipeline: ");
      Err(PB.parsePassPipeline(PM, Pipeline));
    });
  }
  if (tryParsePipelineText<ModulePassManager>(
          PB, "passes-ep-full-link-time-optimization-early",
          NPMOpts.FullLinkTimeOptimizationEarlyEPPipeline)) {
    std::string Pipeline = NPMOpts.FullLinkTimeOptimizationEarlyEPPipeline;
    PB.registerFullLinkTimeOptimizationEarlyEPCallback(
        [&PB, Pipeline](ModulePassManager &PM, OptimizationLevel) {
          ExitOnError Err(
              "Unable to parse FullLinkTimeOptimizationEarlyEP pipeline: ");
          Err(PB.parsePassPipeline(PM, Pipeline));
        });
  }
  if (tryParsePipelineText<ModulePassManager>(
          PB, "passes-ep-full-link-time-optimization-last",
          NPMOpts.FullLinkTimeOptimizationLastEPPipeline)) {
    std::string Pipeline = NPMOpts.FullLinkTimeOptimizationLastEPPipeline;
    PB.registerFullLinkTimeOptimizationLastEPCallback(
        [&PB, Pipeline](ModulePassManager &PM, OptimizationLevel) {
          ExitOnError Err(
              "Unable to parse FullLinkTimeOptimizationLastEP pipeline: ");
          Err(PB.parsePassPipeline(PM, Pipeline));
        });
  }
}

#define HANDLE_EXTENSION(Ext)                                                  \
  llvm::PassPluginLibraryInfo get##Ext##PluginInfo();
#include "llvm/Support/Extension.def"
#undef HANDLE_EXTENSION

bool llvm::runPassPipeline(
    StringRef Arg0, Module &M, TargetMachine *TM, TargetLibraryInfoImpl *TLII,
    ToolOutputFile *Out, ToolOutputFile *ThinLTOLinkOut,
    ToolOutputFile *OptRemarkFile, StringRef PassPipeline,
    ArrayRef<PassPlugin> PassPlugins,
    ArrayRef<std::function<void(PassBuilder &)>> PassBuilderCallbacks,
    OutputKind OK, VerifierKind VK, bool ShouldPreserveAssemblyUseListOrder,
    bool ShouldPreserveBitcodeUseListOrder, bool EmitSummaryIndex,
    bool EmitModuleHash, bool EnableDebugify, bool VerifyDIPreserve,
    bool EnableProfcheck, bool UnifiedLTO, const NPMOptions &NPMOpts) {
  // Map PGOColdFuncAttrKind -> PGOOptions::ColdFuncOpt.
  auto toColdFuncOpt = [](PGOColdFuncAttrKind K) {
    switch (K) {
    case PGOColdFuncAttrKind::OptSize:
      return PGOOptions::ColdFuncOpt::OptSize;
    case PGOColdFuncAttrKind::MinSize:
      return PGOOptions::ColdFuncOpt::MinSize;
    case PGOColdFuncAttrKind::OptNone:
      return PGOOptions::ColdFuncOpt::OptNone;
    default:
      return PGOOptions::ColdFuncOpt::Default;
    }
  };
  PGOOptions::ColdFuncOpt ColdFuncAttr = toColdFuncOpt(NPMOpts.PGOColdFuncAttr);

  std::optional<PGOOptions> P;
  switch (NPMOpts.PGOKindFlag) {
  case InstrGen:
    P = PGOOptions(NPMOpts.ProfileFile, "", "", NPMOpts.MemoryProfileFile,
                   PGOOptions::IRInstr, PGOOptions::NoCSAction, ColdFuncAttr);
    break;
  case InstrUse:
    P = PGOOptions(NPMOpts.ProfileFile, "", NPMOpts.ProfileRemappingFile,
                   NPMOpts.MemoryProfileFile, PGOOptions::IRUse,
                   PGOOptions::NoCSAction, ColdFuncAttr);
    break;
  case SampleUse:
    P = PGOOptions(NPMOpts.ProfileFile, "", NPMOpts.ProfileRemappingFile,
                   NPMOpts.MemoryProfileFile, PGOOptions::SampleUse,
                   PGOOptions::NoCSAction, ColdFuncAttr);
    break;
  case NoPGO:
    if (NPMOpts.DebugInfoForProfiling || NPMOpts.PseudoProbeForProfiling ||
        !NPMOpts.MemoryProfileFile.empty())
      P = PGOOptions("", "", "", NPMOpts.MemoryProfileFile,
                     PGOOptions::NoAction, PGOOptions::NoCSAction, ColdFuncAttr,
                     NPMOpts.DebugInfoForProfiling,
                     NPMOpts.PseudoProbeForProfiling);
    else
      P = std::nullopt;
  }
  if (NPMOpts.CSPGOKindFlag != NoCSPGO) {
    if (P && (P->Action == PGOOptions::IRInstr ||
              P->Action == PGOOptions::SampleUse)) {
      errs() << "CSPGOKind cannot be used with IRInstr or SampleUse";
      return false;
    }
    if (NPMOpts.CSPGOKindFlag == CSInstrGen) {
      if (NPMOpts.CSProfileGenFile.empty()) {
        errs() << "CSInstrGen needs to specify CSProfileGenFile";
        return false;
      }
      if (P) {
        P->CSAction = PGOOptions::CSIRInstr;
        P->CSProfileGenFile = NPMOpts.CSProfileGenFile;
      } else
        P = PGOOptions(
            "", NPMOpts.CSProfileGenFile, NPMOpts.ProfileRemappingFile,
            /*MemoryProfile=*/"", PGOOptions::NoAction, PGOOptions::CSIRInstr);
    } else /* CSInstrUse */ {
      if (!P) {
        errs() << "CSInstrUse needs to be together with InstrUse";
        return false;
      }
      P->CSAction = PGOOptions::CSIRUse;
    }
  }

  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;

  if (TM) {
    TM->setPGOOption(P);

    MAM.registerPass([&] {
      const TargetOptions &Options = TM->Options;
      return RuntimeLibraryAnalysis(Options.ExceptionModel, Options.EABIVersion,
                                    Options.MCOptions.ABIName, Options.VecLib);
    });
  }

  PassInstrumentationCallbacks PIC;
  PrintPassOptions PrintPassOpts;
  PrintPassOpts.Verbose = NPMOpts.DebugPM == DebugLogging::Verbose;
  PrintPassOpts.SkipAnalyses = NPMOpts.DebugPM == DebugLogging::Quiet;
  StandardInstrumentations SI(M.getContext(),
                              NPMOpts.DebugPM != DebugLogging::None,
                              VK == VerifierKind::EachPass, PrintPassOpts);
  SI.registerCallbacks(PIC, &MAM);
  DebugifyEachInstrumentation Debugify;
  DebugifyStatsMap DIStatsMap;
  DebugInfoPerPass DebugInfoBeforePass;
  if (NPMOpts.DebugifyEach) {
    Debugify.setDIStatsMap(DIStatsMap);
    Debugify.setDebugifyMode(DebugifyMode::SyntheticDebugInfo);
    Debugify.registerCallbacks(PIC, MAM);
  } else if (NPMOpts.VerifyEachDebugInfoPreserve) {
    Debugify.setDebugInfoBeforePass(DebugInfoBeforePass);
    Debugify.setDebugifyMode(DebugifyMode::OriginalDebugInfo);
    Debugify.setOrigDIVerifyBugsReportFilePath(NPMOpts.VerifyDIPreserveExport);
    Debugify.registerCallbacks(PIC, MAM);
  }

  auto &OptsCtx = M.getContext().getOptionsContext();
  PipelineTuningOptions PTO(OptsCtx);
  // LoopUnrolling defaults on to true and DisableLoopUnrolling is initialized
  // to false above so we shouldn't necessarily need to check whether or not the
  // option has been enabled.
  PTO.LoopUnrolling = !NPMOpts.DisableLoopUnrolling;
  PTO.UnifiedLTO = UnifiedLTO;
  PTO.LoopFusion = NPMOpts.EnableLoopFusion;
  PassBuilder PB(OptsCtx, TM, PTO, P, &PIC, vfs::getRealFileSystem());
  registerEPCallbacks(PB, NPMOpts);

  // For any loaded plugins, let them register pass builder callbacks.
  for (auto &PassPlugin : PassPlugins)
    PassPlugin.registerPassBuilderCallbacks(PB);

  // Load any explicitly specified plugins.
  for (auto &PassCallback : PassBuilderCallbacks)
    PassCallback(PB);

#define HANDLE_EXTENSION(Ext)                                                  \
  get##Ext##PluginInfo().RegisterPassBuilderCallbacks(PB);
#include "llvm/Support/Extension.def"
#undef HANDLE_EXTENSION

  // Specially handle the alias analysis manager so that we can register
  // a custom pipeline of AA passes with it.
  AAManager AA;
  if (auto Err = PB.parseAAPipeline(AA, NPMOpts.AAPipeline)) {
    errs() << Arg0 << ": " << toString(std::move(Err)) << "\n";
    return false;
  }

  // Register the AA manager first so that our version is the one used.
  FAM.registerPass([&] { return std::move(AA); });
  // Register our TargetLibraryInfoImpl.
  FAM.registerPass([&] { return TargetLibraryAnalysis(*TLII); });

  // Register all the basic analyses with the managers.
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  ModulePassManager MPM;
  if (EnableDebugify)
    MPM.addPass(NewPMDebugifyPass());
  if (VerifyDIPreserve)
    MPM.addPass(NewPMDebugifyPass(DebugifyMode::OriginalDebugInfo, "",
                                  &DebugInfoBeforePass));
  if (EnableProfcheck)
    MPM.addPass(createModuleToFunctionPassAdaptor(ProfileInjectorPass()));
  // Add passes according to the -passes options.
  if (!PassPipeline.empty()) {
    if (auto Err = PB.parsePassPipeline(MPM, PassPipeline)) {
      errs() << Arg0 << ": " << toString(std::move(Err)) << "\n";
      return false;
    }
  }

  if (VK != VerifierKind::None)
    MPM.addPass(VerifierPass());
  if (EnableDebugify)
    MPM.addPass(NewPMCheckDebugifyPass(false, "", &DIStatsMap));
  if (VerifyDIPreserve)
    MPM.addPass(NewPMCheckDebugifyPass(
        false, "", nullptr, DebugifyMode::OriginalDebugInfo,
        &DebugInfoBeforePass, NPMOpts.VerifyDIPreserveExport));
  if (EnableProfcheck)
    MPM.addPass(ProfileVerifierPass());

  // Add any relevant output pass at the end of the pipeline.
  switch (OK) {
  case OK_NoOutput:
    break; // No output pass needed.
  case OK_OutputAssembly:
    if (EmitSummaryIndex) {
      MPM.addPass(AssignGUIDPass());
    }
    MPM.addPass(PrintModulePass(
        Out->os(), "", ShouldPreserveAssemblyUseListOrder, EmitSummaryIndex));
    break;
  case OK_OutputBitcode:
    if (EmitSummaryIndex) {
      MPM.addPass(AssignGUIDPass());
    }
    MPM.addPass(BitcodeWriterPass(Out->os(), ShouldPreserveBitcodeUseListOrder,
                                  EmitSummaryIndex, EmitModuleHash));
    break;
  case OK_OutputThinLTOBitcode:
    MPM.addPass(AssignGUIDPass());
    MPM.addPass(ThinLTOBitcodeWriterPass(
        Out->os(), ThinLTOLinkOut ? &ThinLTOLinkOut->os() : nullptr,
        ShouldPreserveBitcodeUseListOrder));
    break;
  }

  // Before executing passes, print the final values of the LLVM options.

  // Print a textual, '-passes=' compatible, representation of pipeline if
  // requested.
  bool DoPrintPipeline = false;
  PrintPipelinePassesFormat PipelineFormat = PrintPipelinePassesFormat::Text;
  if (auto *O = clv2::getView<&clv2::PassesOptsReg>(OptsCtx)) {
    DoPrintPipeline = O->specified<&clv2::PAS_PrintPipelinePasses>();
    if (DoPrintPipeline) {
      StringRef FormatStr = O->get<&clv2::PAS_PrintPipelinePasses>();
      if (FormatStr == "tree")
        PipelineFormat = PrintPipelinePassesFormat::Tree;
    }
  }
  if (DoPrintPipeline) {
    std::string Pipeline;
    raw_string_ostream SOS(Pipeline);
    MPM.printPipeline(SOS, [&PIC](StringRef ClassName) {
      auto PassName = PIC.getPassNameForClassName(ClassName);
      return PassName.empty() ? ClassName : PassName;
    });
    printFormattedPipelinePasses(outs(), Pipeline, PipelineFormat);
    outs() << "\n";

    if (!NPMOpts.DisablePipelineVerification) {
      // Check that we can parse the returned pipeline string as an actual
      // pipeline.
      ModulePassManager TempPM;
      if (auto Err = PB.parsePassPipeline(TempPM, Pipeline)) {
        errs() << "Could not parse dumped pass pipeline: "
               << toString(std::move(Err)) << "\n";
        return false;
      }
    }

    return true;
  }

  // Now that we have all of the passes ready, run them.
  MPM.run(M, MAM);

  // If a pass reported an error via LLVMContext::emitError, fail without
  // writing the output module.
  if (auto *DH = M.getContext().getDiagHandlerPtr()) {
    if (DH->HasErrors)
      return false;
  }

  // Declare success.
  if (OK != OK_NoOutput) {
    Out->keep();
    if (OK == OK_OutputThinLTOBitcode && ThinLTOLinkOut)
      ThinLTOLinkOut->keep();
  }

  if (OptRemarkFile)
    OptRemarkFile->keep();

  if (NPMOpts.DebugifyEach && !NPMOpts.DebugifyExport.empty())
    exportDebugifyStats(NPMOpts.DebugifyExport, Debugify.getDebugifyStatsMap());

  TimerGroup::printAll(*CreateInfoOutputFile());
  TimerGroup::clearAll();

  return true;
}

void llvm::printPasses(raw_ostream &OS) {
  PassBuilder PB(/*OptsCtx=*/llvm::clv2::defaultOptionsContext());
  PB.printPassNames(OS);
}
