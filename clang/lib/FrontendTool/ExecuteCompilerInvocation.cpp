//===--- ExecuteCompilerInvocation.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file holds ExecuteCompilerInvocation(). It is split into its own file to
// minimize the impact of pulling in essentially everything else in Clang.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/DiagnosticFrontend.h"
#include "clang/CodeGen/ClangCodeGenOptionsOptInfos.h"
#include "clang/CodeGen/CodeGenAction.h"
#include "clang/Config/config.h"
#include "clang/ExtractAPI/FrontendActions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Frontend/SSAFOptions.h"
#include "clang/Frontend/Utils.h"
#include "clang/FrontendTool/Utils.h"
#include "clang/Options/Options.h"
#include "clang/Rewrite/Frontend/FrontendActions.h"
#include "clang/ScalableStaticAnalysis/Frontend/SourceTransformationFrontendAction.h"
#include "clang/ScalableStaticAnalysis/Frontend/TUSummaryExtractorFrontendAction.h"
#include "clang/ScalableStaticAnalysis/SSAFForceLinker.h" // IWYU pragma: keep
#include "clang/StaticAnalyzer/Core/AnalyzerOptions.h"
#include "clang/StaticAnalyzer/Frontend/AnalyzerHelpFlags.h"
#include "clang/StaticAnalyzer/Frontend/FrontendActions.h"
#include "llvm/AsmParser/AsmParserOptions.h"
#include "llvm/CGData/CGDataOptions.h"
#include "llvm/CodeGen/CodeGenPassOptionsAsmPrint.h"
#include "llvm/CodeGen/CodeGenPassOptionsCore1.h"
#include "llvm/CodeGen/CodeGenPassOptionsCore2.h"
#include "llvm/CodeGen/CodeGenPassOptionsGISel.h"
#include "llvm/CodeGen/CodeGenPassOptionsMachine1.h"
#include "llvm/CodeGen/CodeGenPassOptionsMachine2.h"
#include "llvm/CodeGen/CodeGenPassOptionsRegAlloc.h"
#include "llvm/CodeGen/CodeGenPassOptionsSched1.h"
#include "llvm/CodeGen/CodeGenPassOptionsSched2.h"
#include "llvm/CodeGen/CodeGenPassOptionsSelDAG.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/Config/Targets.h"
#if LLVM_HAS_ARC_TARGET
#include "llvm/Target/ARC/ARCOptions.h"
#endif
#if LLVM_HAS_CSKY_TARGET
#include "llvm/Target/CSKY/CSKYOptions.h"
#endif
#if LLVM_HAS_LANAI_TARGET
#include "llvm/Target/Lanai/LanaiOptions.h"
#endif
#if LLVM_HAS_M68K_TARGET
#include "llvm/Target/M68k/M68kOptions.h"
#endif
#if LLVM_HAS_SYSTEMZ_TARGET
#include "llvm/Target/SystemZ/SystemZOptions.h"
#endif
#include "llvm/Frontend/OpenMP/OpenMPOptions.h"
#include "llvm/IR/IROptions.h"
#include "llvm/LTO/LTOOptions.h"
#include "llvm/MC/MCOptions.h"
#include "llvm/Object/ObjectOptions.h"
#include "llvm/Option/LibraryOptions.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Passes/PassesOptions.h"
#include "llvm/ProfileData/ProfileDataOptions.h"
#include "llvm/Remarks/RemarksOptions.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/BuryPointer.h"
#include "llvm/Support/ColorOptions.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/PluginLoaderOptions.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/SupportOptions.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/ARM/ARMOptions.h"
#include "llvm/Target/BPF/BPFOptions.h"
#include "llvm/Target/Hexagon/HexagonOptions.h"
#include "llvm/Target/LoongArch/LoongArchOptions.h"
#include "llvm/Target/MSP430/MSP430Options.h"
#include "llvm/Target/Mips/MipsOptions.h"
#include "llvm/Target/NVPTX/NVPTXOptions.h"
#include "llvm/Target/PowerPC/PowerPCOptions.h"
#include "llvm/Target/RISCV/RISCVOptions.h"
#include "llvm/Target/SPIRV/SPIRVOptions.h"
#include "llvm/Target/Sparc/SparcOptions.h"
#include "llvm/Target/WebAssembly/WebAssemblyOptions.h"
#include "llvm/Target/X86/X86Options.h"
#include "llvm/Target/XCore/XCoreOptions.h"
#include "llvm/Transforms/AggressiveInstCombine/AggressiveInstCombineOptions.h"
#include "llvm/Transforms/Coroutines/CoroutinesOptions.h"
#include "llvm/Transforms/InstCombine/InstCombineOptions.h"
#include "llvm/Transforms/Instrumentation/InstrumentationOptions.h"
#include "llvm/Transforms/IPO/IPOOptions.h"
#include "llvm/Transforms/ObjCARC/ObjCARCOptions.h"
#include "llvm/Transforms/Scalar/ScalarOptions.h"
#include "llvm/Transforms/Utils/UtilsOptions.h"

