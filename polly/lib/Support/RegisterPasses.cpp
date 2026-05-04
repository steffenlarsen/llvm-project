//===------ RegisterPasses.cpp - Add the Polly Passes to default passes  --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file composes the individual LLVM-IR passes provided by Polly to a
// functional polyhedral optimizer. The polyhedral optimizer is automatically
// made available to LLVM based compilers by loading the Polly shared library
// into such a compiler.
//
// The Polly optimizer is made available by executing a static constructor that
// registers the individual Polly passes in the LLVM pass manager builder. The
// passes are registered such that the default behaviour of the compiler is not
// changed, but that the flag '-polly' provided at optimization level '-O3'
// enables additional polyhedral optimizations.
//===----------------------------------------------------------------------===//

#include "polly/RegisterPasses.h"
#include "polly/Canonicalization.h"
#include "polly/CodeGen/CodeGeneration.h"
#include "polly/CodeGen/IslAst.h"
#include "polly/CodePreparation.h"
#include "polly/DeLICM.h"
#include "polly/DeadCodeElimination.h"
#include "polly/DependenceInfo.h"
#include "polly/ForwardOpTree.h"
#include "polly/JSONExporter.h"
#include "polly/MaximalStaticExpansion.h"
#include "polly/Pass/PollyFunctionPass.h"
#include "polly/PollyOptionsOptInfos.h"
#include "polly/PruneUnprofitable.h"
#include "polly/ScheduleOptimizer.h"
#include "polly/ScopDetection.h"
#include "polly/ScopGraphPrinter.h"
#include "polly/ScopInfo.h"
#include "polly/ScopInliner.h"
#include "polly/Simplify.h"
#include "polly/Support/DumpFunctionPass.h"
#include "polly/Support/DumpModulePass.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Analysis/CFGPrinter.h"
#include "llvm/Config/llvm-config.h" // for LLVM_VERSION_STRING
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Transforms/IPO.h"

using namespace llvm;
using namespace polly;

using llvm::FunctionPassManager;
using llvm::OptimizationLevel;
using llvm::PassBuilder;
using llvm::PassInstrumentationCallbacks;

cl::OptionCategory PollyCategory("Polly Options",
                                 "Configure the polly loop optimizer");

