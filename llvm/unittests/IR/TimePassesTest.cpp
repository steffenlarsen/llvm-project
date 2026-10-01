//===- unittests/IR/TimePassesTest.cpp - TimePassesHandler tests ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/PassTimingInfo.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include <gtest/gtest.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassInstrumentation.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/PassTimingInfo.h>
#include <llvm/Support/raw_ostream.h>
#include <thread>

using namespace llvm;

//===----------------------------------------------------------------------===//
// Define dummy passes for legacy pass manager run.

namespace llvm {

void initializePass1Pass(PassRegistry &);
void initializePass2Pass(PassRegistry &);

} // namespace llvm

namespace {
struct Pass1 : public ModulePass {
  static char ID;

public:
  Pass1() : ModulePass(ID) {}
  bool runOnModule(Module &M) override { return false; }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
  StringRef getPassName() const override { return "Pass1"; }
};
char Pass1::ID;

struct Pass2 : public ModulePass {
  static char ID;

public:
  Pass2() : ModulePass(ID) {}
  bool runOnModule(Module &M) override { return false; }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
  StringRef getPassName() const override { return "Pass2"; }
};
char Pass2::ID;
} // namespace

INITIALIZE_PASS(Pass1, "Pass1", "Pass1", false, false)
INITIALIZE_PASS(Pass2, "Pass2", "Pass2", false, false)

