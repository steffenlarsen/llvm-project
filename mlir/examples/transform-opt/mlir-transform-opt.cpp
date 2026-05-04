//===- mlir-transform-opt.cpp -----------------------------------*- C++ -*-===//
//
// This file is licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/Transform/IR/TransformDialect.h"
#include "mlir/Dialect/Transform/IR/Utils.h"
#include "mlir/Dialect/Transform/Transforms/TransformInterpreterUtils.h"
#include "mlir/IR/AsmState.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/InitAllDialects.h"
#include "mlir/InitAllExtensions.h"
#include "mlir/InitAllPasses.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Support/FileUtilities.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/ToolOutputFile.h"
#include <cstdlib>

using namespace llvm;

namespace {

/// Enum mirroring mlir::SourceMgrDiagnosticVerifierHandler::Level for
/// constexpr option description.
enum class VerifyDiagLevel : int {
  All = static_cast<int>(mlir::SourceMgrDiagnosticVerifierHandler::Level::All),
  OnlyExpected = static_cast<int>(
      mlir::SourceMgrDiagnosticVerifierHandler::Level::OnlyExpected),
};

} // namespace

static constexpr clv2::OptionInfo<bool> allowUnregisteredDialectOpt{
    "allow-unregistered-dialect",
    "Allow operations coming from an unregistered dialect"};

static constexpr clv2::EnumVal<VerifyDiagLevel> verifyDiagVals[] = {
    {"all", VerifyDiagLevel::All,
     "Check all diagnostics (expected, unexpected, near-misses)"},
    {"", VerifyDiagLevel::All,
     "Check all diagnostics (expected, unexpected, near-misses)"},
    {"only-expected", VerifyDiagLevel::OnlyExpected,
     "Check only expected diagnostics"},
};
static constexpr auto verifyDiagnosticsOpt =
    clv2::makeEnumOption<VerifyDiagLevel>(
        "verify-diagnostics",
        "Check that emitted diagnostics match expected-* lines on the "
        "corresponding line",
        verifyDiagVals, clv2::ValueOptional);

static constexpr clv2::OptionInfo<std::string> payloadFilenameOpt{
    "", "<input file>", clv2::Positional{}, clv2::Init{"-"}};

static constexpr clv2::OptionInfo<std::string> outputFilenameOpt{
    "o", "Output filename", clv2::Init{"-"}};

static constexpr clv2::OptionInfo<std::string> transformMainFilenameOpt{
    "transform",
    "File containing entry point of the transform script, if "
    "different from the input file",
    clv2::value_desc("filename")};

static constexpr clv2::ListOptionInfo<std::string> transformLibraryFilenamesOpt{
    "transform-library",
    "File(s) containing definitions of "
    "additional transform script symbols",
    clv2::ZeroOrMore};

static constexpr clv2::OptionInfo<std::string> transformEntryPointOpt{
    "transform-entry-point", "Name of the entry point transform symbol"};

static constexpr clv2::OptionInfo<bool> disableExpensiveChecksOpt{
    "disable-expensive-checks",
    "Disables potentially expensive checks in the transform "
    "interpreter, providing more speed at the expense of "
    "potential memory problems and silent corruptions"};

static constexpr clv2::OptionInfo<bool> dumpLibraryModuleOpt{
    "dump-library-module",
    "Prints the combined library module before the output"};

static constexpr clv2::OptionsRegistry<
    &allowUnregisteredDialectOpt, &verifyDiagnosticsOpt, &payloadFilenameOpt,
    &outputFilenameOpt, &transformMainFilenameOpt,
    &transformLibraryFilenamesOpt, &transformEntryPointOpt,
    &disableExpensiveChecksOpt, &dumpLibraryModuleOpt>
    TransformOptReg;