namespace polly {

enum PassPositionChoice { POSITION_EARLY, POSITION_BEFORE_VECTORIZER };
enum OptimizerChoice { OPTIMIZER_NONE, OPTIMIZER_ISL };
enum CodeGenChoice { CODEGEN_FULL, CODEGEN_AST, CODEGEN_NONE };

static bool shouldEnablePollyForOptimization(const clv2::OptionsContext &Ctx) {
  auto *Opts = polly_opts::getPollyOpts(Ctx);
  return Opts ? Opts->get<&llvm::clv2::POLLY_Enabled>() : false;
}

static bool shouldEnablePollyForDiagnostic(const clv2::OptionsContext &Ctx) {
  auto *Opts = polly_opts::getPollyOpts(Ctx);
  if (!Opts)
    return false;

  bool OnlyPrinter = Opts->get<&llvm::clv2::POLLY_DotOnly>();
  bool Printer = Opts->get<&llvm::clv2::POLLY_Dot>();
  bool OnlyViewer = Opts->get<&llvm::clv2::POLLY_ShowOnly>();
  bool Viewer = Opts->get<&llvm::clv2::POLLY_Show>();
  bool Export = Opts->get<&llvm::clv2::POLLY_Export>();

  return OnlyPrinter || Printer || OnlyViewer || Viewer || Export;
}

/// Parser of parameters for LoopVectorize pass.
static llvm::Expected<PollyPassOptions>
parsePollyOptions(StringRef Params, bool IsCustom,
                  const clv2::OptionsContext &Ctx) {
  PassPhase PrevPhase = PassPhase::None;

  bool EnableDefaultOpts = !IsCustom;
  bool EnableEnd2End = !IsCustom;
  std::optional<bool>
      PassEnabled[static_cast<size_t>(PassPhase::PassPhaseLast) + 1];
  PassPhase StopAfter = PassPhase::None;

  // Read options from context.
  auto *Opts = polly_opts::getPollyOpts(Ctx);

  bool PrintDetect = Opts ? Opts->get<&llvm::clv2::POLLY_PrintDetect>() : false;
  bool PrintScops = Opts ? Opts->get<&llvm::clv2::POLLY_PrintScops>() : false;
  bool PrintDeps = Opts ? Opts->get<&llvm::clv2::POLLY_PrintDeps>() : false;
  bool PollyViewer = Opts ? Opts->get<&llvm::clv2::POLLY_Show>() : false;
  bool PollyOnlyViewer =
      Opts ? Opts->get<&llvm::clv2::POLLY_ShowOnly>() : false;
  bool PollyPrinter = Opts ? Opts->get<&llvm::clv2::POLLY_Dot>() : false;
  bool PollyOnlyPrinter =
      Opts ? Opts->get<&llvm::clv2::POLLY_DotOnly>() : false;
  bool EnableSimplify =
      Opts ? Opts->get<&llvm::clv2::POLLY_EnableSimplify>() : true;
  bool EnableForwardOpTree =
      Opts ? Opts->get<&llvm::clv2::POLLY_EnableOptree>() : true;
  bool EnableDeLICM =
      Opts ? Opts->get<&llvm::clv2::POLLY_EnableDelicm>() : true;
  bool ImportJScop = Opts ? Opts->get<&llvm::clv2::POLLY_Import>() : false;
  bool DeadCodeElim = Opts ? Opts->get<&llvm::clv2::POLLY_RunDce>() : false;
  bool FullyIndexedStaticExpansion =
      Opts ? Opts->get<&llvm::clv2::POLLY_EnableMse>() : false;
  bool EnablePruneUnprofitable =
      Opts ? Opts->get<&llvm::clv2::POLLY_EnablePruneUnprofitable>() : true;
  auto OptimizerVal = Opts ? static_cast<OptimizerChoice>(
                                 Opts->get<&llvm::clv2::POLLY_Optimizer>())
                           : OPTIMIZER_ISL;
  bool ExportJScop = Opts ? Opts->get<&llvm::clv2::POLLY_Export>() : false;
  auto CodeGenerationVal =
      Opts ? static_cast<CodeGenChoice>(
                 Opts->get<&llvm::clv2::POLLY_CodeGeneration>())
           : CODEGEN_FULL;

  // Passes enabled using command-line flags (can be overridden using
  // 'polly<no-pass>')
  if (PrintDetect)
    PassEnabled[static_cast<size_t>(PassPhase::PrintDetect)] = true;
  if (PrintScops)
    PassEnabled[static_cast<size_t>(PassPhase::PrintScopInfo)] = true;
  if (PrintDeps)
    PassEnabled[static_cast<size_t>(PassPhase::PrintDependences)] = true;

  if (PollyViewer)
    PassEnabled[static_cast<size_t>(PassPhase::ViewScops)] = true;
  if (PollyOnlyViewer)
    PassEnabled[static_cast<size_t>(PassPhase::ViewScopsOnly)] = true;
  if (PollyPrinter)
    PassEnabled[static_cast<size_t>(PassPhase::DotScops)] = true;
  if (PollyOnlyPrinter)
    PassEnabled[static_cast<size_t>(PassPhase::DotScopsOnly)] = true;
  if (!EnableSimplify)
    PassEnabled[static_cast<size_t>(PassPhase::Simplify0)] = false;
  if (!EnableForwardOpTree)
    PassEnabled[static_cast<size_t>(PassPhase::Optree)] = false;
  if (!EnableDeLICM)
    PassEnabled[static_cast<size_t>(PassPhase::DeLICM)] = false;
  if (!EnableSimplify)
    PassEnabled[static_cast<size_t>(PassPhase::Simplify1)] = false;
  if (ImportJScop)
    PassEnabled[static_cast<size_t>(PassPhase::ImportJScop)] = true;
  if (DeadCodeElim)
    PassEnabled[static_cast<size_t>(PassPhase::DeadCodeElimination)] = true;
  if (FullyIndexedStaticExpansion)
    PassEnabled[static_cast<size_t>(PassPhase::MaximumStaticExtension)] = true;
  if (!EnablePruneUnprofitable)
    PassEnabled[static_cast<size_t>(PassPhase::PruneUnprofitable)] = false;
  switch (OptimizerVal) {
  case OPTIMIZER_NONE:
    // explicitly switched off
    PassEnabled[static_cast<size_t>(PassPhase::Optimization)] = false;
    break;
  case OPTIMIZER_ISL:
    // default: enabled
    break;
  }
  if (ExportJScop)
    PassEnabled[static_cast<size_t>(PassPhase::ExportJScop)] = true;
  switch (CodeGenerationVal) {
  case CODEGEN_AST:
    PassEnabled[static_cast<size_t>(PassPhase::AstGen)] = true;
    PassEnabled[static_cast<size_t>(PassPhase::CodeGen)] = false;
    break;
  case CODEGEN_FULL:
    // default: ast and codegen enabled
    break;
  case CODEGEN_NONE:
    PassEnabled[static_cast<size_t>(PassPhase::AstGen)] = false;
    PassEnabled[static_cast<size_t>(PassPhase::CodeGen)] = false;
    break;
  }

  while (!Params.empty()) {
    StringRef Param;
    std::tie(Param, Params) = Params.split(';');
    auto [ParamName, ParamVal] = Param.split('=');

    if (ParamName == "stopafter") {
      StopAfter = parsePhase(ParamVal);
      if (StopAfter == PassPhase::None)
        return make_error<StringError>(
            formatv("invalid stopafter parameter value '{0}'", ParamVal).str(),
            inconvertibleErrorCode());
      continue;
    }

    if (!ParamVal.empty())
      return make_error<StringError>(
          formatv("parameter '{0}' does not take value", ParamName).str(),
          inconvertibleErrorCode());

    bool Enabled = true;
    if (ParamName.starts_with("no-")) {
      Enabled = false;
      ParamName = ParamName.drop_front(3);
    }

    if (ParamName == "default-opts") {
      EnableDefaultOpts = Enabled;
      continue;
    }

    if (ParamName == "end2end") {
      EnableEnd2End = Enabled;
      continue;
    }

    PassPhase Phase;

    // Shortcut for both simplifys at the same time
    if (ParamName == "simplify") {
      PassEnabled[static_cast<size_t>(PassPhase::Simplify0)] = Enabled;
      PassEnabled[static_cast<size_t>(PassPhase::Simplify1)] = Enabled;
      Phase = PassPhase::Simplify0;
    } else {
      Phase = parsePhase(ParamName);
      if (Phase == PassPhase::None)
        return make_error<StringError>(
            formatv("invalid Polly parameter/phase name '{0}'", ParamName)
                .str(),
            inconvertibleErrorCode());

      if (PrevPhase >= Phase)
        return make_error<StringError>(
            formatv("phases must not be repeated and enumerated in-order: "
                    "'{0}' listed before '{1}'",
                    getPhaseName(PrevPhase), getPhaseName(Phase))
                .str(),
            inconvertibleErrorCode());

      PassEnabled[static_cast<size_t>(Phase)] = Enabled;
    }
    PrevPhase = Phase;
  }

  PollyPassOptions PPOpts;
  // Read ViewAll/ViewFilter/PrintDepsAnalysisLevel from context.
  PPOpts.ViewAll = Opts ? Opts->get<&llvm::clv2::POLLY_ViewAll>() : false;
  PPOpts.ViewFilter =
      Opts ? Opts->get<&llvm::clv2::POLLY_ViewOnly>() : std::string();
  PPOpts.PrintDepsAnalysisLevel =
      Opts ? static_cast<Dependences::AnalysisLevel>(
                 Opts->get<&llvm::clv2::POLLY_DependencesAnalysisLevel>())
           : Dependences::AL_Statement;

  // Implicitly enable dependent phases first. May be overriden explicitly
  // on/off later.
  for (PassPhase P : llvm::enum_seq_inclusive(PassPhase::PassPhaseFirst,
                                              PassPhase::PassPhaseLast)) {
    bool Enabled = PassEnabled[static_cast<size_t>(P)].value_or(false);
    if (!Enabled)
      continue;

    if (static_cast<size_t>(PassPhase::Detection) < static_cast<size_t>(P))
      PPOpts.setPhaseEnabled(PassPhase::Detection);

    if (static_cast<size_t>(PassPhase::ScopInfo) < static_cast<size_t>(P))
      PPOpts.setPhaseEnabled(PassPhase::ScopInfo);

    if (dependsOnDependenceInfo(P))
      PPOpts.setPhaseEnabled(PassPhase::Dependences);

    if (static_cast<size_t>(PassPhase::AstGen) < static_cast<size_t>(P))
      PPOpts.setPhaseEnabled(PassPhase::AstGen);
  }

  if (EnableEnd2End)
    PPOpts.enableEnd2End();

  if (EnableDefaultOpts)
    PPOpts.enableDefaultOpts();

  for (PassPhase P : llvm::enum_seq_inclusive(PassPhase::PassPhaseFirst,
                                              PassPhase::PassPhaseLast)) {
    std::optional<bool> Enabled = PassEnabled[static_cast<size_t>(P)];

    // Apply only if set explicitly.
    if (Enabled.has_value())
      PPOpts.setPhaseEnabled(P, *Enabled);
  }

  if (StopAfter != PassPhase::None)
    PPOpts.disableAfter(StopAfter);

  if (Error CheckResult = PPOpts.checkConsistency())
    return CheckResult;

  return PPOpts;
}

static llvm::Expected<PollyPassOptions>
parsePollyDefaultOptions(StringRef Params, const clv2::OptionsContext &Ctx) {
  return parsePollyOptions(Params, false, Ctx);
}

static llvm::Expected<PollyPassOptions>
parsePollyCustomOptions(StringRef Params, const clv2::OptionsContext &Ctx) {
  return parsePollyOptions(Params, true, Ctx);
}

/// Register Polly passes such that they form a polyhedral optimizer.
///
/// The individual Polly passes are registered in the pass manager such that
/// they form a full polyhedral optimizer. The flow of the optimizer starts with
/// a set of preparing transformations that canonicalize the LLVM-IR such that
/// the LLVM-IR is easier for us to understand and to optimizes. On the
/// canonicalized LLVM-IR we first run the ScopDetection pass, which detects
/// static control flow regions. Those regions are then translated by the
/// ScopInfo pass into a polyhedral representation. As a next step, a scheduling
/// optimizer is run on the polyhedral representation and finally the optimized
/// polyhedral representation is code generated back to LLVM-IR.
///
/// Besides this core functionality, we optionally schedule passes that provide
/// a graphical view of the scops (Polly[Only]Viewer, Polly[Only]Printer), that
/// allow the export/import of the polyhedral representation
/// (JSCON[Exporter|Importer]) or that show the cfg after code generation.
///
/// For certain parts of the Polly optimizer, several alternatives are provided:
///
/// As scheduling optimizer we support the isl scheduling optimizer
/// (http://freecode.com/projects/isl).
/// It is also possible to run Polly with no optimizer. This mode is mainly
/// provided to analyze the run and compile time changes caused by the
/// scheduling optimizer.
///
/// Polly supports the isl internal code generator.

/// Add the pass sequence required for Polly to the New Pass Manager.
///
/// @param PM           The pass manager itself.
/// @param Level        The optimization level. Used for the cleanup of Polly's
///                     output.
/// @param EnableForOpt Whether to add Polly IR transformations. If False, only
///                     the analysis passes are added, skipping Polly itself.
///                     The IR may still be modified.
/// @param Ctx          The options context for reading clv2 options.
static void buildCommonPollyPipeline(FunctionPassManager &PM,
                                     OptimizationLevel Level,
                                     IntrusiveRefCntPtr<vfs::FileSystem> FS,
                                     bool EnableForOpt,
                                     const clv2::OptionsContext &Ctx) {
  PassBuilder PB(Ctx,
                 /*TM=*/nullptr,
                 /*PipelineTuningOptions=*/PipelineTuningOptions(Ctx),
                 /*PGOOpt=*/{},
                 /*PIC=*/nullptr, std::move(FS));

  ExitOnError Err("Inconsistent Polly configuration: ");
  PollyPassOptions &&PPOpts =
      Err(parsePollyOptions(StringRef(), /*IsCustom=*/false, Ctx));
  PM.addPass(PollyFunctionPass(PPOpts));

  PM.addPass(PB.buildFunctionSimplificationPipeline(
      Level, llvm::ThinOrFullLTOPhase::None)); // Cleanup

  auto *Opts = polly_opts::getPollyOpts(Ctx);
  bool ViewCfg = Opts ? Opts->get<&llvm::clv2::POLLY_ViewCfg>() : false;
  if (ViewCfg)
    PM.addPass(llvm::CFGPrinterPass());
}

static void buildEarlyPollyPipeline(llvm::ModulePassManager &MPM,
                                    llvm::OptimizationLevel Level,
                                    IntrusiveRefCntPtr<vfs::FileSystem> FS,
                                    const clv2::OptionsContext &Ctx) {
  bool EnableForOpt =
      shouldEnablePollyForOptimization(Ctx) && Level != OptimizationLevel::O0;
  if (!shouldEnablePollyForDiagnostic(Ctx) && !EnableForOpt)
    return;

  FunctionPassManager FPM = buildCanonicalicationPassesForNPM(MPM, Level, Ctx);

  auto *Opts = polly_opts::getPollyOpts(Ctx);
  bool DumpBefore = Opts ? Opts->get<&llvm::clv2::POLLY_DumpBefore>() : false;
  auto DumpBeforeFile = Opts ? Opts->get<&llvm::clv2::POLLY_DumpBeforeFile>()
                             : std::vector<std::string>();
  bool DumpAfter = Opts ? Opts->get<&llvm::clv2::POLLY_DumpAfter>() : false;
  auto DumpAfterFile = Opts ? Opts->get<&llvm::clv2::POLLY_DumpAfterFile>()
                            : std::vector<std::string>();

  if (DumpBefore || !DumpBeforeFile.empty()) {
    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));

