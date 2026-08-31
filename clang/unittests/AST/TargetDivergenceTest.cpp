//===- unittests/AST/TargetDivergenceTest.cpp -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// PROTOTYPE (Stage 2): locks in what TargetDivergence reports for the target
// pairs the combined-frontend design is sized against. The whole design rests
// on divergence being rare -- if these expectations ever change, the per-target
// caching in Stage 1 is protecting the wrong set of entities.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/TargetDivergence.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"
#include <memory>

using namespace clang;

namespace {

class TargetDivergenceTest : public ::testing::Test {
protected:
  DiagnosticOptions DiagOpts;
  IntrusiveRefCntPtr<DiagnosticsEngine> Diags;
  std::vector<std::shared_ptr<TargetOptions>> Opts;

  void SetUp() override {
    Diags = new DiagnosticsEngine(new DiagnosticIDs, DiagOpts);
  }

  /// Nothing here needs a full CompilerInstance; TargetInfo is constructible
  /// from a triple alone.
  const TargetInfo *make(StringRef Triple) {
    auto TO = std::make_shared<TargetOptions>();
    TO->Triple = Triple.str();
    Opts.push_back(TO);
    Targets.emplace_back(TargetInfo::CreateTargetInfo(*Diags, *TO));
    return Targets.back().get();
  }

  std::string summarize(ArrayRef<const TargetInfo *> Ts) {
    std::string S;
    llvm::raw_string_ostream OS(S);
    TargetDivergence(Ts).print(OS);
    return S;
  }

  std::vector<std::unique_ptr<TargetInfo>> Targets;
};

/// The pair the design is sized against. Exactly one primitive type differs;
/// everything else is shared, which is what keeps the multi-target path cheap.
TEST_F(TargetDivergenceTest, HostVsAMDGPUDivergesOnlyInLongDouble) {
  const TargetInfo *Host = make("x86_64-unknown-linux-gnu");
  const TargetInfo *Device = make("amdgcn-amd-amdhsa");
  ASSERT_TRUE(Host && Device);

  const TargetInfo *Ts[] = {Host, Device};
  TargetDivergence D(Ts);
  EXPECT_TRUE(D.any());
  EXPECT_TRUE(D.isDivergent(BuiltinType::LongDouble));

  // Spot-check the types HIP code actually computes sizes of all day.
  EXPECT_FALSE(D.isDivergent(BuiltinType::Bool));
  EXPECT_FALSE(D.isDivergent(BuiltinType::Char_S));
  EXPECT_FALSE(D.isDivergent(BuiltinType::Short));
  EXPECT_FALSE(D.isDivergent(BuiltinType::Int));
  EXPECT_FALSE(D.isDivergent(BuiltinType::Long));
  EXPECT_FALSE(D.isDivergent(BuiltinType::LongLong));
  EXPECT_FALSE(D.isDivergent(BuiltinType::Half));
  EXPECT_FALSE(D.isDivergent(BuiltinType::Float));
  EXPECT_FALSE(D.isDivergent(BuiltinType::Double));
}

/// A target cannot diverge from itself, and one target cannot diverge at all.
/// Both must report nothing, or single-target compilation would start paying
/// for multi-target support.
TEST_F(TargetDivergenceTest, IdenticalOrSingleTargetHasNoDivergence) {
  const TargetInfo *A = make("x86_64-unknown-linux-gnu");
  const TargetInfo *B = make("x86_64-unknown-linux-gnu");
  ASSERT_TRUE(A && B);

  const TargetInfo *Pair[] = {A, B};
  EXPECT_FALSE(TargetDivergence(Pair).any());

  const TargetInfo *Single[] = {A};
  EXPECT_FALSE(TargetDivergence(Single).any());

  EXPECT_FALSE(TargetDivergence({}).any());
}

/// Two unrelated CPU targets happen to agree on every primitive layout. This
/// is the case that shows divergence tracks real disagreement rather than
/// merely "the triples differ".
TEST_F(TargetDivergenceTest, DistinctCPUTargetsCanStillAgree) {
  const TargetInfo *X86 = make("x86_64-unknown-linux-gnu");
  const TargetInfo *AArch64 = make("aarch64-unknown-linux-gnu");
  ASSERT_TRUE(X86 && AArch64);

  const TargetInfo *Ts[] = {X86, AArch64};
  EXPECT_FALSE(TargetDivergence(Ts).any());
}

/// print() is what -Rtarget-divergence and header authors will read, so its
/// two shapes -- something differs, nothing differs -- are part of the API.
TEST_F(TargetDivergenceTest, PrintNamesTheDivergentTypes) {
  const TargetInfo *Host = make("x86_64-unknown-linux-gnu");
  const TargetInfo *Device = make("amdgcn-amd-amdhsa");
  ASSERT_TRUE(Host && Device);

  const TargetInfo *Diverging[] = {Host, Device};
  EXPECT_NE(summarize(Diverging).find("long double"), std::string::npos);

  const TargetInfo *Agreeing[] = {Host, Host};
  EXPECT_EQ(summarize(Agreeing).find("long double"), std::string::npos);
}

} // namespace
