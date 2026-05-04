//===- RegAllocPriorityAdvisor.cpp - live ranges priority advisor ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of the default priority advisor and of the Analysis pass.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/RegAllocPriorityAdvisor.h"
#include "RegAllocGreedy.h"
#include "llvm/CodeGen/CodeGenPassOptionsOptInfos.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/OptionsContext.h"

using namespace llvm;

static RegAllocPriorityAdvisorProvider::AdvisorMode Mode =
    RegAllocPriorityAdvisorProvider::AdvisorMode::Default;

static RegAllocPriorityAdvisorProvider::AdvisorMode
getPriorityAdvisorMode(const clv2::OptionsContext &Ctx) {
  return static_cast<RegAllocPriorityAdvisorProvider::AdvisorMode>(
      clv2::getOptValOr<&clv2::CGPassRegAllocReg,
                        &clv2::CGPASS_RegallocEnablePriorityAdvisor>(Ctx,
                                                                     Mode));
}

char RegAllocPriorityAdvisorAnalysisLegacy::ID = 0;
INITIALIZE_PASS(RegAllocPriorityAdvisorAnalysisLegacy, "regalloc-priority",
                "Regalloc priority policy", false, true)

namespace {

class DefaultPriorityAdvisorProvider final
    : public RegAllocPriorityAdvisorProvider {
public:
  DefaultPriorityAdvisorProvider(bool NotAsRequested, LLVMContext &Ctx)
      : RegAllocPriorityAdvisorProvider(AdvisorMode::Default) {
    if (NotAsRequested)
      Ctx.emitError("Requested regalloc priority advisor analysis "
                    "could be created. Using default");
  }

  // support for isa<> and dyn_cast.
  static bool classof(const RegAllocPriorityAdvisorProvider *R) {
    return R->getAdvisorMode() == AdvisorMode::Default;
  }

  std::unique_ptr<RegAllocPriorityAdvisor>
  getAdvisor(const MachineFunction &MF, const RAGreedy &RA,
             SlotIndexes &SI) override {
    return std::make_unique<DefaultPriorityAdvisor>(MF, RA, &SI);
  }
};

class DummyPriorityAdvisorProvider final
    : public RegAllocPriorityAdvisorProvider {
public:
  DummyPriorityAdvisorProvider()
      : RegAllocPriorityAdvisorProvider(AdvisorMode::Dummy) {}

  static bool classof(const RegAllocPriorityAdvisorProvider *R) {
    return R->getAdvisorMode() == AdvisorMode::Dummy;
  }

  std::unique_ptr<RegAllocPriorityAdvisor>
  getAdvisor(const MachineFunction &MF, const RAGreedy &RA,
             SlotIndexes &SI) override {
    return std::make_unique<DummyPriorityAdvisor>(MF, RA, &SI);
  }
};

class DefaultPriorityAdvisorAnalysisLegacy final
    : public RegAllocPriorityAdvisorAnalysisLegacy {
public:
  DefaultPriorityAdvisorAnalysisLegacy(bool NotAsRequested)
      : RegAllocPriorityAdvisorAnalysisLegacy(AdvisorMode::Default),
        NotAsRequested(NotAsRequested) {}

  // support for isa<> and dyn_cast.
  static bool classof(const RegAllocPriorityAdvisorAnalysisLegacy *R) {
    return R->getAdvisorMode() == AdvisorMode::Default;
  }

private:
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<SlotIndexesWrapperPass>();
    RegAllocPriorityAdvisorAnalysisLegacy::getAnalysisUsage(AU);
  }

  bool doInitialization(Module &M) override {
    Provider.reset(
        new DefaultPriorityAdvisorProvider(NotAsRequested, M.getContext()));
    return false;
  }

  const bool NotAsRequested;
};

class DummyPriorityAdvisorAnalysis final
    : public RegAllocPriorityAdvisorAnalysisLegacy {
public:
  using RegAllocPriorityAdvisorAnalysisLegacy::AdvisorMode;
  DummyPriorityAdvisorAnalysis()
      : RegAllocPriorityAdvisorAnalysisLegacy(AdvisorMode::Dummy) {}

  // support for isa<> and dyn_cast.
  static bool classof(const RegAllocPriorityAdvisorAnalysisLegacy *R) {
    return R->getAdvisorMode() == AdvisorMode::Dummy;
  }

private:
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<SlotIndexesWrapperPass>();
    RegAllocPriorityAdvisorAnalysisLegacy::getAnalysisUsage(AU);
  }

  bool doInitialization(Module &M) override {
    Provider.reset(new DummyPriorityAdvisorProvider());
    return false;
  }
};