    if (DumpBefore)
      MPM.addPass(DumpModulePass("-before", true));
    for (auto &Filename : DumpBeforeFile)
      MPM.addPass(DumpModulePass(Filename, false));

    FPM = FunctionPassManager();
  }

  buildCommonPollyPipeline(FPM, Level, std::move(FS), EnableForOpt, Ctx);
  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));

  if (DumpAfter)
    MPM.addPass(DumpModulePass("-after", true));
  for (auto &Filename : DumpAfterFile)
    MPM.addPass(DumpModulePass(Filename, false));
}

static void buildLatePollyPipeline(FunctionPassManager &PM,
                                   llvm::OptimizationLevel Level,
                                   IntrusiveRefCntPtr<vfs::FileSystem> FS,
                                   const clv2::OptionsContext &Ctx) {
  bool EnableForOpt =
      shouldEnablePollyForOptimization(Ctx) && Level != OptimizationLevel::O0;
  if (!shouldEnablePollyForDiagnostic(Ctx) && !EnableForOpt)
    return;

  auto *Opts = polly_opts::getPollyOpts(Ctx);
  bool DumpBefore = Opts ? Opts->get<&llvm::clv2::POLLY_DumpBefore>() : false;
  auto DumpBeforeFile = Opts ? Opts->get<&llvm::clv2::POLLY_DumpBeforeFile>()
                             : std::vector<std::string>();
  bool DumpAfter = Opts ? Opts->get<&llvm::clv2::POLLY_DumpAfter>() : false;
  auto DumpAfterFile = Opts ? Opts->get<&llvm::clv2::POLLY_DumpAfterFile>()
                            : std::vector<std::string>();

  if (DumpBefore)
    PM.addPass(DumpFunctionPass("-before"));
  if (!DumpBeforeFile.empty())
    llvm::report_fatal_error(
        "Option -polly-dump-before-file at -polly-position=late "
        "not supported with NPM",
        false);

  buildCommonPollyPipeline(PM, Level, std::move(FS), EnableForOpt, Ctx);

  if (DumpAfter)
    PM.addPass(DumpFunctionPass("-after"));
  if (!DumpAfterFile.empty())
    llvm::report_fatal_error(
        "Option -polly-dump-after-file at -polly-position=late "
        "not supported with NPM",
        false);
}

