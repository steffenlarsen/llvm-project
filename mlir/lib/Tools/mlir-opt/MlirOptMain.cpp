//===- MlirOptMain.cpp - MLIR Optimizer Driver ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a utility that runs an optimization pass and prints the result back
// out. It is designed to support unit testing.
//
//===----------------------------------------------------------------------===//

#include "mlir/Tools/mlir-opt/MlirOptMain.h"
#include "mlir/Bytecode/BytecodeWriter.h"
#include "mlir/Debug/CLOptionsSetup.h"
#include "mlir/Debug/Counter.h"
#include "mlir/Dialect/IRDL/IR/IRDL.h"
#include "mlir/Dialect/IRDL/IRDLLoading.h"
#include "mlir/IR/AsmState.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Location.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/MLIROptionsOptInfos.h"
#include "mlir/IR/Remarks.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Remark/RemarkStreamer.h"
#include "mlir/Support/FileUtilities.h"
#include "mlir/Support/Timing.h"
#include "mlir/Support/ToolUtilities.h"
#include "mlir/Tools/ParseUtilities.h"
#include "mlir/Tools/Plugins/DialectPlugin.h"
#include "mlir/Tools/Plugins/PassPlugin.h"
#include "mlir/Tools/mlir-opt/MlirOptToolOptionsOptInfos.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Remarks/RemarkFormat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/LogicalResult.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/SupportOptions.h"
#include "llvm/Support/SupportOptionsOptInfos.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/ToolOutputFile.h"

using namespace mlir;
using namespace llvm;

namespace {
/// This class is intended to manage the handling of command line options for
// Plugins are loaded as a side effect of parsing, and must stay that way: a
// plugin registers passes/dialects that later options (e.g. --pass-pipeline)
// need to resolve during the same parse.
static void loadPassPluginValue(const std::string &Path) {
  auto plugin = PassPlugin::load(Path);
  if (!plugin) {
    errs() << "Failed to load passes from '" << Path << "'. Request ignored.\n";
    return;
  }
  plugin.get().registerPassRegistryCallbacks();
}

// Needs the DialectRegistry, which a plain function-pointer Callback cannot
// carry, so its descriptor is built at runtime with a CtxCallback.
static bool loadDialectPluginValue(void *Ctx, const std::string &Path) {
  auto *registry = static_cast<DialectRegistry *>(Ctx);
  if (!registry)
    return true;
  auto plugin = DialectPlugin::load(Path);
  if (!plugin) {
    errs() << "Failed to load dialect plugin from '" << Path
           << "'. Request ignored.\n";
    return true;
  }
  plugin.get().registerDialectRegistryCallbacks(*registry);
  return true;
}

static constexpr llvm::clv2::OptionInfo<std::string> OI_LoadPassPlugin{
    "load-pass-plugin", "Load passes from plugin library",
    llvm::clv2::ZeroOrMore,
    llvm::clv2::Callback<std::string>{&loadPassPluginValue}};

/// creating a *-opt config. This is a singleton.
struct MlirOptMainConfigCLOptions : public MlirOptMainConfig {
  MlirOptMainConfigCLOptions()
      : passPipeline("", "Compiler passes to run", "p") {
    setPassPipelineParser(passPipeline);
  }

  PassPipelineCLParser passPipeline;

  using MlirOptToolOpts =
      decltype(llvm::clv2::MlirOptToolOptsReg)::ParsedOptionsT;