namespace {

/// Structure containing command line options for the tool, populated
/// from parsed options in runMain().
struct MlirTransformOptCLOptions {
  bool allowUnregisteredDialects = false;
  mlir::SourceMgrDiagnosticVerifierHandler::Level verifyDiagnosticsValue =
      mlir::SourceMgrDiagnosticVerifierHandler::Level::All;
  bool verifyDiagnosticsWasSet = false;
  std::string payloadFilename = "-";
  std::string outputFilename = "-";
  std::string transformMainFilename;
  std::vector<std::string> transformLibraryFilenames;
  std::string transformEntryPoint =
      mlir::transform::TransformDialect::kTransformEntryPointSymbolName.str();
  bool disableExpensiveChecks = false;
  bool dumpLibraryModule = false;
};
} // namespace

/// "Managed" static instance of the command-line options structure.
static llvm::ManagedStatic<MlirTransformOptCLOptions> clOptions;

namespace {
/// A wrapper class for source managers diagnostic. This provides both unique
/// ownership and virtual function-like overload for a pair of
/// inheritance-related classes that do not use virtual functions.
class DiagnosticHandlerWrapper {
public:
  /// Kind of the diagnostic handler to use.
  enum class Kind { EmitDiagnostics, VerifyDiagnostics };

  /// Constructs the diagnostic handler of the specified kind of the given
  /// source manager and context.
  DiagnosticHandlerWrapper(
      Kind kind, llvm::SourceMgr &mgr, mlir::MLIRContext *context,
      std::optional<mlir::SourceMgrDiagnosticVerifierHandler::Level> level =
          {}) {
    if (kind == Kind::EmitDiagnostics) {
      handler = new mlir::SourceMgrDiagnosticHandler(mgr, context);
    } else {
      assert(level.has_value() && "expected level");
      handler =
          new mlir::SourceMgrDiagnosticVerifierHandler(mgr, context, *level);
    }
  }

  /// This object is non-copyable but movable.
  DiagnosticHandlerWrapper(const DiagnosticHandlerWrapper &) = delete;
  DiagnosticHandlerWrapper(DiagnosticHandlerWrapper &&other) = default;
  DiagnosticHandlerWrapper &
  operator=(const DiagnosticHandlerWrapper &) = delete;
  DiagnosticHandlerWrapper &operator=(DiagnosticHandlerWrapper &&) = default;

  /// Verifies the captured "expected-*" diagnostics if required.
  llvm::LogicalResult verify() const {
    if (auto *ptr =
            dyn_cast<mlir::SourceMgrDiagnosticVerifierHandler *>(handler)) {
      return ptr->verify();
    }
    return mlir::success();
  }

  /// Destructs the object of the same type as allocated.
  ~DiagnosticHandlerWrapper() {
    if (auto *ptr = dyn_cast<mlir::SourceMgrDiagnosticHandler *>(handler)) {
      delete ptr;
    } else {
      delete cast<mlir::SourceMgrDiagnosticVerifierHandler *>(handler);
    }
  }

private:
  /// Internal storage is a type-safe union.
  llvm::PointerUnion<mlir::SourceMgrDiagnosticHandler *,
                     mlir::SourceMgrDiagnosticVerifierHandler *>
      handler;
};

/// MLIR has deeply rooted expectations that the LLVM source manager contains
/// exactly one buffer, until at least the lexer level. This class wraps
/// multiple LLVM source managers each managing a buffer to match MLIR's
/// expectations while still providing a centralized handling mechanism.
class TransformSourceMgr {
public:
  /// Constructs the source manager indicating whether diagnostic messages will
  /// be verified later on.
  explicit TransformSourceMgr(
      std::optional<mlir::SourceMgrDiagnosticVerifierHandler::Level>
          verifyDiagnostics)
      : verifyDiagnostics(verifyDiagnostics) {}

  /// Deconstructs the source manager. Note that `checkResults` must have been
  /// called on this instance before deconstructing it.
  ~TransformSourceMgr() {
    assert(resultChecked && "must check the result of diagnostic handlers by "
                            "running TransformSourceMgr::checkResult");
  }

