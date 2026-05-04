//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Converts CIR directly to LLVM IR, similar to mlir-translate or LLVM llc.
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/DLTI/DLTI.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/OpenMP/OpenMPDialect.h"
#include "mlir/Dialect/OpenMP/Utils/Utils.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/InitAllTranslations.h"
#include "mlir/Support/LogicalResult.h"
#include "mlir/Target/LLVMIR/Dialect/All.h"
#include "mlir/Tools/mlir-translate/MlirTranslateMain.h"
#include "mlir/Tools/mlir-translate/Translation.h"

#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/TargetParser/Host.h"

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CIR/CIRDataLayoutSpec.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/OpenMP/RegisterOpenMPExtensions.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/LowerToLLVM.h"
#include "clang/CIR/MissingFeatures.h"

// cir-translate's main() delegates to mlir::mlirTranslateMain() which owns
// the parse, so we must register options at runtime rather than using
// OptionParser::parse() directly.

inline constexpr llvm::clv2::OptionInfo<std::string> CTTargetOpt{
    "target", "Specify a default target triple when "
              "it's not available in the module"};

inline constexpr llvm::clv2::OptionInfo<bool> CTDisableCCLoweringOpt{
    "disable-cc-lowering", "Disable calling convention lowering pass"};

inline constexpr llvm::clv2::OptionsRegistry<&CTTargetOpt,
                                             &CTDisableCCLoweringOpt>
    CirTranslateReg;

struct CirTranslateOptions {
  std::string TargetTriple;
  bool TargetTripleSet = false;
  bool DisableCCLowering = false;
};

static void
applyCirTranslateOpts(const decltype(CirTranslateReg)::ParsedOptionsT &Parsed,
                      CirTranslateOptions &Opts) {
  Opts.TargetTriple = Parsed.get<&CTTargetOpt>();
  Opts.TargetTripleSet = Parsed.specified<&CTTargetOpt>();
  Opts.DisableCCLowering = Parsed.get<&CTDisableCCLoweringOpt>();
}

// cir-translate's main() delegates the parse to mlir::mlirTranslateMain(), so
// the parsed values must be written into a main()-owned struct via a
// post-parse callback rather than read back directly after P.parse().
static void configureParser(llvm::clv2::OptionParser &P,
                            CirTranslateOptions &Opts) {
  using ParsedT = decltype(CirTranslateReg)::ParsedOptionsT;
  auto *Storage = new ParsedT();
  decltype(CirTranslateReg)::applyDefaultsTo(*Storage);
  std::vector<llvm::clv2::detail::OptionEntry> Entries;
  std::vector<llvm::clv2::detail::AliasEntry> Aliases;
  std::vector<llvm::clv2::detail::SubCommandSpec> SubSpecs;
  decltype(CirTranslateReg)::staticBuildInto(*Storage, Entries, Aliases,
                                             SubSpecs);
  for (auto &E : Entries)
    P.addDynamicEntry(std::move(E));
  llvm::clv2::registerDynamicPostParseCallback(
      [Storage, &Opts]() { applyCirTranslateOpts(*Storage, Opts); });
}