  void populateFromParsedOpts(const MlirOptToolOpts &O) {
    using namespace llvm::clv2;
    allowUnregisteredDialectsFlag = O.get<&MLIROPT_AllowUnregisteredDialect>();
    dumpPassPipelineFlag = O.get<&MLIROPT_DumpPassPipeline>();
    emitBytecodeFlag = O.get<&MLIROPT_EmitBytecode>();
    elideResourceDataFromBytecodeFlag = O.get<&MLIROPT_ElideResourceData>();
    emitBytecodeProducerFlag = O.get<&MLIROPT_EmitBytecodeProducer>();
    if (O.specified<&MLIROPT_EmitBytecodeVersion>())
      emitBytecodeVersion = O.get<&MLIROPT_EmitBytecodeVersion>();
    irdlFileFlag = O.get<&MLIROPT_IrdlFile>();
    diagnosticVerbosityLevelFlag =
        static_cast<VerbosityLevel>(O.get<&MLIROPT_DiagnosticVerbosityLevel>());
    disableDiagnosticNotesFlag = O.get<&MLIROPT_DisableDiagnosticNotes>();
    useExplicitModuleFlag = O.get<&MLIROPT_NoImplicitModule>();
    listPassesFlag = O.get<&MLIROPT_ListPasses>();
    runReproducerFlag = O.get<&MLIROPT_RunReproducer>();
    showDialectsFlag = O.get<&MLIROPT_ShowDialects>();
    if (O.specified<&MLIROPT_SplitInputFile>()) {
      auto marker = O.get<&MLIROPT_SplitInputFile>();
      splitInputFileFlag =
          marker.empty() ? kDefaultSplitMarker : std::string(marker);
    }
    outputSplitMarkerFlag = O.get<&MLIROPT_OutputSplitMarker>();
    if (O.specified<&MLIROPT_VerifyDiagnostics>()) {
      auto level = O.get<&MLIROPT_VerifyDiagnostics>();
      if (level == MLIROpt_VerifyDiagLevel::OnlyExpected)
        verifyDiagnosticsFlag =
            SourceMgrDiagnosticVerifierHandler::Level::OnlyExpected;
      else
        verifyDiagnosticsFlag = SourceMgrDiagnosticVerifierHandler::Level::All;
    }
    verifyPassesFlag = O.get<&MLIROPT_VerifyEach>();
    disableVerifierOnParsingFlag = O.get<&MLIROPT_DisableVerifierOnParsing>();
    verifyRoundtripFlag = O.get<&MLIROPT_VerifyRoundtrip>();
    generateReproducerFileFlag = O.get<&MLIROPT_GenerateReproducer>();
    remarkFormatFlag =
        static_cast<RemarkFormat>(O.get<&MLIROPT_RemarkFormat>());
    remarkPolicyFlag =
        static_cast<RemarkPolicy>(O.get<&MLIROPT_RemarkPolicy>());
    remarksOutputFileFlag = O.get<&MLIROPT_RemarksOutputFile>();
    remarksAllFilterFlag = O.get<&MLIROPT_RemarksFilter>();
    remarksPassedFilterFlag = O.get<&MLIROPT_RemarksFilterPassed>();
    remarksFailedFilterFlag = O.get<&MLIROPT_RemarksFilterFailed>();
    remarksMissedFilterFlag = O.get<&MLIROPT_RemarksFilterMissed>();
    remarksAnalyseFilterFlag = O.get<&MLIROPT_RemarksFilterAnalyse>();
  }

  void registerPluginOptions(llvm::clv2::OptionParser &P,
                             DialectRegistry *registry) {
    using namespace llvm::clv2;
    P.addDynamicEntry(llvm::clv2::makeEntry<&OI_LoadPassPlugin>(
        passPluginPath, passPluginCount));

    dialectPluginOpt.emplace(
        "load-dialect-plugin", "Load dialects from plugin library", ZeroOrMore,
        CtxCallback<std::string>{&loadDialectPluginValue, registry});
    P.addDynamicEntry(dialectPluginOpt->makeEntry());
  }

  /// Flush all dynamic option entries (pass pipeline + plugin options)
  /// into the given OptionParser.
  void registerDynamicOptions(llvm::clv2::OptionParser &P) {
    passPipeline.registerWith(P);
    registerPluginOptions(P, dialectRegistryPtr);
  }

  void setDialectPluginsCallback(DialectRegistry &registry) {
    this->dialectRegistryPtr = &registry;
  }

  DialectRegistry *dialectRegistryPtr = nullptr;

