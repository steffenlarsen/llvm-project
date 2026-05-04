//===- PassManagerOptions.cpp - PassManager Command Line Options ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/MLIROptionsOptInfos.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Support/Timing.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/ManagedStatic.h"

using namespace mlir;
using namespace llvm::clv2;

namespace {
struct PassManagerCLOptions {
  PassNameCLParser printBefore{"mlir-print-ir-before",
                               "Print IR before specified passes"};
  PassNameCLParser printAfter{"mlir-print-ir-after",
                              "Print IR after specified passes"};

  void addPrinterInstrumentation(PassManager &pm,
                                 const mlir_opts::MLIROptsRegOpts *O);
};
} // namespace

static llvm::ManagedStatic<PassManagerCLOptions> clParserOptions;

void PassManagerCLOptions::addPrinterInstrumentation(
    PassManager &pm, const mlir_opts::MLIROptsRegOpts *O) {
  std::function<bool(Pass *, Operation *)> shouldPrintBeforePass;
  std::function<bool(Pass *, Operation *)> shouldPrintAfterPass;

  if (O && O->get<&MLIR_PrintIRBeforeAll>()) {
    shouldPrintBeforePass = [](Pass *, Operation *) { return true; };
  } else if (printBefore.hasAnyOccurrences()) {
    shouldPrintBeforePass = [&](Pass *pass, Operation *) {
      auto *passInfo = pass->lookupPassInfo();
      return passInfo && printBefore.contains(passInfo);
    };
  }

  if (O && (O->get<&MLIR_PrintIRAfterAll>() ||
            O->get<&MLIR_PrintIRAfterFailure>())) {
    shouldPrintAfterPass = [](Pass *, Operation *) { return true; };
  } else if (printAfter.hasAnyOccurrences()) {
    shouldPrintAfterPass = [&](Pass *pass, Operation *) {
      auto *passInfo = pass->lookupPassInfo();
      return passInfo && printAfter.contains(passInfo);
    };
  }

  if (!shouldPrintBeforePass && !shouldPrintAfterPass)
    return;

  std::string treeDir = O ? std::string(O->get<&MLIR_PrintIRTreeDir>()) : "";
  bool moduleScope = O ? O->get<&MLIR_PrintIRModuleScope>() : false;
  bool afterChange = O ? O->get<&MLIR_PrintIRAfterChange>() : false;
  bool afterFailure = O ? O->get<&MLIR_PrintIRAfterFailure>() : false;

  if (!treeDir.empty()) {
    pm.enableIRPrintingToFileTree(
        shouldPrintBeforePass, shouldPrintAfterPass, moduleScope, afterChange,
        afterFailure, std::string(treeDir), OpPrintingFlags(pm.getContext()));
    return;
  }

  pm.enableIRPrinting(shouldPrintBeforePass, shouldPrintAfterPass, moduleScope,
                      afterChange, afterFailure, llvm::errs(),
                      OpPrintingFlags(pm.getContext()));
}

void mlir::registerPassManagerCLOptions() { *clParserOptions; }

void mlir::registerPassManagerCLOptions(llvm::clv2::OptionParser &P) {
  *clParserOptions;
  clParserOptions->printBefore.registerWith(P);
  clParserOptions->printAfter.registerWith(P);
}

LogicalResult mlir::applyPassManagerCLOptions(PassManager &pm) {
  if (!clParserOptions.isConstructed())
    return failure();
  auto *O = mlir_opts::getMLIROptsReg(pm.getContext()->getOptionsContext());

  if (O) {
    bool reproducerFileSet = O->specified<&MLIR_PassCrashReproducer>();
    if (reproducerFileSet && O->get<&MLIR_PassLocalReproducer>() &&
        pm.getContext()->isMultithreadingEnabled()) {
      emitError(UnknownLoc::get(pm.getContext()))
          << "Local crash reproduction may not be used without disabling "
             "mutli-threading first.";
      return failure();
    }

    if (reproducerFileSet)
      pm.enableCrashReproducerGeneration(
          std::string(O->get<&MLIR_PassCrashReproducer>()),
          O->get<&MLIR_PassLocalReproducer>());

    if (O->get<&MLIR_PassStatistics>()) {
      auto mode =
          static_cast<PassDisplayMode>(O->get<&MLIR_PassStatisticsDisplay>());
      pm.enableStatistics(mode);
    }

    if (O->get<&MLIR_PrintIRModuleScope>() &&
        pm.getContext()->isMultithreadingEnabled()) {
      emitError(UnknownLoc::get(pm.getContext()))
          << "IR print for module scope can't be setup on a pass-manager "
             "without disabling multi-threading first.\n";
      return failure();
    }

    clParserOptions->addPrinterInstrumentation(pm, O);
  } else {
    clParserOptions->addPrinterInstrumentation(pm, nullptr);
  }
  return success();
}

void mlir::applyDefaultTimingPassManagerCLOptions(PassManager &pm) {
  // Create a temporary timing manager for the PM to own, apply its CL options,
  // and pass it to the PM.
  auto tm = std::make_unique<DefaultTimingManager>();
  applyDefaultTimingManagerCLOptions(*tm, pm.getContext()->getOptionsContext());
  pm.enableTiming(std::move(tm));
}
