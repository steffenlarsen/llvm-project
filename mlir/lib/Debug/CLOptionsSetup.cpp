//===- CLOptionsSetup.cpp - Helpers to setup debug CL options ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Debug/CLOptionsSetup.h"

#include "mlir/Debug/Counter.h"
#include "mlir/Debug/DebuggerExecutionContextHook.h"
#include "mlir/Debug/ExecutionContext.h"
#include "mlir/Debug/Observers/ActionLogging.h"
#include "mlir/Debug/Observers/ActionProfiler.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/MLIROptionsOptInfos.h"
#include "mlir/Support/FileUtilities.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/ToolOutputFile.h"

using namespace mlir;
using namespace mlir::tracing;
using namespace llvm;

namespace {
struct DebugConfigCLOptions : public DebugConfig {
  DebugConfigCLOptions() { locFilterRegistered = false; }

  void applyLocationFilter(StringRef location) {
    if (!locFilterRegistered) {
      addLogActionLocFilter(&locBreakpointManager);
      locFilterRegistered = true;
    }
    locationStrings.push_back(location.str());
    StringRef locStr = locationStrings.back();

    auto diag = [](Twine msg) { llvm::errs() << msg << "\n"; };
    auto locBreakpoint =
        tracing::FileLineColLocBreakpoint::parseFromString(locStr, diag);
    if (failed(locBreakpoint)) {
      llvm::errs() << "Invalid location filter: " << locStr << "\n";
      exit(1);
    }
    auto [file, line, col] = *locBreakpoint;
    locBreakpointManager.addBreakpoint(file, line, col);
  }

  tracing::FileLineColLocBreakpointManager locBreakpointManager;
  std::vector<std::string> locationStrings;
  bool locFilterRegistered = false;
};

} // namespace

static ManagedStatic<DebugConfigCLOptions> clOptionsConfig;

void DebugConfig::registerCLOptions() { *clOptionsConfig; }

DebugConfig
DebugConfig::createFromCLOptions(const llvm::clv2::OptionsContext &optsCtx) {
  using namespace llvm::clv2;
  auto *O = mlir::mlir_opts::getMLIROptsReg(optsCtx);
  if (!O)
    return *clOptionsConfig;

  auto logTo = O->get<&MLIR_LogActionsTo>();
  if (!logTo.empty())
    clOptionsConfig->logActionsTo(logTo);
  auto profileTo = O->get<&MLIR_ProfileActionsTo>();
  if (!profileTo.empty())
    clOptionsConfig->profileActionsTo(profileTo);
  for (const auto &loc : O->get<&MLIR_LogActionsFilter>())
    clOptionsConfig->applyLocationFilter(loc);
  if (O->get<&MLIR_EnableDebuggerHook>())
    clOptionsConfig->enableDebuggerActionHook(true);

  return *clOptionsConfig;
}

class InstallDebugHandler::Impl {
public:
  Impl(MLIRContext &context, const DebugConfig &config) {
    if (config.getLogActionsTo().empty() &&
        config.getProfileActionsTo().empty() &&
        !config.isDebuggerActionHookEnabled()) {
      if (tracing::DebugCounter::isActivated(context.getOptionsContext()))
        context.registerActionHandler(
            tracing::DebugCounter(context.getOptionsContext()));
      return;
    }
    errs() << "ExecutionContext registered on the context";
    if (tracing::DebugCounter::isActivated(context.getOptionsContext()))
      emitError(UnknownLoc::get(&context),
                "Debug counters are incompatible with --log-actions-to and "
                "--mlir-enable-debugger-hook options and are disabled");
    if (!config.getLogActionsTo().empty()) {
      std::string errorMessage;
      logActionsFile = openOutputFile(config.getLogActionsTo(), &errorMessage);
      if (!logActionsFile) {
        emitError(UnknownLoc::get(&context),
                  "Opening file for --log-actions-to failed: ")
            << errorMessage << "\n";
        return;
      }
      logActionsFile->keep();
      raw_fd_ostream &logActionsStream = logActionsFile->os();
      actionLogger = std::make_unique<tracing::ActionLogger>(logActionsStream);
      for (const auto *locationBreakpoint : config.getLogActionsLocFilters())
        actionLogger->addBreakpointManager(locationBreakpoint);
      executionContext.registerObserver(actionLogger.get());
    }

    if (!config.getProfileActionsTo().empty()) {
      std::string errorMessage;
      profileActionsFile =
          openOutputFile(config.getProfileActionsTo(), &errorMessage);
      if (!profileActionsFile) {
        emitError(UnknownLoc::get(&context),
                  "Opening file for --profile-actions-to failed: ")
            << errorMessage << "\n";
        return;
      }
      profileActionsFile->keep();
      raw_fd_ostream &profileActionsStream = profileActionsFile->os();
      actionProfiler =
          std::make_unique<tracing::ActionProfiler>(profileActionsStream);
      executionContext.registerObserver(actionProfiler.get());
    }

    if (config.isDebuggerActionHookEnabled()) {
      errs() << " (with Debugger hook)";
      setupDebuggerExecutionContextHook(executionContext);
    }
    errs() << "\n";
    context.registerActionHandler(executionContext);
  }

private:
  std::unique_ptr<ToolOutputFile> logActionsFile;
  tracing::ExecutionContext executionContext;
  std::unique_ptr<tracing::ActionLogger> actionLogger;
  std::vector<std::unique_ptr<tracing::FileLineColLocBreakpoint>>
      locationBreakpoints;
  std::unique_ptr<ToolOutputFile> profileActionsFile;
  std::unique_ptr<tracing::ActionProfiler> actionProfiler;
};

InstallDebugHandler::InstallDebugHandler(MLIRContext &context,
                                         const DebugConfig &config)
    : impl(std::make_unique<Impl>(context, config)) {}

InstallDebugHandler::~InstallDebugHandler() = default;