  // Parse destinations for the plugin options; the work happens in their
  // callbacks, but clv2 still needs somewhere to store the value.
  std::string passPluginPath;
  unsigned passPluginCount = 0;
  // Runtime-constructed because it carries the DialectRegistry as context, so
  // it is a RuntimeOption (which owns the descriptor, its static info, and the
  // value slot) rather than a constexpr OptionInfo.  Must outlive the parse,
  // hence a member rather than a local.
  std::optional<llvm::clv2::RuntimeOption<std::string>> dialectPluginOpt;
};

/// A scoped diagnostic handler that suppresses certain diagnostics based on
/// the verbosity level and whether the diagnostic is a note.
class DiagnosticFilter : public ScopedDiagnosticHandler {
public:
  DiagnosticFilter(MLIRContext *ctx, VerbosityLevel verbosityLevel,
                   bool showNotes = true)
      : ScopedDiagnosticHandler(ctx) {
    setHandler([verbosityLevel, showNotes](Diagnostic &diag) {
      auto severity = diag.getSeverity();
      switch (severity) {
      case mlir::DiagnosticSeverity::Error:
        // failure indicates that the error is not handled by the filter and
        // goes through to the default handler. Therefore, the error can be
        // successfully printed.
        return failure();
      case mlir::DiagnosticSeverity::Warning:
        if (verbosityLevel == VerbosityLevel::ErrorsOnly)
          return success();
        else
          return failure();
      case mlir::DiagnosticSeverity::Remark:
        if (verbosityLevel == VerbosityLevel::ErrorsOnly ||
            verbosityLevel == VerbosityLevel::ErrorsAndWarnings)
          return success();
        else
          return failure();
      case mlir::DiagnosticSeverity::Note:
        if (showNotes)
          return failure();
        else
          return success();
      }
      llvm_unreachable("Unknown diagnostic severity");
    });
  }
};
} // namespace

ManagedStatic<MlirOptMainConfigCLOptions> clOptionsConfig;

static void
applyMlirOptToolOpts(const MlirOptMainConfigCLOptions::MlirOptToolOpts &Opts) {
  clOptionsConfig->populateFromParsedOpts(Opts);
}

void MlirOptMainConfig::registerCLOptions(DialectRegistry &registry) {
  clOptionsConfig->setDialectPluginsCallback(registry);
  tracing::DebugConfig::registerCLOptions();
}

void MlirOptMainConfig::registerCLOptions(llvm::clv2::OptionParser &P,
                                          DialectRegistry &registry) {
  registerCLOptions(registry);
  clOptionsConfig->registerPluginOptions(P, &registry);
}

MlirOptMainConfig MlirOptMainConfig::createFromCLOptions(
    const llvm::clv2::OptionsContext &optsCtx) {
  clOptionsConfig->setDebugConfig(
      tracing::DebugConfig::createFromCLOptions(optsCtx));
  clOptionsConfig->setOptionsContext(optsCtx);
  return *clOptionsConfig;
}

MlirOptMainConfig &MlirOptMainConfig::setPassPipelineParser(
    const PassPipelineCLParser &passPipeline) {
  passPipelineCallback = [&](PassManager &pm) {
    auto errorHandler = [&](const Twine &msg) {
      emitError(UnknownLoc::get(pm.getContext())) << msg;
      return failure();
    };
    if (failed(passPipeline.addToPipeline(pm, errorHandler)))
      return failure();
    if (this->shouldDumpPassPipeline()) {

      pm.dump();
      llvm::errs() << "\n";
    }
    return success();
  };
  return *this;
}


LogicalResult loadIRDLDialects(StringRef irdlFile, MLIRContext &ctx) {
  DialectRegistry registry;
  registry.insert<irdl::IRDLDialect>();
  ctx.appendDialectRegistry(registry);

  // Set up the input file.
  std::string errorMessage;
  std::unique_ptr<MemoryBuffer> file = openInputFile(irdlFile, &errorMessage);
  if (!file) {
    emitError(UnknownLoc::get(&ctx)) << errorMessage;
    return failure();
  }

  // Give the buffer to the source manager.
  // This will be picked up by the parser.
  SourceMgr sourceMgr;
  sourceMgr.AddNewSourceBuffer(std::move(file), SMLoc());

  SourceMgrDiagnosticHandler sourceMgrHandler(sourceMgr, &ctx);

  // Parse the input file.
  OwningOpRef<ModuleOp> module(parseSourceFile<ModuleOp>(sourceMgr, &ctx));
  if (!module)
    return failure();

  // Load IRDL dialects.
  return irdl::loadDialects(module.get());
}