namespace cir {
namespace direct {
extern void registerCIRDialectTranslation(mlir::DialectRegistry &registry);
} // namespace direct

namespace {

/// The goal of this option is to ensure that the triple and data layout specs
/// are always available in the ClangIR module. With this requirement met, the
/// behavior of this option is designed to be as intuitive as possible, as shown
/// in the table below:
///
/// +--------+--------+-------------+-----------------+-----------------------+
/// | Option | Triple | Data Layout | Behavior Triple | Behavior Data Layout  |
/// +========+========+=============+=================+=======================+
/// | T      | T      | T           | Overwrite       | Derive from triple    |
/// | T      | T      | F           | Overwrite       | Derive from triple    |
/// | T      | F      | T           | Overwrite       | Derive from triple    |
/// | T      | F      | F           | Overwrite       | Derive from triple    |
/// | F      | T      | T           |                 |                       |
/// | F      | T      | F           |                 | Derive from triple    |
/// | F      | F      | T           | Set default     | Derive from triple    |
/// | F      | F      | F           | Set default     | Derive from triple    |
/// +--------+--------+-------------+-----------------+-----------------------+

std::string prepareCIRModuleTriple(mlir::ModuleOp mod,
                                   const CirTranslateOptions &Opts) {
  std::string triple = Opts.TargetTriple;

  // Treat "" as the default target machine.
  if (triple.empty()) {
    triple = llvm::sys::getDefaultTargetTriple();

    mod.emitWarning() << "no target triple provided, assuming " << triple;
  }

  mod->setAttr(cir::CIRDialect::getTripleAttrName(),
               mlir::StringAttr::get(mod.getContext(), triple));
  return triple;
}

llvm::LogicalResult prepareCIRModuleDataLayout(mlir::ModuleOp mod,
                                               llvm::StringRef rawTriple) {
  auto *context = mod.getContext();

  // Data layout is fully determined by the target triple. Here we only pass the
  // triple to get the data layout.
  llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs> diagID(
      new clang::DiagnosticIDs);
  clang::DiagnosticOptions diagOpts;
  llvm::IntrusiveRefCntPtr<clang::DiagnosticsEngine> diagnostics =
      new clang::DiagnosticsEngine(diagID, diagOpts,
                                   new clang::IgnoringDiagConsumer());
  llvm::Triple triple(rawTriple);
  // TODO: Need to set various target options later to populate
  // 'TargetInfo' properly.
  clang::TargetOptions targetOptions;
  targetOptions.Triple = rawTriple;
  llvm::IntrusiveRefCntPtr<clang::TargetInfo> targetInfo =
      clang::TargetInfo::CreateTargetInfo(*diagnostics, targetOptions);
  if (!targetInfo) {
    mod.emitError() << "error: invalid target triple '" << rawTriple << "'\n";
    return llvm::failure();
  }
  std::string layoutString = targetInfo->getDataLayoutString();

  // Registered dialects may not be loaded yet, ensure they are.
  context->loadDialect<mlir::DLTIDialect, mlir::LLVM::LLVMDialect,
                       mlir::omp::OpenMPDialect>();

  cir::setMLIRDataLayout(mod, llvm::DataLayout(layoutString));

  return llvm::success();
}

/// Prepare requirements like cir.triple and data layout.
llvm::LogicalResult
prepareCIRModuleForTranslation(mlir::ModuleOp mod,
                               const CirTranslateOptions &Opts) {
  auto modTriple = mod->getAttrOfType<mlir::StringAttr>(
      cir::CIRDialect::getTripleAttrName());
  auto modDataLayout = mod->getAttr(mlir::DLTIDialect::kDataLayoutAttrName);
  bool hasTargetOption = Opts.TargetTripleSet;

  // Skip the situation where nothing should be done.
  if (!hasTargetOption && modTriple && modDataLayout)
    return llvm::success();

  std::string triple;

  if (!hasTargetOption && modTriple) {
    // Do nothing if it's already set.
    triple = modTriple.getValue();
  } else {
    // Otherwise, overwrite or set default.
    triple = prepareCIRModuleTriple(mod, Opts);
  }

  // If the data layout is not set, derive it from the triple.
  return prepareCIRModuleDataLayout(mod, triple);
}
} // namespace
} // namespace cir

void registerToLLVMTranslation(const CirTranslateOptions &Opts) {
  mlir::TranslateFromMLIRRegistration registration(
      "cir-to-llvmir", "Translate CIR to LLVMIR",
      [&Opts](mlir::Operation *op, mlir::raw_ostream &output) {
        auto cirModule = llvm::dyn_cast<mlir::ModuleOp>(op);

        if (mlir::failed(cir::prepareCIRModuleForTranslation(cirModule, Opts)))
          return mlir::failure();

        llvm::LLVMContext llvmContext(llvm::clv2::defaultOptionsContext());
        std::unique_ptr<llvm::Module> llvmModule =
            cir::direct::lowerDirectlyFromCIRToLLVMIR(cirModule, llvmContext,
                                                      /*enableOpenMP=*/false);
        if (!llvmModule)
          return mlir::failure();
        llvmModule->print(output, nullptr);
        return mlir::success();
      },
      [](mlir::DialectRegistry &registry) {
        registry.insert<mlir::DLTIDialect, mlir::func::FuncDialect>();
        mlir::registerAllToLLVMIRTranslations(registry);
        cir::direct::registerCIRDialectTranslation(registry);
        cir::omp::registerOpenMPExtensions(registry);
      });
}

int main(int argc, char **argv) {
  CirTranslateOptions Opts;
  registerToLLVMTranslation(Opts);
  return failed(mlir::mlirTranslateMain(
      argc, argv, "CIR Translation Tool",
      [&Opts](llvm::clv2::OptionParser &P) { configureParser(P, Opts); }));
}