  /// Parses the given buffer and creates the top-level operation of the kind
  /// specified as template argument in the given context. Additional parsing
  /// options may be provided.
  template <typename OpTy = mlir::Operation *>
  mlir::OwningOpRef<OpTy> parseBuffer(std::unique_ptr<MemoryBuffer> buffer,
                                      mlir::MLIRContext &context,
                                      const mlir::ParserConfig &config) {
    // Create a single-buffer LLVM source manager. Note that `unique_ptr` allows
    // the code below to capture a reference to the source manager in such a way
    // that it is not invalidated when the vector contents is eventually
    // reallocated.
    llvm::SourceMgr &mgr =
        *sourceMgrs.emplace_back(std::make_unique<llvm::SourceMgr>());
    mgr.AddNewSourceBuffer(std::move(buffer), llvm::SMLoc());

    // Choose the type of diagnostic handler depending on whether diagnostic
    // verification needs to happen and store it.
    if (verifyDiagnostics) {
      diagHandlers.emplace_back(
          DiagnosticHandlerWrapper::Kind::VerifyDiagnostics, mgr, &context,
          verifyDiagnostics);
    } else {
      diagHandlers.emplace_back(DiagnosticHandlerWrapper::Kind::EmitDiagnostics,
                                mgr, &context);
    }

    // Defer to MLIR's parser.
    return mlir::parseSourceFile<OpTy>(mgr, config);
  }

  /// If diagnostic message verification has been requested upon construction of
  /// this source manager, performs the verification, reports errors and returns
  /// the result of the verification. Otherwise passes through the given value.
  llvm::LogicalResult checkResult(llvm::LogicalResult result) {
    resultChecked = true;
    if (!verifyDiagnostics)
      return result;

    return mlir::failure(llvm::any_of(diagHandlers, [](const auto &handler) {
      return mlir::failed(handler.verify());
    }));
  }

private:
  /// Indicates whether diagnostic message verification is requested.
  const std::optional<mlir::SourceMgrDiagnosticVerifierHandler::Level>
      verifyDiagnostics;

  /// Indicates that diagnostic message verification has taken place, and the
  /// deconstruction is therefore safe.
  bool resultChecked = false;

  /// Storage for per-buffer source managers and diagnostic handlers. These are
  /// wrapped into unique pointers in order to make it safe to capture
  /// references to these objects: if the vector is reallocated, the unique
  /// pointer objects are moved by the pointer addresses won't change. Also, for
  /// handlers, this allows to store the pointer to the base class.
  SmallVector<std::unique_ptr<llvm::SourceMgr>> sourceMgrs;
  SmallVector<DiagnosticHandlerWrapper> diagHandlers;
};
} // namespace

/// Trivial wrapper around `applyTransforms` that doesn't support extra mapping
/// and doesn't enforce the entry point transform ops being top-level.
static llvm::LogicalResult
applyTransforms(mlir::Operation *payloadRoot,
                mlir::transform::TransformOpInterface transformRoot,
                const mlir::transform::TransformOptions &options) {
  return applyTransforms(payloadRoot, transformRoot, {}, options,
                         /*enforceToplevelTransformOp=*/false);
}