// Return success if the module can correctly round-trip. This intended to test
// that the custom printers/parsers are complete.
static LogicalResult doVerifyRoundTrip(Operation *op,
                                       const MlirOptMainConfig &config,
                                       bool useBytecode) {
  // We use a new context to avoid resource handle renaming issue in the diff.
  // It gets the same options as the original, so that both sides of the
  // comparison print under identical flags.
  MLIRContext roundtripContext(op->getContext()->getOptionsContext());
  OwningOpRef<Operation *> roundtripModule;
  roundtripContext.appendDialectRegistry(
      op->getContext()->getDialectRegistry());
  if (op->getContext()->allowsUnregisteredDialects())
    roundtripContext.allowUnregisteredDialects();
  StringRef irdlFile = config.getIrdlFile();
  if (!irdlFile.empty() && failed(loadIRDLDialects(irdlFile, roundtripContext)))
    return failure();

  std::string testType = (useBytecode) ? "bytecode" : "textual";
  // Print a first time with custom format (or bytecode) and parse it back to
  // the roundtripModule.
  {
    std::string buffer;
    llvm::raw_string_ostream ostream(buffer);
    if (useBytecode) {
      if (failed(writeBytecodeToFile(op, ostream))) {
        op->emitOpError()
            << "failed to write bytecode, cannot verify round-trip.\n";
        return failure();
      }
    } else {
      op->print(ostream,
                opPrintingFlags(op).printGenericOpForm().enableDebugInfo());
    }
    FallbackAsmResourceMap fallbackResourceMap;
    ParserConfig parseConfig(&roundtripContext, config.shouldVerifyOnParsing(),
                             &fallbackResourceMap);
    roundtripModule = parseSourceString<Operation *>(buffer, parseConfig);
    if (!roundtripModule) {
      op->emitOpError() << "failed to parse " << testType
                        << " content back, cannot verify round-trip.\n";
      return failure();
    }
  }

  // Print in the generic form for the reference module and the round-tripped
  // one and compare the outputs.
  std::string reference, roundtrip;
  {
    llvm::raw_string_ostream ostreamref(reference);
    op->print(ostreamref,
              opPrintingFlags(op).printGenericOpForm().enableDebugInfo());
    llvm::raw_string_ostream ostreamrndtrip(roundtrip);
    roundtripModule.get()->print(ostreamrndtrip,
                                 opPrintingFlags(roundtripModule.get())
                                     .printGenericOpForm()
                                     .enableDebugInfo());
  }
  if (reference != roundtrip) {
    // TODO implement a diff.
    return op->emitOpError()
           << testType
           << " roundTrip testing roundtripped module differs "
              "from reference:\n<<<<<<Reference\n"
           << reference << "\n=====\n"
           << roundtrip << "\n>>>>>roundtripped\n";
  }

  return success();
}

static LogicalResult doVerifyRoundTrip(Operation *op,
                                       const MlirOptMainConfig &config) {
  auto txtStatus = doVerifyRoundTrip(op, config, /*useBytecode=*/false);
  auto bcStatus = doVerifyRoundTrip(op, config, /*useBytecode=*/true);
  return success(succeeded(txtStatus) && succeeded(bcStatus));
}