/// Deferred priority advisor analysis that reads the mode from the Module's
/// LLVMContext OptionsContext in doInitialization, rather than at
/// construction time when no context is available.
class DeferredPriorityAdvisorAnalysisLegacy final
    : public RegAllocPriorityAdvisorAnalysisLegacy {
public:
  using RegAllocPriorityAdvisorAnalysisLegacy::AdvisorMode;
  DeferredPriorityAdvisorAnalysisLegacy()
      : RegAllocPriorityAdvisorAnalysisLegacy(AdvisorMode::Default) {}

  static bool classof(const RegAllocPriorityAdvisorAnalysisLegacy *R) {
    return true;
  }

private:
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<SlotIndexesWrapperPass>();
    RegAllocPriorityAdvisorAnalysisLegacy::getAnalysisUsage(AU);
  }

  bool doInitialization(Module &M) override {
    auto &Ctx = M.getContext();
    AdvisorMode Mode = static_cast<AdvisorMode>(
        static_cast<int>(getPriorityAdvisorMode(Ctx.getOptionsContext())));
    switch (Mode) {
    case AdvisorMode::Default:
      Provider.reset(
          new DefaultPriorityAdvisorProvider(/*NotAsRequested=*/false, Ctx));
      break;
    case AdvisorMode::Dummy:
      Provider.reset(new DummyPriorityAdvisorProvider());
      break;
    case AdvisorMode::Development:
#if defined(LLVM_HAVE_TFLITE)
      Provider.reset(createDevelopmentModePriorityAdvisorProvider(Ctx));
#else
      Provider.reset(
          new DefaultPriorityAdvisorProvider(/*NotAsRequested=*/true, Ctx));
#endif
      break;
    case AdvisorMode::Release: {
      auto *Ret = createReleaseModePriorityAdvisorProvider();
      if (Ret)
        Provider.reset(Ret);
      else
        Provider.reset(
            new DefaultPriorityAdvisorProvider(/*NotAsRequested=*/true, Ctx));
      break;
    }
    }
    return false;
  }
};

} // namespace

void RegAllocPriorityAdvisorAnalysis::initializeProvider(LLVMContext &Ctx) {
  if (Provider)
    return;
  switch (getPriorityAdvisorMode(Ctx.getOptionsContext())) {
  case RegAllocPriorityAdvisorProvider::AdvisorMode::Dummy:
    Provider.reset(new DummyPriorityAdvisorProvider());
    return;
  case RegAllocPriorityAdvisorProvider::AdvisorMode::Default:
    Provider.reset(
        new DefaultPriorityAdvisorProvider(/*NotAsRequested=*/false, Ctx));
    return;
  case RegAllocPriorityAdvisorProvider::AdvisorMode::Development:
#if defined(LLVM_HAVE_TFLITE)
    Provider.reset(createDevelopmentModePriorityAdvisorProvider(Ctx));
#else
    Provider.reset(
        new DefaultPriorityAdvisorProvider(/*NotAsRequested=*/true, Ctx));
#endif
    return;
  case RegAllocPriorityAdvisorProvider::AdvisorMode::Release: {
    auto *Ret = createReleaseModePriorityAdvisorProvider();
    if (Ret)
      Provider.reset(Ret);
    else
      Provider.reset(
          new DefaultPriorityAdvisorProvider(/*NotAsRequested=*/true, Ctx));
    return;
  }
  }
}

AnalysisKey RegAllocPriorityAdvisorAnalysis::Key;

RegAllocPriorityAdvisorAnalysis::Result
RegAllocPriorityAdvisorAnalysis::run(MachineFunction &MF,
                                     MachineFunctionAnalysisManager &MFAM) {
  // Lazily initialize the provider.
  initializeProvider(MF.getFunction().getContext());
  // The requiring analysis will construct the advisor.
  return Result{Provider.get()};
}

template <>
Pass *llvm::callDefaultCtor<RegAllocPriorityAdvisorAnalysisLegacy>() {
  // Defer mode selection to doInitialization where the Module's
  // LLVMContext OptionsContext is available for reading CLI options.
  return new DeferredPriorityAdvisorAnalysisLegacy();
}

StringRef RegAllocPriorityAdvisorAnalysisLegacy::getPassName() const {
  switch (getAdvisorMode()) {
  case AdvisorMode::Default:
    return "Default Regalloc Priority Advisor";
  case AdvisorMode::Release:
    return "Release mode Regalloc Priority Advisor";
  case AdvisorMode::Development:
    return "Development mode Regalloc Priority Advisor";
  case AdvisorMode::Dummy:
    return "Dummy Regalloc Priority Advisor";
  }
  llvm_unreachable("Unknown advisor kind");
}

RegAllocPriorityAdvisor::RegAllocPriorityAdvisor(const MachineFunction &MF,
                                                 const RAGreedy &RA,
                                                 SlotIndexes *const Indexes)
    : RA(RA), LIS(RA.getLiveIntervals()), VRM(RA.getVirtRegMap()),
      MRI(&VRM->getRegInfo()), TRI(MF.getSubtarget().getRegisterInfo()),
      RegClassInfo(RA.getRegClassInfo()), Indexes(Indexes),
      RegClassPriorityTrumpsGlobalness(
          RA.getRegClassPriorityTrumpsGlobalness()),
      ReverseLocalAssignment(RA.getReverseLocalAssignment()) {}