/// Applies transforms indicated in the transform dialect script to the input
/// buffer. The transform script may be embedded in the input buffer or as a
/// separate buffer. The transform script may have external symbols, the
/// definitions of which must be provided in transform library buffers. If the
/// application is successful, prints the transformed input buffer into the
/// given output stream. Additional configuration options are derived from
/// command-line options.
static llvm::LogicalResult processPayloadBuffer(
    raw_ostream &os, std::unique_ptr<MemoryBuffer> inputBuffer,
    std::unique_ptr<llvm::MemoryBuffer> transformBuffer,
    MutableArrayRef<std::unique_ptr<MemoryBuffer>> transformLibraries,
    mlir::DialectRegistry &registry) {

  // Initialize the MLIR context, and various configurations.
  mlir::MLIRContext context(registry, mlir::MLIRContext::Threading::DISABLED);
  context.allowUnregisteredDialects(clOptions->allowUnregisteredDialects);
  mlir::ParserConfig config(&context);
  TransformSourceMgr sourceMgr(
      /*verifyDiagnostics=*/clOptions->verifyDiagnosticsWasSet
          ? std::optional{clOptions->verifyDiagnosticsValue}
          : std::nullopt);

  // Parse the input buffer that will be used as transform payload.
  mlir::OwningOpRef<mlir::Operation *> payloadRoot =
      sourceMgr.parseBuffer(std::move(inputBuffer), context, config);
  if (!payloadRoot)
    return sourceMgr.checkResult(mlir::failure());

  // Identify the module containing the transform script entry point. This may
  // be the same module as the input or a separate module. In the former case,
  // make a copy of the module so it can be modified freely. Modification may
  // happen in the script itself (at which point it could be rewriting itself
  // during interpretation, leading to tricky memory errors) or by embedding
  // library modules in the script.
  mlir::OwningOpRef<mlir::ModuleOp> transformRoot;
  if (transformBuffer) {
    transformRoot = sourceMgr.parseBuffer<mlir::ModuleOp>(
        std::move(transformBuffer), context, config);
    if (!transformRoot)
      return sourceMgr.checkResult(mlir::failure());
  } else {
    transformRoot = cast<mlir::ModuleOp>(payloadRoot->clone());
  }

  // Parse and merge the libraries into the main transform module.
  for (auto &&transformLibrary : transformLibraries) {
    mlir::OwningOpRef<mlir::ModuleOp> libraryModule =
        sourceMgr.parseBuffer<mlir::ModuleOp>(std::move(transformLibrary),
                                              context, config);

    if (!libraryModule ||
        mlir::failed(mlir::transform::detail::mergeSymbolsInto(
            *transformRoot, std::move(libraryModule))))
      return sourceMgr.checkResult(mlir::failure());
  }

  // If requested, dump the combined transform module.
  if (clOptions->dumpLibraryModule)
    transformRoot->dump();

  // Find the entry point symbol. Even if it had originally been in the payload
  // module, it was cloned into the transform module so only look there.
  mlir::transform::TransformOpInterface entryPoint =
      mlir::transform::detail::findTransformEntryPoint(
          *transformRoot, mlir::ModuleOp(), clOptions->transformEntryPoint);
  if (!entryPoint)
    return sourceMgr.checkResult(mlir::failure());

  // Apply the requested transformations.
  mlir::transform::TransformOptions transformOptions;
  transformOptions.enableExpensiveChecks(!clOptions->disableExpensiveChecks);
  if (mlir::failed(applyTransforms(*payloadRoot, entryPoint, transformOptions)))
    return sourceMgr.checkResult(mlir::failure());

  // Print the transformed result and check the captured diagnostics if
  // requested.
  payloadRoot->print(os);
  return sourceMgr.checkResult(mlir::success());
}