/// Perform the actions on the input file indicated by the command line flags
/// within the specified context.
///
/// This typically parses the main source file, runs zero or more optimization
/// passes, then prints the output.
///
static LogicalResult
performActions(raw_ostream &os,
               const std::shared_ptr<llvm::SourceMgr> &sourceMgr,
               MLIRContext *context, const MlirOptMainConfig &config) {
  DefaultTimingManager tm;
  applyDefaultTimingManagerCLOptions(tm, context->getOptionsContext());
  TimingScope timing = tm.getRootScope();

  // Disable multi-threading when parsing the input file. This removes the
  // unnecessary/costly context synchronization when parsing.
  bool wasThreadingEnabled = context->isMultithreadingEnabled();
  context->disableMultithreading();

  // Prepare the parser config, and attach any useful/necessary resource
  // handlers. Unhandled external resources are treated as passthrough, i.e.
  // they are not processed and will be emitted directly to the output
  // untouched.
  PassReproducerOptions reproOptions;
  FallbackAsmResourceMap fallbackResourceMap;
  ParserConfig parseConfig(context, config.shouldVerifyOnParsing(),
                           &fallbackResourceMap);
  if (config.shouldRunReproducer())
    reproOptions.attachResourceParser(parseConfig);

  // Parse the input file and reset the context threading state.
  TimingScope parserTiming = timing.nest("Parser");
  OwningOpRef<Operation *> op = parseSourceFileForTool(
      sourceMgr, parseConfig, !config.shouldUseExplicitModule());
  parserTiming.stop();
  if (!op)
    return failure();

  // Perform round-trip verification if requested
  if (config.shouldVerifyRoundtrip() &&
      failed(doVerifyRoundTrip(op.get(), config)))
    return failure();

  context->enableMultithreading(wasThreadingEnabled);
  // Set the remark categories and policy.
  remark::RemarkCategories cats{
      config.getRemarksAllFilter(), config.getRemarksPassedFilter(),
      config.getRemarksMissedFilter(), config.getRemarksAnalyseFilter(),
      config.getRemarksFailedFilter()};

  mlir::MLIRContext &ctx = *context;
  // Helper to create the appropriate policy based on configuration
  auto createPolicy = [&config]()
      -> std::unique_ptr<mlir::remark::detail::RemarkEmittingPolicyBase> {
    if (config.getRemarkPolicy() == RemarkPolicy::REMARK_POLICY_ALL)
      return std::make_unique<mlir::remark::RemarkEmittingPolicyAll>();
    if (config.getRemarkPolicy() == RemarkPolicy::REMARK_POLICY_FINAL)
      return std::make_unique<mlir::remark::RemarkEmittingPolicyFinal>();

    llvm_unreachable("Invalid remark policy");
  };

  switch (config.getRemarkFormat()) {
  case RemarkFormat::REMARK_FORMAT_STDOUT:
    if (failed(mlir::remark::enableOptimizationRemarks(
            ctx, nullptr, createPolicy(), cats, true /*printAsEmitRemarks*/)))
      return failure();
    break;

  case RemarkFormat::REMARK_FORMAT_YAML: {
    std::string file = config.getRemarksOutputFile().empty()
                           ? "mlir-remarks.yaml"
                           : config.getRemarksOutputFile();
    if (failed(mlir::remark::enableOptimizationRemarksWithLLVMStreamer(
            ctx, file, llvm::remarks::Format::YAML, createPolicy(), cats)))
      return failure();
    break;
  }

  case RemarkFormat::REMARK_FORMAT_BITSTREAM: {
    std::string file = config.getRemarksOutputFile().empty()
                           ? "mlir-remarks.bitstream"
                           : config.getRemarksOutputFile();
    if (failed(mlir::remark::enableOptimizationRemarksWithLLVMStreamer(
            ctx, file, llvm::remarks::Format::Bitstream, createPolicy(), cats)))
      return failure();
    break;
  }
  }

  // Prepare the pass manager, applying command-line and reproducer options.
  PassManager pm(op.get()->getName(), PassManager::Nesting::Implicit);
  pm.enableVerifier(config.shouldVerifyPasses());
  if (failed(applyPassManagerCLOptions(pm)))
    return failure();
  pm.enableTiming(timing);
  if (config.shouldRunReproducer() && failed(reproOptions.apply(pm)))
    return failure();
  if (failed(config.setupPassPipeline(pm)))
    return failure();

  // Run the pipeline.
  if (failed(pm.run(*op)))
    return failure();

  // Generate reproducers if requested
  if (!config.getReproducerFilename().empty()) {
    StringRef anchorName = pm.getOpAnchorName();
    const auto &passes = pm.getPasses();
    makeReproducer(anchorName, passes, op.get(),
                   config.getReproducerFilename());
  }

  // Print the output.
  TimingScope outputTiming = timing.nest("Output");
  if (config.shouldEmitBytecode()) {
    std::optional<StringRef> producer = config.bytecodeProducerToEmit();
    BytecodeWriterConfig writerConfig =
        producer ? BytecodeWriterConfig(fallbackResourceMap, producer.value())
                 : BytecodeWriterConfig(fallbackResourceMap);
    if (auto v = config.bytecodeVersionToEmit())
      writerConfig.setDesiredBytecodeVersion(*v);
    if (config.shouldElideResourceDataFromBytecode())
      writerConfig.setElideResourceDataFlag();
    return writeBytecodeToFile(op.get(), os, writerConfig);
  }

  if (config.bytecodeVersionToEmit().has_value())
    return emitError(UnknownLoc::get(pm.getContext()))
           << "bytecode version while not emitting bytecode";

  // Don't re-run the verifier if we already ran the verifier at the end of the
  // pass pipeline.
  AsmState asmState(
      op.get(),
      OpPrintingFlags(op.get()->getContext())
          .assumeVerified(config.shouldVerifyPasses() && !pm.empty()),
      /*locationMap=*/nullptr, &fallbackResourceMap);
  os << OpWithState(op.get(), asmState) << '\n';

  // This is required if the remark policy is final. Otherwise, the remarks are
  // not emitted.
  if (remark::detail::RemarkEngine *engine = ctx.getRemarkEngine())
    engine->getRemarkEmittingPolicy()->finalize();

  return success();
}