static llvm::Expected<std::monostate>
parseNoOptions(StringRef Params, const clv2::OptionsContext &) {
  if (!Params.empty())
    return make_error<StringError>(
        formatv("'{0}' passed to pass that does not take any options", Params)
            .str(),
        inconvertibleErrorCode());

  return std::monostate{};
}

static llvm::Expected<bool>
parseCGPipeline(StringRef Name, llvm::CGSCCPassManager &CGPM,
                PassInstrumentationCallbacks *PIC,
                ArrayRef<PassBuilder::PipelineElement> Pipeline,
                IntrusiveRefCntPtr<vfs::FileSystem> FS,
                const clv2::OptionsContext &Ctx) {
#define CGSCC_PASS(NAME, CREATE_PASS, PARSER)                                  \
  if (PassBuilder::checkParametrizedPassName(Name, NAME)) {                    \
    auto Params = PassBuilder::parsePassParameters(                            \
        [&Ctx](StringRef P) { return PARSER(P, Ctx); }, Name, NAME);           \
    if (!Params)                                                               \
      return Params.takeError();                                               \
    CGPM.addPass(CREATE_PASS);                                                 \
    return true;                                                               \
  }
#include "PollyPasses.def"

  return false;
}

static llvm::Expected<bool>
parseFunctionPipeline(StringRef Name, FunctionPassManager &FPM,
                      PassInstrumentationCallbacks *PIC,
                      ArrayRef<PassBuilder::PipelineElement> Pipeline,
                      const clv2::OptionsContext &Ctx) {

#define FUNCTION_PASS(NAME, CREATE_PASS, PARSER)                               \
  if (PassBuilder::checkParametrizedPassName(Name, NAME)) {                    \
    auto ExpectedOpts = PassBuilder::parsePassParameters(                      \
        [&Ctx](StringRef P) { return PARSER(P, Ctx); }, Name, NAME);           \
    if (!ExpectedOpts)                                                         \
      return ExpectedOpts.takeError();                                         \
    auto &&Opts = *ExpectedOpts;                                               \
    (void)Opts;                                                                \
    FPM.addPass(CREATE_PASS);                                                  \
    return true;                                                               \
  }

#include "PollyPasses.def"
  return false;
}