/// Tool entry point.
static llvm::LogicalResult runMain(int argc, char **argv) {
  // Register all upstream dialects and extensions. Specific uses are advised
  // not to register all dialects indiscriminately but rather hand-pick what is
  // necessary for their use case.
  mlir::DialectRegistry registry;
  mlir::registerAllDialects(registry);
  mlir::registerAllExtensions(registry);
  mlir::registerAllPasses();

  // Explicitly register the transform dialect. This is not strictly necessary
  // since it has been already registered as part of the upstream dialect list,
  // but useful for example purposes for cases when dialects to register are
  // hand-picked. The transform dialect must be registered.
  registry.insert<mlir::transform::TransformDialect>();

  // Register various command-line options. Note that the LLVM initializer
  // object is a RAII that ensures correct deconstruction of command-line option
  // objects inside ManagedStatic.
  llvm::InitLLVM y(argc, argv);
  mlir::registerAsmPrinterCLOptions();
  mlir::registerMLIRContextCLOptions();
  llvm::clv2::OptionParser P;
  P.add<&TransformOptReg>();
  RegisterAllLLVMOptions(P);
  auto OptsCtx = P.parse(argc, argv, "Minimal Transform dialect driver\n");
  auto *Opts = OptsCtx->getViewPtr<&TransformOptReg>();

  // Populate the CLOptions struct from parsed options.
  *clOptions;
  clOptions->allowUnregisteredDialects =
      Opts->get<&allowUnregisteredDialectOpt>();
  clOptions->verifyDiagnosticsValue =
      static_cast<mlir::SourceMgrDiagnosticVerifierHandler::Level>(
          Opts->get<&verifyDiagnosticsOpt>());
  clOptions->verifyDiagnosticsWasSet = Opts->specified<&verifyDiagnosticsOpt>();
  clOptions->payloadFilename = std::string(Opts->get<&payloadFilenameOpt>());
  clOptions->outputFilename = std::string(Opts->get<&outputFilenameOpt>());
  clOptions->transformMainFilename =
      std::string(Opts->get<&transformMainFilenameOpt>());
  clOptions->transformLibraryFilenames =
      Opts->get<&transformLibraryFilenamesOpt>();
  {
    auto entryPoint = std::string(Opts->get<&transformEntryPointOpt>());
    if (!entryPoint.empty())
      clOptions->transformEntryPoint = std::move(entryPoint);
  }
  clOptions->disableExpensiveChecks = Opts->get<&disableExpensiveChecksOpt>();
  clOptions->dumpLibraryModule = Opts->get<&dumpLibraryModuleOpt>();

  // Try opening the main input file.
  std::string errorMessage;
  std::unique_ptr<llvm::MemoryBuffer> payloadFile =
      mlir::openInputFile(clOptions->payloadFilename, &errorMessage);
  if (!payloadFile) {
    llvm::errs() << errorMessage << "\n";
    return mlir::failure();
  }

  // Try opening the output file.
  std::unique_ptr<llvm::ToolOutputFile> outputFile =
      mlir::openOutputFile(clOptions->outputFilename, &errorMessage);
  if (!outputFile) {
    llvm::errs() << errorMessage << "\n";
    return mlir::failure();
  }

  // Try opening the main transform file if provided.
  std::unique_ptr<llvm::MemoryBuffer> transformRootFile;
  if (!clOptions->transformMainFilename.empty()) {
    if (clOptions->transformMainFilename == clOptions->payloadFilename) {
      llvm::errs() << "warning: " << clOptions->payloadFilename
                   << " is provided as both payload and transform file\n";
    } else {
      transformRootFile =
          mlir::openInputFile(clOptions->transformMainFilename, &errorMessage);
      if (!transformRootFile) {
        llvm::errs() << errorMessage << "\n";
        return mlir::failure();
      }
    }
  }

  // Try opening transform library files if provided.
  SmallVector<std::unique_ptr<llvm::MemoryBuffer>> transformLibraries;
  transformLibraries.reserve(clOptions->transformLibraryFilenames.size());
  for (llvm::StringRef filename : clOptions->transformLibraryFilenames) {
    transformLibraries.emplace_back(
        mlir::openInputFile(filename, &errorMessage));
    if (!transformLibraries.back()) {
      llvm::errs() << errorMessage << "\n";
      return mlir::failure();
    }
  }

  return processPayloadBuffer(outputFile->os(), std::move(payloadFile),
                              std::move(transformRootFile), transformLibraries,
                              registry);
}

int main(int argc, char **argv) {
  return mlir::asMainReturnCode(runMain(argc, argv));
}