/// Parses the memory buffer.  If successfully, run a series of passes against
/// it and print the result.
static LogicalResult
processBuffer(raw_ostream &os, std::unique_ptr<MemoryBuffer> ownedBuffer,
              llvm::MemoryBufferRef sourceBuffer,
              const MlirOptMainConfig &config, DialectRegistry &registry,
              SourceMgrDiagnosticVerifierHandler *verifyHandler,
              llvm::ThreadPoolInterface *threadPool) {
  // Tell sourceMgr about this buffer, which is what the parser will pick up.
  auto sourceMgr = std::make_shared<SourceMgr>();
  // Add the original buffer to the source manager to use for determining
  // locations.
  sourceMgr->AddNewSourceBuffer(
      llvm::MemoryBuffer::getMemBuffer(sourceBuffer,
                                       /*RequiresNullTerminator=*/false),
      SMLoc());
  sourceMgr->AddNewSourceBuffer(std::move(ownedBuffer), SMLoc());

  // Create a context just for the current buffer. Disable threading on
  // creation since we'll inject the thread-pool separately.
  MLIRContext context(config.getOptionsContext(), registry,
                      MLIRContext::Threading::DISABLED);
  if (threadPool)
    context.setThreadPool(*threadPool);
  if (verifyHandler)
    verifyHandler->registerInContext(&context);

  StringRef irdlFile = config.getIrdlFile();
  if (!irdlFile.empty() && failed(loadIRDLDialects(irdlFile, context)))
    return failure();

  // Parse the input file.
  context.allowUnregisteredDialects(config.shouldAllowUnregisteredDialects());
  if (config.shouldVerifyDiagnostics())
    context.printOpOnDiagnostic(false);

  tracing::InstallDebugHandler installDebugHandler(context,
                                                   config.getDebugConfig());

  // If we are in verify diagnostics mode then we have a lot of work to do,
  // otherwise just perform the actions without worrying about it.
  if (!config.shouldVerifyDiagnostics()) {
    SourceMgrDiagnosticHandler sourceMgrHandler(*sourceMgr, &context);
    DiagnosticFilter diagnosticFilter(&context,
                                      config.getDiagnosticVerbosityLevel(),
                                      config.shouldShowNotes());
    return performActions(os, sourceMgr, &context, config);
  }

  // Do any processing requested by command line flags.  We don't care whether
  // these actions succeed or fail, we only care what diagnostics they produce
  // and whether they match our expectations.
  (void)performActions(os, sourceMgr, &context, config);

  return success();
}