static llvm::Expected<bool>
parseModulePipeline(StringRef Name, llvm::ModulePassManager &MPM,
                    PassInstrumentationCallbacks *PIC,
                    ArrayRef<PassBuilder::PipelineElement> Pipeline,
                    const clv2::OptionsContext &Ctx) {
#define MODULE_PASS(NAME, CREATE_PASS, PARSER)                                 \
  if (PassBuilder::checkParametrizedPassName(Name, NAME)) {                    \
    auto ExpectedOpts = PassBuilder::parsePassParameters(                      \
        [&Ctx](StringRef P) { return PARSER(P, Ctx); }, Name, NAME);           \
    if (!ExpectedOpts)                                                         \
      return ExpectedOpts.takeError();                                         \
    auto &&Opts = *ExpectedOpts;                                               \
    (void)Opts;                                                                \
    MPM.addPass(CREATE_PASS);                                                  \
    return true;                                                               \
  }

#include "PollyPasses.def"

  return false;
}

/// Register Polly to be available as an optimizer
///
///
/// We can currently run Polly at two different points int the pass manager.
/// a) very early, b) right before the vectorizer.
///
/// The default is currently a), to register Polly such that it runs as early as
/// possible. This has several implications:
///
///   1) We need to schedule more canonicalization passes
///
///   As nothing is run before Polly, it is necessary to run a set of preparing
///   transformations before Polly to canonicalize the LLVM-IR and to allow
///   Polly to detect and understand the code.
///
///   2) We get the full -O3 optimization sequence after Polly
///
///   The LLVM-IR that is generated by Polly has been optimized on a high level,
///   but it may be rather inefficient on the lower/scalar level. By scheduling
///   Polly before all other passes, we have the full sequence of -O3
///   optimizations behind us, such that inefficiencies on the low level can
///   be optimized away.
///
/// We are currently evaluating the benefit or running Polly at b). b) is nice
/// as everything is fully inlined and canonicalized, but we need to be able to
/// handle LICMed code to make it useful.
void registerPollyPasses(PassBuilder &PB) {
  PassInstrumentationCallbacks *PIC = PB.getPassInstrumentationCallbacks();
  IntrusiveRefCntPtr<vfs::FileSystem> FS = PB.getVirtualFileSystemPtr();
  auto &Ctx = PB.getOptionsContext();

#define MODULE_PASS(NAME, CREATE_PASS, PARSER)                                 \
  {                                                                            \
    std::remove_reference_t<decltype(*PARSER(StringRef(), Ctx))> Opts;         \
    (void)Opts;                                                                \
    PIC->addClassToPassName(decltype(CREATE_PASS)::name(), NAME);              \
  }
#define CGSCC_PASS(NAME, CREATE_PASS, PARSER)                                  \
  {                                                                            \
    std::remove_reference_t<decltype(*PARSER(StringRef(), Ctx))> Opts;         \
    (void)Opts;                                                                \
    PIC->addClassToPassName(decltype(CREATE_PASS)::name(), NAME);              \
  }
#define FUNCTION_PASS(NAME, CREATE_PASS, PARSER)                               \
  {                                                                            \
    std::remove_reference_t<decltype(*PARSER(StringRef(), Ctx))> Opts;         \
    (void)Opts;                                                                \
    PIC->addClassToPassName(decltype(CREATE_PASS)::name(), NAME);              \
  }
#include "PollyPasses.def"

  PB.registerPipelineParsingCallback(
      [PIC, &Ctx](StringRef Name, FunctionPassManager &FPM,
                  ArrayRef<PassBuilder::PipelineElement> Pipeline) -> bool {
        ExitOnError Err("Unable to parse Polly function pass: ");
        return Err(parseFunctionPipeline(Name, FPM, PIC, Pipeline, Ctx));
      });
  PB.registerPipelineParsingCallback(
      [PIC, FS, &Ctx](StringRef Name, CGSCCPassManager &CGPM,
                      ArrayRef<PassBuilder::PipelineElement> Pipeline) -> bool {
        ExitOnError Err("Unable to parse Polly call graph pass: ");
        return Err(parseCGPipeline(Name, CGPM, PIC, Pipeline, FS, Ctx));
      });
  PB.registerPipelineParsingCallback(
      [PIC, &Ctx](StringRef Name, ModulePassManager &MPM,
                  ArrayRef<PassBuilder::PipelineElement> Pipeline) -> bool {
        ExitOnError Err("Unable to parse Polly module pass: ");
        return Err(parseModulePipeline(Name, MPM, PIC, Pipeline, Ctx));
      });

  // Read PassPosition from context.
  auto *Opts = polly_opts::getPollyOpts(Ctx);
  auto PassPosition = Opts ? static_cast<PassPositionChoice>(
                                 Opts->get<&llvm::clv2::POLLY_Position>())
                           : POSITION_BEFORE_VECTORIZER;

  switch (PassPosition) {
  case POSITION_EARLY:
    PB.registerPipelineStartEPCallback(
        [FS, &Ctx](ModulePassManager &MPM, OptimizationLevel Level) {
          buildEarlyPollyPipeline(MPM, Level, FS, Ctx);
        });
    break;
  case POSITION_BEFORE_VECTORIZER:
    PB.registerVectorizerStartEPCallback(
        [FS, &Ctx](FunctionPassManager &FPM, OptimizationLevel Level) {
          buildLatePollyPipeline(FPM, Level, FS, Ctx);
        });
    break;
  }
}
} // namespace polly

llvm::PassPluginLibraryInfo getPollyPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "Polly", LLVM_VERSION_STRING,
          polly::registerPollyPasses};
}
