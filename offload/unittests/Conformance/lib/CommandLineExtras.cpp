//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the definition of the command-line options and the
/// implementation of the logic for selecting test configurations.
///
//===----------------------------------------------------------------------===//

#include "mathtest/CommandLineExtras.hpp"

#include "mathtest/CommandLine.hpp"
#include "mathtest/TestConfig.hpp"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLineV2.h"

using namespace mathtest;

bool mathtest::cl::IsVerbose = false;
TestConfigsArg mathtest::cl::detail::TestConfigsOpt;

static unsigned VerboseCount = 0;
static constexpr llvm::clv2::OptionInfo<bool> OI_Verbose{
    "verbose", "Enable verbose output for failed and unsupported tests",
    llvm::clv2::ValueDisallowed};

// Parses 'all' or a comma-separated provider:platform list into
// TestConfigsOpt.  Returning false marks the value invalid and fails the parse,
// which is why this is a CtxCallback rather than a plain Callback.
static bool parseTestConfigs(void *Ctx, const std::string &Raw) {
  auto &Val = *static_cast<TestConfigsArg *>(Ctx);
  llvm::StringRef ArgValue = llvm::StringRef(Raw).trim();
  if (ArgValue.empty()) {
    llvm::errs() << "Expected 'all|provider:platform[,...]', "
                    "but got an empty string.\n";
    return false;
  }
  if (ArgValue.equals_insensitive("all")) {
    Val.Mode = TestConfigsArg::Mode::All;
    return true;
  }
  llvm::SmallVector<llvm::StringRef, 8> Pairs;
  ArgValue.split(Pairs, ',', /*MaxSplit=*/-1, /*KeepEmpty=*/false);
  Val.Mode = TestConfigsArg::Mode::Explicit;
  Val.Explicit.clear();
  for (llvm::StringRef Pair : Pairs) {
    llvm::SmallVector<llvm::StringRef, 2> Parts;
    Pair.split(Parts, ':');
    if (Parts.size() != 2) {
      llvm::errs() << "Expected '<provider>:<platform>', got '" << Pair
                   << "'\n";
      return false;
    }
    llvm::StringRef Provider = Parts[0].trim();
    llvm::StringRef Platform = Parts[1].trim();
    if (Provider.empty() || Platform.empty()) {
      llvm::errs() << "Provider and platform must not be empty in '" << Pair
                   << "'\n";
      return false;
    }
    TestConfig Config = {Provider.str(), Platform.str()};
    const auto &All = getAllTestConfigs();
    if (!llvm::is_contained(All, Config)) {
      llvm::errs() << "Invalid pair '" << Pair << "'\n";
      return false;
    }
    Val.Explicit.push_back(Config);
  }
  return true;
}

static std::string TestConfigsRaw;
static unsigned TestConfigsCount = 0;
static constexpr llvm::clv2::OptionInfo<std::string> OI_TestConfigs{
    "test-configs", "Select test configurations",
    llvm::clv2::CtxCallback<std::string>{
        &parseTestConfigs,
        static_cast<void *>(&mathtest::cl::detail::TestConfigsOpt)}};

static const int MathTestOptsInit = ([] {
  using namespace llvm::clv2;
  registerDynamicEntry(clv2::makeEntry<&OI_Verbose>(mathtest::cl::IsVerbose, VerboseCount));
  registerDynamicEntry(clv2::makeEntry<&OI_TestConfigs>(TestConfigsRaw,
                                                TestConfigsCount));
}(), 0);

const llvm::SmallVector<TestConfig, 4> &mathtest::cl::getTestConfigs() {
  switch (detail::TestConfigsOpt.Mode) {
  case TestConfigsArg::Mode::Default:
    return getDefaultTestConfigs();
  case TestConfigsArg::Mode::All:
    return getAllTestConfigs();
  case TestConfigsArg::Mode::Explicit:
    return detail::TestConfigsOpt.Explicit;
  }
  llvm_unreachable("Unknown TestConfigsArg mode");
}
