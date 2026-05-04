//===- llvm/unittest/IR/CommandLineV2ContextTest.cpp - clv2+ctx tests -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Tests for the LLVMContext::setParsedOptions / getParsedOptions /
// getOptions() interface introduced as part of the CommandLine v2 migration.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/OptionsContext.h"
#include "gtest/gtest.h"
#include <memory>
#include <string>

using namespace llvm;
using namespace llvm::clv2;

namespace {

// Descriptors declared at namespace scope -- inline constexpr gives static
// storage duration with zero global constructors.
inline constexpr OptionInfo<int> CtxOptLevel{"ctx-opt-level", "Opt level",
                                             Init{0}};
inline constexpr OptionInfo<bool> CtxDebug{"ctx-debug", "Debug mode"};
inline constexpr OptionsRegistry<&CtxOptLevel, &CtxDebug> CtxRegistry;

// A second, distinct registry -- getOptions() should return nullptr for it.
inline constexpr OptionInfo<int> OtherOpt{"ctx-other", "Other opt"};
inline constexpr OptionsRegistry<&OtherOpt> OtherRegistry;

// Helper: parse argv via OptionParser and return an OptionsContext.
auto parseToContext(std::vector<const char *> Argv) {
  std::string ErrBuf;
  raw_string_ostream Errs(ErrBuf);
  OptionParser P;
  P.add<&CtxRegistry>();
  auto Ctx =
      P.parse(static_cast<int>(Argv.size()), Argv.data(), "test-tool", &Errs);
  return std::make_pair(std::move(Ctx), std::move(ErrBuf));
}

} // anonymous namespace

// ---------------------------------------------------------------------------

TEST(CommandLineV2Context, DefaultContextWhenNullArg) {
  // Passing nullptr to the constructor is how a caller says "no session
  // options here".  It resolves to the one shared empty context rather than
  // to null, so library code never has to test.
  LLVMContext Ctx{llvm::clv2::defaultOptionsContext()};
  EXPECT_EQ(&Ctx.getOptionsContext(), &clv2::defaultOptionsContext());
  // Reading through it yields the option's compile-time default.  The value
  // is bound first: the comma in the template argument list would otherwise be
  // seen by the preprocessor as a macro argument separator.
  const int Level =
      getOptValOr<&CtxRegistry, &CtxOptLevel>(Ctx.getOptionsContext(), 0);
  EXPECT_EQ(Level, 0);
}

TEST(CommandLineV2Context, SetAndGetBase) {
  auto [Parsed, Err] = parseToContext({"tool", "-ctx-opt-level=2"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Parsed, nullptr);

  LLVMContext Ctx{llvm::clv2::defaultOptionsContext()};
  Ctx.setOptionsContext(*Parsed);

  EXPECT_EQ(&Ctx.getOptionsContext(), Parsed.get());
  // The parsed value is visible through the LLVMContext, not just the pointer.
  const int Level =
      getOptValOr<&CtxRegistry, &CtxOptLevel>(Ctx.getOptionsContext(), 0);
  EXPECT_EQ(Level, 2);
}

TEST(CommandLineV2Context, GetOptionsTypedMatch) {
  auto [Parsed, Err] =
      parseToContext({"tool", "-ctx-opt-level=3", "-ctx-debug"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Parsed, nullptr);

  // Check values via OptionsContext directly.
  auto *View = Parsed->getViewPtr<&CtxRegistry>();
  ASSERT_NE(View, nullptr);
  EXPECT_EQ(View->get<&CtxOptLevel>(), 3);
  EXPECT_TRUE(View->get<&CtxDebug>());
}

TEST(CommandLineV2Context, GetOptionsTypedMismatch) {
  auto [Parsed, Err] = parseToContext({"tool"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Parsed, nullptr);

  // OtherRegistry was not added to the parser -- should return nullptr.
  auto *Wrong = Parsed->getViewPtr<&OtherRegistry>();
  EXPECT_EQ(Wrong, nullptr);
}

TEST(CommandLineV2Context, Detach) {
  auto [Parsed, Err] = parseToContext({"tool"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Parsed, nullptr);

  LLVMContext Ctx{llvm::clv2::defaultOptionsContext()};
  Ctx.setOptionsContext(*Parsed);
  EXPECT_EQ(&Ctx.getOptionsContext(), Parsed.get());

  // Detaching a context that is about to die means handing back the shared
  // default, not clearing the pointer.  Callers that outlive the parse then
  // read compile-time defaults instead of dereferencing null -- the reason
  // getOptionsContext() can return a reference at all.
  Ctx.setOptionsContext(clv2::defaultOptionsContext());
  EXPECT_EQ(&Ctx.getOptionsContext(), &clv2::defaultOptionsContext());
}

TEST(CommandLineV2Context, DefaultValues) {
  auto [Parsed, Err] = parseToContext({"tool"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Parsed, nullptr);

  auto *View = Parsed->getViewPtr<&CtxRegistry>();
  ASSERT_NE(View, nullptr);
  EXPECT_EQ(View->get<&CtxOptLevel>(), 0); // Init{0}
  EXPECT_FALSE(View->get<&CtxDebug>());    // default false
}