#if CLANG_ENABLE_CIR
#include "mlir/IR/AsmState.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/MLIROptionsOptInfos.h"
#include "mlir/Pass/PassManager.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/FrontendAction/CIRGenAction.h"
#endif

using namespace clang;
using namespace llvm::opt;

namespace clang {

static std::unique_ptr<FrontendAction>
CreateFrontendBaseAction(CompilerInstance &CI) {
  using namespace clang::frontend;
  StringRef Action("unknown");
  (void)Action;

  unsigned UseCIR = CI.getFrontendOpts().UseClangIRPipeline;
  frontend::ActionKind Act = CI.getFrontendOpts().ProgramAction;
  bool EmitsCIR = Act == EmitCIR;

  if (!UseCIR && EmitsCIR)
    llvm::report_fatal_error("-emit-cir and only valid when using -fclangir");

  switch (CI.getFrontendOpts().ProgramAction) {
  case ASTDeclList:
    return std::make_unique<ASTDeclListAction>();
  case ASTDump:
    return std::make_unique<ASTDumpAction>();
  case ASTPrint:
    return std::make_unique<ASTPrintAction>();
  case ASTView:
    return std::make_unique<ASTViewAction>();
  case DumpCompilerOptions:
    return std::make_unique<DumpCompilerOptionsAction>();
  case DumpRawTokens:
    return std::make_unique<DumpRawTokensAction>();
  case DumpTokens:
    return std::make_unique<DumpTokensAction>();
  case EmitAssembly:
#if CLANG_ENABLE_CIR
    if (UseCIR)
      return std::make_unique<cir::EmitAssemblyAction>();
#endif
    return std::make_unique<EmitAssemblyAction>();
  case EmitBC:
#if CLANG_ENABLE_CIR
    if (UseCIR)
      return std::make_unique<cir::EmitBCAction>();
#endif
    return std::make_unique<EmitBCAction>();
  case EmitCIR:
#if CLANG_ENABLE_CIR
    return std::make_unique<cir::EmitCIRAction>();
#else
    CI.getDiagnostics().Report(diag::err_fe_cir_not_built);
    return nullptr;
#endif
  case EmitHTML:
    return std::make_unique<HTMLPrintAction>();
  case EmitLLVM: {
#if CLANG_ENABLE_CIR
    if (UseCIR)
      return std::make_unique<cir::EmitLLVMAction>();
#endif
    return std::make_unique<EmitLLVMAction>();
  }
  case EmitLLVMOnly:
    return std::make_unique<EmitLLVMOnlyAction>();
  case EmitCodeGenOnly:
    return std::make_unique<EmitCodeGenOnlyAction>();
  case EmitObj:
#if CLANG_ENABLE_CIR
    if (UseCIR)
      return std::make_unique<cir::EmitObjAction>();
#endif
    return std::make_unique<EmitObjAction>();
  case ExtractAPI:
    return std::make_unique<ExtractAPIAction>();
  case FixIt:
    return std::make_unique<FixItAction>();
  case GenerateModule:
    return std::make_unique<GenerateModuleFromModuleMapAction>();
  case GenerateModuleInterface:
    return std::make_unique<GenerateModuleInterfaceAction>();
  case GenerateReducedModuleInterface:
    return std::make_unique<GenerateReducedModuleInterfaceAction>();
  case GenerateHeaderUnit:
    return std::make_unique<GenerateHeaderUnitAction>();
  case GeneratePCH:
    return std::make_unique<GeneratePCHAction>();
  case GenerateInterfaceStubs:
    return std::make_unique<GenerateInterfaceStubsAction>();
  case InitOnly:
    return std::make_unique<InitOnlyAction>();
  case ParseSyntaxOnly:
    return std::make_unique<SyntaxOnlyAction>();
  case ModuleFileInfo:
    return std::make_unique<DumpModuleInfoAction>();
  case VerifyPCH:
    return std::make_unique<VerifyPCHAction>();

  case PluginAction: {
    for (const FrontendPluginRegistry::entry &Plugin :
         FrontendPluginRegistry::entries()) {
      if (Plugin.getName() == CI.getFrontendOpts().ActionName) {
        std::unique_ptr<PluginASTAction> P(Plugin.instantiate());
        if ((P->getActionType() != PluginASTAction::ReplaceAction &&
             P->getActionType() != PluginASTAction::CmdlineAfterMainAction) ||
            !P->ParseArgs(
                CI,
                CI.getFrontendOpts().PluginArgs[std::string(Plugin.getName())]))
          return nullptr;
        return std::move(P);
      }
    }

    CI.getDiagnostics().Report(diag::err_fe_invalid_plugin_name)
        << CI.getFrontendOpts().ActionName;
    return nullptr;
  }

  case PrintPreamble:
    return std::make_unique<PrintPreambleAction>();
  case PrintPreprocessedInput: {
    if (CI.getPreprocessorOutputOpts().RewriteIncludes ||
        CI.getPreprocessorOutputOpts().RewriteImports)
      return std::make_unique<RewriteIncludesAction>();
    return std::make_unique<PrintPreprocessedAction>();
  }

  case RewriteMacros:
    return std::make_unique<RewriteMacrosAction>();
  case RewriteTest:
    return std::make_unique<RewriteTestAction>();
#if CLANG_ENABLE_OBJC_REWRITER
  case RewriteObjC:
    return std::make_unique<RewriteObjCAction>();
#else
  case RewriteObjC:
    Action = "RewriteObjC";
    break;
#endif
#if CLANG_ENABLE_STATIC_ANALYZER
  case RunAnalysis:
    return std::make_unique<ento::AnalysisAction>();
#else
  case RunAnalysis:
    Action = "RunAnalysis";
    break;
#endif
  case RunPreprocessorOnly:
    return std::make_unique<PreprocessOnlyAction>();
  case PrintDependencyDirectivesSourceMinimizerOutput:
    return std::make_unique<PrintDependencyDirectivesSourceMinimizerAction>();
  }

#if !CLANG_ENABLE_STATIC_ANALYZER || !CLANG_ENABLE_OBJC_REWRITER
  CI.getDiagnostics().Report(diag::err_fe_action_not_available) << Action;
  return 0;
#else
  llvm_unreachable("Invalid program action!");
#endif
}

std::unique_ptr<FrontendAction> CreateFrontendAction(CompilerInstance &CI) {
  // Create the underlying action.
  std::unique_ptr<FrontendAction> Act = CreateFrontendBaseAction(CI);
  if (!Act)
    return nullptr;

  const FrontendOptions &FEOpts = CI.getFrontendOpts();

  if (CI.getLangOpts().HLSL)
    Act = std::make_unique<HLSLFrontendAction>(std::move(Act));

  if (FEOpts.FixAndRecompile) {
    Act = std::make_unique<FixItRecompile>(std::move(Act));
  }

  // Wrap the base FE action in an extract api action to generate
  // symbol graph as a biproduct of compilation (enabled with
  // --emit-symbol-graph option)
  if (FEOpts.EmitSymbolGraph) {
    if (FEOpts.SymbolGraphOutputDir.empty()) {
      CI.getDiagnostics().Report(diag::warn_missing_symbol_graph_dir);
      CI.getFrontendOpts().SymbolGraphOutputDir = ".";
    }
    CI.getCodeGenOpts().ClearASTBeforeBackend = false;
    Act = std::make_unique<WrappingExtractAPIAction>(std::move(Act));
  }

  // If there are any AST files to merge, create a frontend action
  // adaptor to perform the merge.
  if (!FEOpts.ASTMergeFiles.empty())
    Act =
        std::make_unique<ASTMergeAction>(std::move(Act), FEOpts.ASTMergeFiles);

  if (!CI.getSSAFOpts().TUSummaryFile.empty()) {
    Act = std::make_unique<ssaf::TUSummaryExtractorFrontendAction>(
        std::move(Act));
  }
  // Enter the source-transformation action when the transformation option is
  // set, and also when only an output option (--ssaf-src-edit-file= /
  // --ssaf-transformation-report-file=) is set — the action's
  // reportOrphanOptionMisuse then diagnoses that as a reverse orphan rather
  // than silently ignoring the option.
  if (!CI.getSSAFOpts().SourceTransformation.empty() ||
      !CI.getSSAFOpts().SrcEditFile.empty() ||
      !CI.getSSAFOpts().TransformationReportFile.empty()) {
    Act = std::make_unique<ssaf::SourceTransformationFrontendAction>(
        std::move(Act));
  }
  return Act;
}

bool ExecuteCompilerInvocation(CompilerInstance *Clang) {
  unsigned NumErrorsBefore = Clang->getDiagnostics().getNumErrors();

  // Honor -help.
  if (Clang->getFrontendOpts().ShowHelp) {
    getDriverOptTable().printHelp(
        llvm::outs(), "clang -cc1 [options] file...",
        "LLVM 'Clang' Compiler: http://clang.llvm.org",
        /*ShowHidden=*/false, /*ShowAllAliases=*/false,
        llvm::opt::Visibility(options::CC1Option));
    return true;
  }

  // Honor -version.
  //
  // FIXME: Use a better -version message?
  if (Clang->getFrontendOpts().ShowVersion) {
    llvm::cl::PrintVersionMessage();
    return true;
  }

  Clang->LoadRequestedPlugins();

  // Honor -mllvm.
  //
  // FIXME: Remove this, one day.
  // This should happen AFTER plugins have been loaded!
  // pre-RA-sched registration is handled by llc's ExtraEntries;
  // clang's BackendUtil creates TargetMachines directly without this option.
  if (!Clang->getFrontendOpts().LLVMArgs.empty()) {
    llvm::SmallVector<llvm::StringRef> ArgRefs;
    for (const auto &A : Clang->getFrontendOpts().LLVMArgs)
      ArgRefs.push_back(A);
    {
      llvm::clv2::OptionParser P;
      llvm::RegisterAllLLVMOptions(P);
      P.add<&llvm::clv2::ClangCodeGenOptsReg>();
      llvm::BumpPtrAllocator Alloc;
      llvm::StringSaver Saver(Alloc);
      llvm::SmallVector<const char *> Argv;
      Argv.push_back(Saver.save("clang (LLVM option parsing)").data());
      for (llvm::StringRef A : ArgRefs)
        Argv.push_back(Saver.save(A).data());
      std::vector<const char *> ArgsAfterPlugins =
          llvm::loadPluginsAndStripArgs(static_cast<int>(Argv.size()),
                                        Argv.data());

      // The libraries below have migrated off clv2 onto the new per-library
      // OptTable/hand-written struct design (see
      // llvm/include/llvm/Option/LibraryOptions.h) and are no longer among
      // the clv2::OptionParser registries RegisterAllLLVMOptions() populates
      // above. Parse them out of the (plugin-stripped) -mllvm args first,
      // forwarding whatever none of them recognizes to the legacy clv2
      // parser unchanged -- mirroring the pattern established in
      // llvm/tools/opt/optdriver.cpp.
      llvm::SmallVector<const char *, 32> LibraryOptsRest;
      {
        std::string LibraryOptsErrs;
        llvm::raw_string_ostream LibraryOptsErrsOS(LibraryOptsErrs);
        if (llvm::Error Err = llvm::opt::parseLibraryOptionsChain<
                llvm::SupportOptions, llvm::PassesOptions, llvm::ColorOptions,
                llvm::CodeGenAsmPrintOptions, llvm::CodeGenGISelOptions,
                llvm::CodeGenMachine1Options, llvm::CodeGenMachine2Options,
                llvm::CodeGenRegAllocOptions, llvm::CodeGenSched1Options,
                llvm::CodeGenSched2Options, llvm::CodeGenSelDAGOptions,
                llvm::CodeGenCore2Options, llvm::CodeGenCore1Options,
                llvm::ObjectOptions, llvm::RemarksOptions,
                llvm::AsmParserOptions, llvm::XCoreOptions,
                llvm::ObjCARCOptions, llvm::MSP430Options, llvm::SparcOptions,
                llvm::SPIRVOptions, llvm::BPFOptions, llvm::LoongArchOptions,
                llvm::MipsOptions, llvm::NVPTXOptions, llvm::WebAssemblyOptions,
                llvm::ARMOptions, llvm::RISCVOptions, llvm::X86Options,
                llvm::PowerPCOptions, llvm::HexagonOptions
#if LLVM_HAS_ARC_TARGET
                ,
                llvm::ARCOptions
#endif
#if LLVM_HAS_CSKY_TARGET
                ,
                llvm::CSKYOptions
#endif
#if LLVM_HAS_LANAI_TARGET
                ,
                llvm::LanaiOptions
#endif
#if LLVM_HAS_M68K_TARGET
                ,
                llvm::M68kOptions
#endif
#if LLVM_HAS_SYSTEMZ_TARGET
                ,
                llvm::SystemZOptions
#endif
                ,
                llvm::CoroutinesOptions, llvm::OpenMPOptions,
                llvm::AggressiveInstCombineOptions, llvm::InstCombineCLOptions,
                llvm::CGDataOptions, llvm::LTOOptions, llvm::ProfileDataOptions,
                llvm::MCLibraryOptions, llvm::IROptions, llvm::UtilsOptions,
                llvm::InstrumentationOptions, llvm::IPOOptions,
                llvm::ScalarOptions>(
                llvm::ArrayRef<const char *>(ArgsAfterPlugins.data() + 1,
                                             ArgsAfterPlugins.data() +
                                                 ArgsAfterPlugins.size()),
                LibraryOptsRest, LibraryOptsErrsOS)) {
          llvm::errs() << "clang (LLVM option parsing): "
                       << llvm::toString(std::move(Err)) << "\n";
          return false;
        }
        llvm::errs() << LibraryOptsErrs;
      }
      // IROptions has no automatic apply step (unlike the other libraries in
      // the chain above, which are read on demand via Ctx.getOptions<T>());
      // it must sync a couple of legacy globals (TimePassesIsEnabled/
      // TimePassesPerRun and the OptBisect singleton) explicitly. See
      // llvm/lib/IR/IROptions.cpp.
      llvm::ir_opts::applyIROptions();
      llvm::SmallVector<const char *, 32> ArgvAfterLibraryOpts;
      ArgvAfterLibraryOpts.push_back(ArgsAfterPlugins[0]);
      ArgvAfterLibraryOpts.append(LibraryOptsRest.begin(),
                                  LibraryOptsRest.end());

      Clang->setLLVMOptionsContext(
          P.parse(static_cast<int>(ArgvAfterLibraryOpts.size()),
                  ArgvAfterLibraryOpts.data()));
    }
  }

#if CLANG_ENABLE_STATIC_ANALYZER
  // These should happen AFTER plugins have been loaded!

  AnalyzerOptions &AnOpts = Clang->getAnalyzerOpts();

  // Honor -analyzer-checker-help and -analyzer-checker-help-hidden.
  if (AnOpts.ShowCheckerHelp || AnOpts.ShowCheckerHelpAlpha ||
      AnOpts.ShowCheckerHelpDeveloper) {
    ento::printCheckerHelp(llvm::outs(), *Clang);
    return true;
  }

  // Honor -analyzer-checker-option-help.
  if (AnOpts.ShowCheckerOptionList || AnOpts.ShowCheckerOptionAlphaList ||
      AnOpts.ShowCheckerOptionDeveloperList) {
    ento::printCheckerConfigList(llvm::outs(), *Clang);
    return true;
  }

  // Honor -analyzer-list-enabled-checkers.
  if (AnOpts.ShowEnabledCheckerList) {
    ento::printEnabledCheckerList(llvm::outs(), *Clang);
    return true;
  }

  // Honor -analyzer-config-help.
  if (AnOpts.ShowConfigOptionsList) {
    ento::printAnalyzerConfigList(llvm::outs());
    return true;
  }
#endif

#if CLANG_ENABLE_CIR
  if (!Clang->getFrontendOpts().MLIRArgs.empty()) {
    mlir::registerCIRPasses();
    mlir::registerMLIRContextCLOptions();
    mlir::registerAsmPrinterCLOptions();
    unsigned NumArgs = Clang->getFrontendOpts().MLIRArgs.size();
    auto Args = std::make_unique<const char *[]>(NumArgs + 2);
    Args[0] = "clang (MLIR option parsing)";
    for (unsigned i = 0; i != NumArgs; ++i)
      Args[i + 1] = Clang->getFrontendOpts().MLIRArgs[i].c_str();
    Args[NumArgs + 1] = nullptr;
    llvm::clv2::OptionParser MP;
    MP.add<&llvm::clv2::MLIROptsReg, mlir::mlir_opts::applyMLIROpts>();
    mlir::registerPassManagerCLOptions(MP);
    MP.parse(NumArgs + 1, Args.get());
  }
#endif

  // If there were errors in the above, don't do anything else.
  // This intentionally ignores errors emitted before this function to
  // accommodate lenient callers that decided to make progress despite errors.
  if (Clang->getDiagnostics().getNumErrors() != NumErrorsBefore)
    return false;

  // Create and execute the frontend action.
  std::unique_ptr<FrontendAction> Act(CreateFrontendAction(*Clang));
  if (!Act)
    return false;
  bool Success = Clang->ExecuteAction(*Act);
  if (Clang->getFrontendOpts().DisableFree)
    llvm::BuryPointer(std::move(Act));
  return Success;
}

} // namespace clang