std::string mlir::registerCLIOptions(llvm::StringRef toolName,
                                     DialectRegistry &registry) {
  MlirOptMainConfig::registerCLOptions(registry);
  registerAsmPrinterCLOptions();
  registerMLIRContextCLOptions();
  registerPassManagerCLOptions();
  registerDefaultTimingManagerCLOptions();
  tracing::DebugCounter::registerCLOptions();

  // Build the list of dialects as a header for the --help message.
  std::string helpHeader = (toolName + "\nAvailable Dialects: ").str();
  {
    llvm::raw_string_ostream os(helpHeader);
    interleaveComma(registry.getRegisteredDialectNames(), os,
                    [&](auto name) { os << name; });
  }
  return helpHeader;
}

static constexpr clv2::OptionInfo<std::string> OI_MlirOptInput{
    "", "<input file>", clv2::Positional{}, clv2::Init{"-"}};
static constexpr clv2::OptionInfo<std::string> OI_MlirOptOutput{
    "o", "Output filename", clv2::value_desc("filename"), clv2::Init{"-"}};
static constexpr clv2::OptionsRegistry<&OI_MlirOptInput, &OI_MlirOptOutput>
    MlirOptIOReg;

mlir::CLIParseResult mlir::parseCLIOptions(int argc, char **argv,
                                           llvm::StringRef helpHeader) {
  llvm::clv2::OptionParser P;
  P.add<&MlirOptIOReg>();
  P.add<&clv2::MLIROptsReg>();
  P.add<&clv2::MlirOptToolOptsReg, applyMlirOptToolOpts>();
  RegisterAllLLVMOptions(P);
  P.enableGlobalDynamicEntries();
  clOptionsConfig->registerDynamicOptions(P);
  registerPassManagerCLOptions(P);
  auto parsedCtx = P.parse(argc, argv, helpHeader);

  // Read straight out of the parse rather than via file-scope state.  parse()
  // returns null when it reported an error, in which case the declared
  // defaults stand.
  std::string inputFilename = "-", outputFilename = "-";
  if (parsedCtx)
    if (const auto *IO = parsedCtx->getViewPtr<&MlirOptIOReg>()) {
      inputFilename = IO->get<&OI_MlirOptInput>();
      outputFilename = IO->get<&OI_MlirOptOutput>();
    }
  return {std::move(inputFilename), std::move(outputFilename),
          std::move(parsedCtx)};
}

mlir::CLIParseResult
mlir::registerAndParseCLIOptions(int argc, char **argv,
                                 llvm::StringRef toolName,
                                 DialectRegistry &registry) {
  auto helpHeader = registerCLIOptions(toolName, registry);
  return parseCLIOptions(argc, argv, helpHeader);
}

static LogicalResult printRegisteredDialects(DialectRegistry &registry) {
  llvm::outs() << "Available Dialects: ";
  interleave(registry.getRegisteredDialectNames(), llvm::outs(), ",");
  llvm::outs() << "\n";
  return success();
}

static LogicalResult printRegisteredPassesAndReturn() {
  mlir::printRegisteredPasses();
  return success();
}