namespace {

TEST(TimePassesTest, LegacyCustomOut) {
  PassInstrumentationCallbacks PIC;
  PassInstrumentation PI(&PIC);

  PassTimingState TimingState;
  LLVMContext Context;
  Context.setPassTimingState(&TimingState);
  Module M("TestModule", Context);

  SmallString<0> TimePassesStr;
  raw_svector_ostream ReportStream(TimePassesStr);

  // Setup pass manager
  legacy::PassManager PM1;
  PM1.add(new Pass1());
  PM1.add(new Pass2());

  // Run passes on the timed context.
  PM1.run(M);

  // Generating report.
  TimingState.reportAndResetTimings(&ReportStream);

  // There should be Pass1 and Pass2 in the report
  EXPECT_FALSE(TimePassesStr.empty());
  EXPECT_TRUE(TimePassesStr.str().contains("report"));
  EXPECT_TRUE(TimePassesStr.str().contains("Pass1"));
  EXPECT_TRUE(TimePassesStr.str().contains("Pass2"));

  // Clear and generate report again.
  TimePassesStr.clear();
  TimingState.reportAndResetTimings(&ReportStream);

  // Since we did not run any passes since last print, report should be empty.
  EXPECT_TRUE(TimePassesStr.empty());

  // Now run just a single pass to populate timers again.
  legacy::PassManager PM2;
  PM2.add(new Pass2());
  PM2.run(M);

  // Generate report again.
  TimingState.reportAndResetTimings(&ReportStream);

  // There should be Pass2 in this report and no Pass1.
  EXPECT_FALSE(TimePassesStr.str().empty());
  EXPECT_TRUE(TimePassesStr.str().contains("report"));
  EXPECT_FALSE(TimePassesStr.str().contains("Pass1"));
  EXPECT_TRUE(TimePassesStr.str().contains("Pass2"));
}

static std::string runLegacyPassAndReport(PassTimingState &TimingState,
                                          LLVMContext &Context, Pass *P) {
  Module M("TestModule", Context);
  legacy::PassManager PM;
  PM.add(P);
  PM.run(M);

  SmallString<0> Report;
  raw_svector_ostream ReportStream(Report);
  TimingState.reportAndResetTimings(&ReportStream);
  return Report.str().str();
}

// Each context's passes are timed by its own state.
TEST(TimePassesTest, LegacyIsolation) {
  PassTimingState StateA, StateB;
  LLVMContext ContextA, ContextB;
  ContextA.setPassTimingState(&StateA);
  ContextB.setPassTimingState(&StateB);

  std::string ReportA = runLegacyPassAndReport(StateA, ContextA, new Pass1());
  std::string ReportB = runLegacyPassAndReport(StateB, ContextB, new Pass2());
  EXPECT_TRUE(StringRef(ReportA).contains("Pass1"));
  EXPECT_FALSE(StringRef(ReportA).contains("Pass2"));
  EXPECT_TRUE(StringRef(ReportB).contains("Pass2"));
  EXPECT_FALSE(StringRef(ReportB).contains("Pass1"));

  // Passes run on a context without a state are not timed, even after passes
  // were timed on another context.
  LLVMContext Untimed;
  Module M("TestModule", Untimed);
  legacy::PassManager PM;
  PM.add(new Pass1());
  PM.run(M);
  SmallString<0> Report;
  raw_svector_ostream ReportStream(Report);
  StateA.reportAndResetTimings(&ReportStream);
  EXPECT_TRUE(Report.empty());
}

// Compilations on different threads can be timed at the same time, each
// into its own state, even when they time regions with the same names.
TEST(TimePassesTest, Concurrent) {
  auto Compile = [](Pass *(*CreatePass)(), std::string &Report) {
    PassTimingState TimingState;
    LLVMContext Context;
    Context.setPassTimingState(&TimingState);
    for (int I = 0; I < 100; ++I) {
      Module M("TestModule", Context);
      legacy::PassManager PM;
      PM.add(CreatePass());
      PM.run(M);
      for (int J = 0; J < 100; ++J)
        NamedRegionTimer T("region", "Region", "group", "Group",
                           Context.getPassTimingState());
    }
    raw_string_ostream ReportStream(Report);
    TimingState.print(ReportStream);
  };

  std::string ReportA, ReportB;
  std::thread ThreadA(
      Compile, []() -> Pass * { return new Pass1(); }, std::ref(ReportA));
  std::thread ThreadB(
      Compile, []() -> Pass * { return new Pass2(); }, std::ref(ReportB));
  ThreadA.join();
  ThreadB.join();

  EXPECT_TRUE(StringRef(ReportA).contains("Pass1"));
  EXPECT_FALSE(StringRef(ReportA).contains("Pass2"));
  EXPECT_TRUE(StringRef(ReportA).contains("Region"));
  EXPECT_TRUE(StringRef(ReportB).contains("Pass2"));
  EXPECT_FALSE(StringRef(ReportB).contains("Pass1"));
  EXPECT_TRUE(StringRef(ReportB).contains("Region"));
}

class MyPass1 : public OptionalPassInfoMixin<MyPass1> {};
class MyPass2 : public OptionalPassInfoMixin<MyPass2> {};

TEST(TimePassesTest, CustomOut) {
  PassInstrumentationCallbacks PIC;
  PassInstrumentation PI(&PIC);

  LLVMContext Context;
  Module M("TestModule", Context);
  MyPass1 Pass1;
  MyPass2 Pass2;

  SmallString<0> TimePassesStr;
  raw_svector_ostream ReportStream(TimePassesStr);

  // Setup time-passes handler and redirect output to the stream.
  PassTimingState TimingState;
  std::unique_ptr<TimePassesHandler> TimePasses =
      std::make_unique<TimePassesHandler>(&TimingState);
  TimePasses->setOutStream(ReportStream);
  TimePasses->registerCallbacks(PIC);

  // Pretending that passes are running to trigger the timers.
  PI.runBeforePass(Pass1, M);
  PI.runAfterPass(Pass1, M, PreservedAnalyses::all());
  PI.runBeforePass(Pass2, M);
  PI.runAfterPass(Pass2, M, PreservedAnalyses::all());

  // Generating report.
  TimePasses->print();

  // There should be Pass1 and Pass2 in the report
  EXPECT_FALSE(TimePassesStr.empty());
  EXPECT_TRUE(TimePassesStr.str().contains("report"));
  EXPECT_TRUE(TimePassesStr.str().contains("Pass1"));
  EXPECT_TRUE(TimePassesStr.str().contains("Pass2"));

  // Clear and generate report again.
  TimePassesStr.clear();
  TimePasses->print();
  // Since we did not run any passes since last print, report should be empty.
  EXPECT_TRUE(TimePassesStr.empty());

  // Now trigger just a single pass to populate timers again.
  PI.runBeforePass(Pass2, M);
  PI.runAfterPass(Pass2, M, PreservedAnalyses::all());

  // Clear and generate report again.
  TimePasses->print();

  // There should be Pass2 in this report and no Pass1.
  EXPECT_FALSE(TimePassesStr.str().empty());
  EXPECT_TRUE(TimePassesStr.str().contains("report"));
  EXPECT_FALSE(TimePassesStr.str().contains("Pass1"));
  EXPECT_TRUE(TimePassesStr.str().contains("Pass2"));
}

} // end anonymous namespace