LogicalResult mlir::MlirOptMain(llvm::raw_ostream &outputStream,
                                std::unique_ptr<llvm::MemoryBuffer> buffer,
                                DialectRegistry &registry,
                                const MlirOptMainConfig &config) {
  if (config.shouldShowDialects())
    return printRegisteredDialects(registry);

  if (config.shouldListPasses())
    return printRegisteredPassesAndReturn();

  // The split-input-file mode is a very specific mode that slices the file
  // up into small pieces and checks each independently.
  // We use an explicit threadpool to avoid creating and joining/destroying
  // threads for each of the split.
  ThreadPoolInterface *threadPool = nullptr;

  // Create a temporary context for the sake of checking if
  // --mlir-disable-threading was passed on the command line.
  // We use the thread-pool this context is creating, and avoid
  // creating any thread when disabled.
  MLIRContext threadPoolCtx;
  if (threadPoolCtx.isMultithreadingEnabled())
    threadPool = &threadPoolCtx.getThreadPool();

  SourceMgr sourceMgr;
  sourceMgr.AddNewSourceBuffer(
      llvm::MemoryBuffer::getMemBuffer(buffer->getMemBufferRef(),
                                       /*RequiresNullTerminator=*/false),
      SMLoc());
  // Note: this creates a verifier handler independent of the the flag set, as
  // internally if the flag is not set, a new scoped diagnostic handler is
  // created which would intercept the diagnostics and verify them.
  SourceMgrDiagnosticVerifierHandler sourceMgrHandler(
      sourceMgr, &threadPoolCtx, config.verifyDiagnosticsLevel());
  auto chunkFn = [&](std::unique_ptr<MemoryBuffer> chunkBuffer,
                     llvm::MemoryBufferRef sourceBuffer, raw_ostream &os) {
    return processBuffer(
        os, std::move(chunkBuffer), sourceBuffer, config, registry,
        config.shouldVerifyDiagnostics() ? &sourceMgrHandler : nullptr,
        threadPool);
  };
  LogicalResult status = splitAndProcessBuffer(
      llvm::MemoryBuffer::getMemBuffer(buffer->getMemBufferRef(),
                                       /*RequiresNullTerminator=*/false),
      chunkFn, outputStream, config.inputSplitMarker(),
      config.outputSplitMarker());
  if (config.shouldVerifyDiagnostics() && failed(sourceMgrHandler.verify()))
    status = failure();
  return status;
}

LogicalResult mlir::MlirOptMain(int argc, char **argv,
                                llvm::StringRef inputFilename,
                                llvm::StringRef outputFilename,
                                DialectRegistry &registry,
                                const llvm::clv2::OptionsContext &optsCtx) {

  InitLLVM y(argc, argv);

  MlirOptMainConfig config = MlirOptMainConfig::createFromCLOptions(optsCtx);

  if (config.shouldShowDialects())
    return printRegisteredDialects(registry);

  if (config.shouldListPasses())
    return printRegisteredPassesAndReturn();

  // When reading from stdin and the input is a tty, it is often a user
  // mistake and the process "appears to be stuck". Print a message to let the
  // user know about it!
  if (inputFilename == "-" &&
      sys::Process::FileDescriptorIsDisplayed(fileno(stdin)))
    llvm::errs() << "(processing input from stdin now, hit ctrl-c/ctrl-d to "
                    "interrupt)\n";

  // Set up the input file.
  std::string errorMessage;
  auto file = openInputFile(inputFilename, &errorMessage);
  if (!file) {
    llvm::errs() << errorMessage << "\n";
    return failure();
  }

  auto output = openOutputFile(outputFilename, &errorMessage);
  if (!output) {
    llvm::errs() << errorMessage << "\n";
    return failure();
  }
  if (failed(MlirOptMain(output->os(), std::move(file), registry, config)))
    return failure();

  // Keep the output file if the invocation of MlirOptMain was successful.
  output->keep();
  return success();
}

LogicalResult mlir::MlirOptMain(int argc, char **argv, llvm::StringRef toolName,
                                DialectRegistry &registry) {

  // Register and parse command line options.  The parse result owns this
  // run's OptionsContext, so it has to outlive the call below.
  CLIParseResult parsed =
      registerAndParseCLIOptions(argc, argv, toolName, registry);

  return MlirOptMain(
      argc, argv, parsed.inputFilename, parsed.outputFilename, registry,
      parsed.optsCtx ? *parsed.optsCtx : llvm::clv2::defaultOptionsContext());
}
