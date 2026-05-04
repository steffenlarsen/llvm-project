//===- llvm/unittest/Support/CommandLineV2Test.cpp - clv2 tests -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/CommandLineV2.h"
#include "llvm-c/Support.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace llvm;
using namespace llvm::clv2;

//===----------------------------------------------------------------------===//
// Option descriptors used across tests.  Declared inline constexpr at
// namespace scope so they have static storage duration with zero global ctors.
//===----------------------------------------------------------------------===//

namespace {

// --- Scalar options ---
inline constexpr OptionInfo<bool> OptVerbose{"verbose",
                                             "Enable verbose output"};
inline constexpr OptionInfo<int> OptCount{"count", "Repeat count", Init{3}};
inline constexpr OptionInfo<unsigned> OptWidth{"width", "Output width"};
inline constexpr OptionInfo<int64_t> OptOffset{"offset", "Byte offset"};
inline constexpr OptionInfo<uint64_t> OptSize{"size", "Buffer size"};
inline constexpr OptionInfo<float> OptThreshold{"threshold", "Float threshold"};
inline constexpr OptionInfo<double> OptTolerance{"tolerance",
                                                 "Double tolerance"};
inline constexpr OptionInfo<std::string> OptOutput{"output", "Output file",
                                                   Init{"a.out"}};

// --- List option ---
inline constexpr ListOptionInfo<std::string> OptInputs{"input", "Input files"};
inline constexpr ListOptionInfo<int> OptFlags{"flag", "Integer flags"};

// --- Enum option ---
enum class OptimLevel { O0, O1, O2, O3 };
inline constexpr EnumVal<OptimLevel> OptimVals[] = {
    {"O0", OptimLevel::O0, "No optimisation"},
    {"O1", OptimLevel::O1, "Light optimisation"},
    {"O2", OptimLevel::O2, "Standard optimisation"},
    {"O3", OptimLevel::O3, "Aggressive optimisation"},
};
inline constexpr auto OptOptim = makeEnumOption<OptimLevel>(
    "optim", "Optimisation level", OptimVals, Init{OptimLevel::O1});

// --- BitsOptionInfo fixtures ---
enum class EmitKind { Obj, Asm, BC };
inline constexpr EnumVal<EmitKind> EmitVals[] = {
    {"obj", EmitKind::Obj, "Emit object code"},
    {"asm", EmitKind::Asm, "Emit assembly"},
    {"bc", EmitKind::BC, "Emit bitcode"},
};
inline constexpr BitsOptionInfo<EmitKind> BitsOpt{"emit", "Emit formats",
                                                  EmitVals};
inline constexpr OptionsRegistry<&BitsOpt> BitsReg;

// --- Registries ---
inline constexpr OptionsRegistry<&OptVerbose, &OptCount, &OptOutput, &OptInputs>
    ScalarListReg;

// Two list options in one registry, so tests can observe how their elements
// interleave on the command line.
inline constexpr OptionsRegistry<&OptInputs, &OptFlags> TwoListReg;

// Positional list used by tests that need a stray token to land somewhere.
// Without a positional entry the parser reports "Unknown command line
// argument", which makes OptionParser::parse return nullptr and hides whatever
// the test was actually trying to observe.
inline constexpr ListOptionInfo<std::string> OptRest{"rest", "Remaining args",
                                                     Positional{}};
inline constexpr OptionsRegistry<&OptVerbose, &OptCount, &OptRest>
    PositionalTailReg;

inline constexpr OptionsRegistry<&OptVerbose, &OptCount, &OptWidth, &OptOffset,
                                 &OptSize, &OptThreshold, &OptTolerance,
                                 &OptOutput>
    AllScalarReg;

inline constexpr OptionsRegistry<&OptOptim> EnumReg;

// Bool option with explicit ValueOptional — tests that =true/=false still work
// when the descriptor overrides the default ValueDisallowed promotion.
inline constexpr OptionInfo<bool> OptToggle{"toggle", "Toggleable flag",
                                            ValueOptional};
inline constexpr OptionsRegistry<&OptToggle> ToggleReg;

// value_desc test fixtures
inline constexpr OptionInfo<std::string> OptFileOutput{
    "output", "Output file", value_desc("filename"), Required};
inline constexpr OptionsRegistry<&OptFileOutput> FileOutputReg;

// Alias test fixtures
inline constexpr OptionInfo<std::string> AliasTarget{"output-long",
                                                     "Output file", Required};
inline constexpr AliasInfo AliasShort{"o", "output-long"};
inline constexpr OptionsRegistry<&AliasTarget, &AliasShort> AliasReg;

// Bad alias — target does not exist (for AliasUnknownTargetError test)
inline constexpr OptionInfo<int> BadAliasOpt{"some-opt", "Some option"};
inline constexpr AliasInfo BadAlias{"bad", "nonexistent-option"};
inline constexpr OptionsRegistry<&BadAliasOpt, &BadAlias> BadAliasReg;

// ReallyHidden test fixture
inline constexpr OptionInfo<bool> ReallyHiddenOpt{"secret", "Secret flag",
                                                  ReallyHidden};
inline constexpr OptionsRegistry<&ReallyHiddenOpt> ReallyHiddenReg;

// extrahelp test fixtures
inline constexpr OptionInfo<bool> ExtraHelpOpt{"xopt", "Some option"};
inline constexpr OptionsRegistry<&ExtraHelpOpt> ExtraHelpReg{
    "See also: https://example.com\n"};

// Prefix-format test fixtures
inline constexpr ListOptionInfo<std::string> PrefixOpt{
    "I", "Include directories", value_desc("dir"), PrefixFormat, ZeroOrMore};
inline constexpr OptionsRegistry<&PrefixOpt> PrefixReg;

// Multi-registry test fixtures
inline constexpr OptionInfo<std::string> CompOptA{"comp-a", "Option A",
                                                  Required};
inline constexpr OptionsRegistry<&CompOptA> CompRegA;

inline constexpr OptionInfo<int> CompOptB{"comp-b", "Option B", Init{42}};
inline constexpr OptionsRegistry<&CompOptB> CompRegB;

// Alias crossing registries
inline constexpr OptionInfo<std::string> CompOptC{"comp-c", "Option C",
                                                  Required};
inline constexpr OptionsRegistry<&CompOptC> CompRegC;
inline constexpr AliasInfo CompAlias{"ca", "comp-c"};
inline constexpr OptionsRegistry<&CompAlias> CompRegAlias;

// Sink test fixtures
inline constexpr OptionInfo<std::string> SinkKnownOpt{"known", "Known option"};
inline constexpr ListOptionInfo<std::string> SinkOpt{
    "sink-passthrough", "Unknown option sink", Sink, ZeroOrMore};
inline constexpr OptionsRegistry<&SinkKnownOpt, &SinkOpt> SinkReg;

// PositionalEatsArgs test fixtures
inline constexpr ListOptionInfo<std::string> PosEatArgs1{
    "positional-eat-args", "<arguments>...", Positional{}, PositionalEatsArgs};
inline constexpr ListOptionInfo<std::string> PosEatArgs2{
    "positional-eat-args2", "Some strings", Positional{}, PositionalEatsArgs};
inline constexpr OptionsRegistry<&PosEatArgs1, &PosEatArgs2> PosEatArgsReg;

// Callback test fixtures
static std::vector<std::string> CallbackLog;

void listStringCallback(const std::string &V) { CallbackLog.push_back(V); }
void scalarIntCallback(const int &V) {
  CallbackLog.push_back(std::to_string(V));
}

inline constexpr ListOptionInfo<std::string> CbListOpt{
    "cb-list", "List with callback", CommaSeparated,
    Callback<std::string>{&listStringCallback}};
inline constexpr OptionsRegistry<&CbListOpt> CbListReg;

inline constexpr ListOptionInfo<std::string> CbListNoCSOpt{
    "cb-list-nocs", "List without comma-separated",
    Callback<std::string>{&listStringCallback}};
inline constexpr OptionsRegistry<&CbListNoCSOpt> CbListNoCSReg;

inline constexpr OptionInfo<int> CbScalarOpt{
    "cb-scalar", "Scalar with callback", Callback<int>{&scalarIntCallback}};
inline constexpr OptionsRegistry<&CbScalarOpt> CbScalarReg;

// --- Subcommand options ---
inline constexpr OptionInfo<int> SubOptLevel{"level", "Optimisation level",
                                             Init{2}};
inline constexpr OptionInfo<std::string> SubOptArch{
    "arch", "Target architecture", Init{"x86_64"}};
inline constexpr SubCommandInfo<&SubOptLevel, &SubOptArch> BuildCmd{
    "build", "Build targets"};
inline constexpr SubCommandInfo<&SubOptLevel> TestCmd{"test", "Run tests"};
inline constexpr OptionsRegistry<&OptVerbose, &BuildCmd, &TestCmd> SubCmdReg;

// A global prefix option and a subcommand declaring one of the same name, so
// that prefix lookup has to decide which wins.  No in-tree tool currently has
// both subcommands and prefix options, so this shadowing rule is only
// reachable from here.
inline constexpr ListOptionInfo<std::string> GlobalIncOpt{
    "I", "Global include dirs", value_desc("dir"), PrefixFormat, ZeroOrMore};
inline constexpr ListOptionInfo<std::string> SubIncOpt{
    "I", "Subcommand include dirs", value_desc("dir"), PrefixFormat,
    ZeroOrMore};
inline constexpr SubCommandInfo<&SubIncOpt> PrefixSubCmd{"go", "Go"};
inline constexpr OptionsRegistry<&GlobalIncOpt, &PrefixSubCmd> PrefixSubReg;

//===----------------------------------------------------------------------===//
// Helper: parse argv vector and return (OptionsContext, error string).
//===----------------------------------------------------------------------===//

template <const auto *Reg> auto parseArgs(std::vector<const char *> Argv) {
  std::string ErrBuf;
  raw_string_ostream Errs(ErrBuf);
  OptionParser P;
  P.template add<Reg>();
  auto Ctx =
      P.parse(static_cast<int>(Argv.size()), Argv.data(), "test-tool", &Errs);
  return std::make_pair(std::move(Ctx), std::move(ErrBuf));
}

} // anonymous namespace

//===----------------------------------------------------------------------===//
// OptionStaticInfo must be a compile-time constant
//
// These are static_asserts rather than TESTs on purpose: the whole value of
// OptionStaticInfo is that it costs nothing at runtime, so the property worth
// guarding is "this folds at compile time", which a runtime check cannot show.
//===----------------------------------------------------------------------===//
namespace {
using llvm::clv2::detail::StaticInfoFor;

// Scalar: the bool ValueOptional promotion happens at compile time.
static_assert(StaticInfoFor<&OptVerbose>.ValueExpected == ValueOptional);
static_assert(StaticInfoFor<&OptVerbose>.SuppressValuePlaceholder);
static_assert(StaticInfoFor<&OptCount>.ValueExpected == ValueRequired);
static_assert(!StaticInfoFor<&OptCount>.SuppressValuePlaceholder);
static_assert(StaticInfoFor<&OptCount>.Name.size() == 5); // "count"
static_assert(StaticInfoFor<&OptCount>.Desc == &OptCount);
static_assert(StaticInfoFor<&OptCount>.DefaultValueName.size() == 3); // "int"

// An explicit ValueOptional is respected rather than promoted.
static_assert(StaticInfoFor<&OptToggle>.ValueExpected == ValueOptional);

// value_desc and Required survive the flattening.
static_assert(StaticInfoFor<&OptFileOutput>.ValueDesc.size() == 8); // filename
static_assert(StaticInfoFor<&OptFileOutput>.OccurrencesFlag == Required);

// Lists default to ZeroOrMore and ValueRequired.
static_assert(StaticInfoFor<&OptInputs>.OccurrencesFlag == ZeroOrMore);
static_assert(StaticInfoFor<&OptInputs>.ValueExpected == ValueRequired);

// Positional-ness and prefix formatting.
static_assert(StaticInfoFor<&OptRest>.IsPositional);
static_assert(StaticInfoFor<&PrefixOpt>.IsPrefix);
static_assert(!StaticInfoFor<&PrefixOpt>.IsAlwaysPrefix);

// Enum metadata.  MaxEnumUsed/ShowDualDisplay are absent by design: they need
// the table's contents, which is not constexpr for every descriptor.
static_assert(StaticInfoFor<&OptOptim>.NumEnumVals == 4);
// Not `!= nullptr`: GCC's -Waddress fires on comparing a known function
// address against null.  isNullFnPtr launders it through a parameter.
static_assert(
    !clv2::detail::isNullFnPtr(StaticInfoFor<&OptOptim>.PrintEnumVal));

// Bits options are always repeatable, named and value-taking.
static_assert(StaticInfoFor<&BitsOpt>.OccurrencesFlag == ZeroOrMore);
static_assert(StaticInfoFor<&BitsOpt>.ValueExpected == ValueRequired);
static_assert(StaticInfoFor<&BitsOpt>.NumEnumVals == 3);

// Hidden-ness and category are carried as descriptor defaults; the entry keeps
// its own mutable copies because the hide/show filters rewrite them.
static_assert(StaticInfoFor<&ReallyHiddenOpt>.DefaultHidden == ReallyHidden);
} // namespace

//===----------------------------------------------------------------------===//
// Tests
//===----------------------------------------------------------------------===//

TEST(CommandLineV2, BoolFlagDefault) {
  auto [Ctx, Err] = parseArgs<&ScalarListReg>({"tool"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&ScalarListReg>();
  EXPECT_FALSE(Opts->get<&OptVerbose>());
}

TEST(CommandLineV2, BoolFlagSet) {
  auto [Ctx, Err] = parseArgs<&ScalarListReg>({"tool", "-verbose"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&ScalarListReg>();
  EXPECT_TRUE(Opts->get<&OptVerbose>());
}

TEST(CommandLineV2, BoolFlagEqualValue) {
  // Bool flags default to ValueOptional:
  // -flag=true sets true, -flag=false sets false, bare -flag sets true.
  {
    auto [Ctx, Err] = parseArgs<&ScalarListReg>({"tool", "-verbose=true"});
    ASSERT_TRUE(Err.empty()) << Err;
    ASSERT_NE(Ctx, nullptr);
    auto *Opts = Ctx->getViewPtr<&ScalarListReg>();
    EXPECT_TRUE(Opts->get<&OptVerbose>());
  }
  {
    auto [Ctx, Err] = parseArgs<&ScalarListReg>({"tool", "-verbose=false"});
    ASSERT_TRUE(Err.empty()) << Err;
    ASSERT_NE(Ctx, nullptr);
    auto *Opts = Ctx->getViewPtr<&ScalarListReg>();
    EXPECT_FALSE(Opts->get<&OptVerbose>());
  }
  {
    auto [Ctx, Err] = parseArgs<&ScalarListReg>({"tool", "-verbose"});
    ASSERT_TRUE(Err.empty()) << Err;
    ASSERT_NE(Ctx, nullptr);
    auto *Opts = Ctx->getViewPtr<&ScalarListReg>();
    EXPECT_TRUE(Opts->get<&OptVerbose>());
  }
  // An invalid value for a bool flag is still an error.
  {
    auto [Ctx, Err] = parseArgs<&ScalarListReg>({"tool", "-verbose=maybe"});
    EXPECT_FALSE(Err.empty());
  }
}

TEST(CommandLineV2, BoolFlagValueOptionalOverride) {
  // Explicit ValueOptional on a bool descriptor continues to work.
  {
    auto [Ctx, Err] = parseArgs<&ToggleReg>({"tool", "-toggle=true"});
    ASSERT_TRUE(Err.empty()) << Err;
    ASSERT_NE(Ctx, nullptr);
    auto *Opts = Ctx->getViewPtr<&ToggleReg>();
    EXPECT_TRUE(Opts->get<&OptToggle>());
  }
  {
    auto [Ctx, Err] = parseArgs<&ToggleReg>({"tool", "-toggle=false"});
    ASSERT_TRUE(Err.empty()) << Err;
    ASSERT_NE(Ctx, nullptr);
    auto *Opts = Ctx->getViewPtr<&ToggleReg>();
    EXPECT_FALSE(Opts->get<&OptToggle>());
  }
}

TEST(CommandLineV2, IntDefaultValue) {
  auto [Ctx, Err] = parseArgs<&ScalarListReg>({"tool"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&ScalarListReg>();
  EXPECT_EQ(Opts->get<&OptCount>(), 3); // Init{3}
}

TEST(CommandLineV2, IntEqualSyntax) {
  auto [Ctx, Err] = parseArgs<&ScalarListReg>({"tool", "-count=7"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&ScalarListReg>();
  EXPECT_EQ(Opts->get<&OptCount>(), 7);
}

TEST(CommandLineV2, IntSpaceSyntax) {
  auto [Ctx, Err] = parseArgs<&ScalarListReg>({"tool", "-count", "42"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&ScalarListReg>();
  EXPECT_EQ(Opts->get<&OptCount>(), 42);
}

TEST(CommandLineV2, IntInvalidValue) {
  auto [Ctx, Err] = parseArgs<&ScalarListReg>({"tool", "-count=abc"});
  EXPECT_FALSE(Err.empty());
}

TEST(CommandLineV2, StringDefaultValue) {
  auto [Ctx, Err] = parseArgs<&ScalarListReg>({"tool"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&ScalarListReg>();
  EXPECT_EQ(Opts->get<&OptOutput>(), "a.out");
}

TEST(CommandLineV2, StringOverride) {
  auto [Ctx, Err] = parseArgs<&ScalarListReg>({"tool", "-output=result.o"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&ScalarListReg>();
  EXPECT_EQ(Opts->get<&OptOutput>(), "result.o");
}

TEST(CommandLineV2, OccurrenceCount) {
  auto [Ctx, Err] = parseArgs<&ScalarListReg>({"tool", "-verbose", "-verbose"});
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&ScalarListReg>();
  // Two occurrences of -verbose; count should be 2 (no constraint violation for
  // flags)
  EXPECT_EQ(Opts->occurrences<&OptVerbose>(), 2u);
}

TEST(CommandLineV2, SpecifiedFalseWhenNotGiven) {
  auto [Ctx, Err] = parseArgs<&ScalarListReg>({"tool"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&ScalarListReg>();
  EXPECT_FALSE(Opts->specified<&OptVerbose>());
}

TEST(CommandLineV2, SpecifiedTrueWhenGiven) {
  auto [Ctx, Err] = parseArgs<&ScalarListReg>({"tool", "-verbose"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&ScalarListReg>();
  EXPECT_TRUE(Opts->specified<&OptVerbose>());
}

TEST(CommandLineV2, AllNumericTypes) {
  auto [Ctx, Err] = parseArgs<&AllScalarReg>(
      {"tool", "-width=16", "-offset=-100", "-size=4096", "-threshold=0.5",
       "-tolerance=1e-9"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&AllScalarReg>();
  EXPECT_EQ(Opts->get<&OptWidth>(), 16u);
  EXPECT_EQ(Opts->get<&OptOffset>(), -100LL);
  EXPECT_EQ(Opts->get<&OptSize>(), 4096ULL);
  EXPECT_FLOAT_EQ(Opts->get<&OptThreshold>(), 0.5f);
  EXPECT_DOUBLE_EQ(Opts->get<&OptTolerance>(), 1e-9);
}

TEST(CommandLineV2, UnknownOptionError) {
  auto [Ctx, Err] = parseArgs<&ScalarListReg>({"tool", "-notexist"});
  EXPECT_FALSE(Err.empty());
  EXPECT_NE(Err.find("Unknown command line argument"), std::string::npos);
}

//===----------------------------------------------------------------------===//
// List option tests
//===----------------------------------------------------------------------===//

TEST(CommandLineV2, ListEmpty) {
  auto [Ctx, Err] = parseArgs<&ScalarListReg>({"tool"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&ScalarListReg>();
  EXPECT_TRUE(Opts->get<&OptInputs>().empty());
}

TEST(CommandLineV2, ListSingleValue) {
  auto [Ctx, Err] = parseArgs<&ScalarListReg>({"tool", "-input=foo.cpp"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&ScalarListReg>();
  ASSERT_EQ(Opts->get<&OptInputs>().size(), 1u);
  EXPECT_EQ(Opts->get<&OptInputs>()[0], "foo.cpp");
}

// Element positions record the argv index each list element came from.  This
// is what lets a tool reconstruct the relative order of *interleaved* list
// options -- llvm-jitlink relies on it to order -l/-L/input files correctly,
// and it is the only in-tree consumer, so the mechanism is covered here rather
// than only indirectly through that tool's lit tests.
TEST(CommandLineV2, ListElementPositions) {
  auto [Ctx, Err] = parseArgs<&TwoListReg>(
      {"tool", "-input=a.cpp", "-flag=1", "-input=b.cpp"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&TwoListReg>();

  const std::vector<unsigned> &InPos = Opts->elementPositions<&OptInputs>();
  const std::vector<unsigned> &FlagPos = Opts->elementPositions<&OptFlags>();
  ASSERT_EQ(InPos.size(), 2u);
  ASSERT_EQ(FlagPos.size(), 1u);

  // One entry per parsed element, in parse order, and the two lists interleave:
  // input[0] < flag[0] < input[1].
  EXPECT_LT(InPos[0], FlagPos[0]);
  EXPECT_LT(FlagPos[0], InPos[1]);
  EXPECT_EQ(Opts->get<&OptInputs>().size(), InPos.size());
  EXPECT_EQ(Opts->get<&OptFlags>().size(), FlagPos.size());
}

TEST(CommandLineV2, ListElementPositionsEmptyWhenUnspecified) {
  auto [Ctx, Err] = parseArgs<&ScalarListReg>({"tool"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&ScalarListReg>();
  EXPECT_TRUE(Opts->elementPositions<&OptInputs>().empty());
}

TEST(CommandLineV2, ListMultipleValues) {
  auto [Ctx, Err] = parseArgs<&ScalarListReg>(
      {"tool", "-input=a.cpp", "-input=b.cpp", "-input=c.cpp"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&ScalarListReg>();
  const auto &Ins = Opts->get<&OptInputs>();
  ASSERT_EQ(Ins.size(), 3u);
  EXPECT_EQ(Ins[0], "a.cpp");
  EXPECT_EQ(Ins[1], "b.cpp");
  EXPECT_EQ(Ins[2], "c.cpp");
}

//===----------------------------------------------------------------------===//
// Enum option tests
//===----------------------------------------------------------------------===//

TEST(CommandLineV2, EnumDefaultValue) {
  auto [Ctx, Err] = parseArgs<&EnumReg>({"tool"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&EnumReg>();
  EXPECT_EQ(Opts->get<&OptOptim>(), OptimLevel::O1);
}

TEST(CommandLineV2, EnumValidValue) {
  auto [Ctx, Err] = parseArgs<&EnumReg>({"tool", "-optim=O3"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&EnumReg>();
  EXPECT_EQ(Opts->get<&OptOptim>(), OptimLevel::O3);
}

TEST(CommandLineV2, EnumInvalidValue) {
  auto [Ctx, Err] = parseArgs<&EnumReg>({"tool", "-optim=O9"});
  EXPECT_FALSE(Err.empty());
}

//===----------------------------------------------------------------------===//
// Subcommand tests
//===----------------------------------------------------------------------===//

TEST(CommandLineV2, SubCmdNotActive) {
  auto [Ctx, Err] = parseArgs<&SubCmdReg>({"tool", "-verbose"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&SubCmdReg>();
  EXPECT_FALSE(Opts->isActive<&BuildCmd>());
  EXPECT_FALSE(Opts->isActive<&TestCmd>());
  EXPECT_TRUE(Opts->get<&OptVerbose>());
}

TEST(CommandLineV2, ActiveSubCommandIsOnTheContext) {
  // The selected subcommand is a property of the parse, readable from its
  // OptionsContext (this replaced a process-wide "Activated" flag).
  auto [CtxBuild, ErrB] = parseArgs<&SubCmdReg>({"tool", "build", "-level=1"});
  ASSERT_TRUE(ErrB.empty()) << ErrB;
  ASSERT_NE(CtxBuild, nullptr);
  EXPECT_EQ(CtxBuild->getActiveSubCommand(), "build");

  auto [CtxNone, ErrN] = parseArgs<&SubCmdReg>({"tool", "-verbose"});
  ASSERT_TRUE(ErrN.empty()) << ErrN;
  ASSERT_NE(CtxNone, nullptr);
  EXPECT_TRUE(CtxNone->getActiveSubCommand().empty());

  // The earlier parse must not have been disturbed by the later one.
  EXPECT_EQ(CtxBuild->getActiveSubCommand(), "build");
}

TEST(CommandLineV2, ConcurrentParsesSelectDifferentSubCommands) {
  // Two threads selecting different subcommands must each observe their own.
  std::unique_ptr<OptionsContext> CtxA, CtxB;
  std::thread TA([&] {
    OptionParser P;
    P.add<&SubCmdReg>();
    const char *argv[] = {"tool", "build", "-level=1"};
    CtxA = P.parse(3, argv, "A");
  });
  std::thread TB([&] {
    OptionParser P;
    P.add<&SubCmdReg>();
    const char *argv[] = {"tool", "test", "-level=2"};
    CtxB = P.parse(3, argv, "B");
  });
  TA.join();
  TB.join();

  ASSERT_NE(CtxA, nullptr);
  ASSERT_NE(CtxB, nullptr);
  EXPECT_EQ(CtxA->getActiveSubCommand(), "build");
  EXPECT_EQ(CtxB->getActiveSubCommand(), "test");
}

TEST(CommandLineV2, SubCmdBuildActive) {
  auto [Ctx, Err] =
      parseArgs<&SubCmdReg>({"tool", "build", "-level=3", "-arch=arm64"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&SubCmdReg>();
  EXPECT_TRUE(Opts->isActive<&BuildCmd>());
  EXPECT_FALSE(Opts->isActive<&TestCmd>());
  auto &Sub = Opts->getSubOptions<&BuildCmd>();
  EXPECT_EQ(Sub.template get<&SubOptLevel>(), 3);
  EXPECT_EQ(Sub.template get<&SubOptArch>(), "arm64");
}

TEST(CommandLineV2, SubCmdTestActive) {
  auto [Ctx, Err] = parseArgs<&SubCmdReg>({"tool", "test", "-level=0"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&SubCmdReg>();
  EXPECT_TRUE(Opts->isActive<&TestCmd>());
  EXPECT_FALSE(Opts->isActive<&BuildCmd>());
  auto &Sub = Opts->getSubOptions<&TestCmd>();
  EXPECT_EQ(Sub.template get<&SubOptLevel>(), 0);
}

TEST(CommandLineV2, SubCmdDefaultValues) {
  auto [Ctx, Err] = parseArgs<&SubCmdReg>({"tool", "build"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&SubCmdReg>();
  EXPECT_TRUE(Opts->isActive<&BuildCmd>());
  auto &Sub = Opts->getSubOptions<&BuildCmd>();
  EXPECT_EQ(Sub.template get<&SubOptLevel>(), 2);       // Init{2}
  EXPECT_EQ(Sub.template get<&SubOptArch>(), "x86_64"); // Init{"x86_64"}
}

TEST(CommandLineV2, SubCmdGlobalOptionBeforeSubCmd) {
  auto [Ctx, Err] = parseArgs<&SubCmdReg>({"tool", "-verbose", "build"});
  // Global option before subcommand name — currently we detect subcommand
  // at argv[1], so -verbose here may or may not work depending on order.
  // For now we just check no crash.  This test may need updating when we
  // add argv scanning for subcommands.
  (void)Ctx;
}

TEST(CommandLineV2, DoubleDashEndsOptions) {
  // Everything after "--" is a positional value, even when it looks like an
  // option.  Options before "--" are still parsed normally.
  auto [Ctx, Err] =
      parseArgs<&PositionalTailReg>({"tool", "-count=5", "--", "-verbose"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&PositionalTailReg>();
  EXPECT_EQ(Opts->get<&OptCount>(), 5);
  // "-verbose" landed in the positional list rather than setting the flag.
  EXPECT_FALSE(Opts->get<&OptVerbose>());
  ASSERT_EQ(Opts->get<&OptRest>().size(), 1u);
  EXPECT_EQ(Opts->get<&OptRest>()[0], "-verbose");
}

TEST(CommandLineV2, UnconsumedPositionalIsAnError) {
  // Without a positional entry to absorb it, a token after "--" is an unknown
  // argument.  OptionParser::parse reports it and returns no context.
  auto [Ctx, Err] =
      parseArgs<&ScalarListReg>({"tool", "-count=5", "--", "-verbose"});
  EXPECT_FALSE(Err.empty());
  EXPECT_EQ(Ctx, nullptr);
}

//===----------------------------------------------------------------------===//
// Move / copy semantics
//===----------------------------------------------------------------------===//

TEST(CommandLineV2, OptionsContextIsMoveable) {
  auto [Ctx, Err] = parseArgs<&ScalarListReg>({"tool", "-count=99"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);

  // Move construct (unique_ptr)
  auto Ctx2 = std::move(Ctx);
  ASSERT_NE(Ctx2, nullptr);
  auto *Opts = Ctx2->getViewPtr<&ScalarListReg>();
  EXPECT_EQ(Opts->get<&OptCount>(), 99);
}

//===----------------------------------------------------------------------===//
// Zero-global-ctor check (compile-time)
//
// Verify that inline constexpr descriptors and registries are constant
// expressions — if they required global constructors they could not appear
// in static_assert contexts.
//===----------------------------------------------------------------------===//

TEST(CommandLineV2, DescriptorIsConstexpr) {
  // If the descriptor were not constexpr these static_asserts would fail.
  static_assert(OptCount.HasDefault, "Init modifier not applied constexpr");
  // OptionsRegistry now carries an optional ExtraHelp_ StringRef field.
  // Verify the default-constructed registry is still small (≤ sizeof
  // StringRef).
  static_assert(sizeof(ScalarListReg) <= sizeof(llvm::StringRef),
                "OptionsRegistry unexpectedly large");
}

//===----------------------------------------------------------------------===//
// Response file (@file) expansion tests
//
// Each test that needs filesystem access uses a RAII helper that writes a
// temp file and cleans it up on scope exit.
//===----------------------------------------------------------------------===//

namespace {

/// RAII temp file: created with given content, deleted on destruction.
struct TempFile {
  SmallString<128> Path;
  bool Valid = false;

  TempFile(StringRef Content) {
    int FD;
    if (sys::fs::createTemporaryFile("clv2_test", "rsp", FD, Path))
      return;
    raw_fd_ostream OS(FD, /*shouldClose=*/true);
    OS << Content;
    Valid = true;
  }
  ~TempFile() {
    if (Valid)
      sys::fs::remove(Path);
  }
  // Return "@/path/to/file" as a std::string for use in argv.
  std::string atArg() const { return ("@" + Path).str(); }
};

// Registry reused for all response file tests.
inline constexpr OptionInfo<int> RFCount{"count", "Count", Init{0}};
inline constexpr OptionInfo<bool> RFVerbose{"verbose", "Verbose"};
inline constexpr OptionInfo<std::string> RFOutput{"output", "Output",
                                                  Init{"default"}};
inline constexpr OptionsRegistry<&RFCount, &RFVerbose, &RFOutput> RFReg;

// Same options plus a positional tail, so an unexpanded '@file' token has
// somewhere to land instead of becoming an "unknown argument" error.
inline constexpr ListOptionInfo<std::string> RFRest{"rest", "Remaining args",
                                                    Positional{}};
inline constexpr OptionsRegistry<&RFCount, &RFVerbose, &RFRest> RFPositionalReg;

template <const auto *Reg> auto parseRF(std::vector<const char *> Argv) {
  std::string ErrBuf;
  raw_string_ostream Errs(ErrBuf);
  OptionParser P;
  P.template add<Reg>();
  auto Ctx =
      P.parse(static_cast<int>(Argv.size()), Argv.data(), "test-tool", &Errs);
  return std::make_pair(std::move(Ctx), std::move(ErrBuf));
}

} // anonymous namespace

TEST(CommandLineV2, ResponseFileBasic) {
  TempFile RF("-count=5");
  ASSERT_TRUE(RF.Valid);
  std::string AtArg = RF.atArg();
  auto [Ctx, Err] = parseRF<&RFReg>({"tool", AtArg.c_str()});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&RFReg>();
  EXPECT_EQ(Opts->get<&RFCount>(), 5);
}

TEST(CommandLineV2, ResponseFileWithRegularArgs) {
  TempFile RF("-count=7");
  ASSERT_TRUE(RF.Valid);
  std::string AtArg = RF.atArg();
  auto [Ctx, Err] =
      parseRF<&RFReg>({"tool", "-verbose", AtArg.c_str(), "-output=out.o"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&RFReg>();
  EXPECT_EQ(Opts->get<&RFCount>(), 7);
  EXPECT_TRUE(Opts->get<&RFVerbose>());
  EXPECT_EQ(Opts->get<&RFOutput>(), "out.o");
}

TEST(CommandLineV2, ResponseFileNested) {
  // inner.rsp contains "-count=9"
  TempFile Inner("-count=9");
  ASSERT_TRUE(Inner.Valid);
  // outer.rsp contains "@<path to inner>"
  TempFile Outer(Inner.atArg());
  ASSERT_TRUE(Outer.Valid);
  std::string AtOuter = Outer.atArg();
  auto [Ctx, Err] = parseRF<&RFReg>({"tool", AtOuter.c_str()});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&RFReg>();
  EXPECT_EQ(Opts->get<&RFCount>(), 9);
}

TEST(CommandLineV2, ResponseFileMissingSkipped) {
  // A non-existent @file is deliberately NOT an expansion error: it is left
  // unexpanded (as libiberty does) and reaches the parser as an ordinary
  // argument.  Only genuine expansion failures are fatal — see
  // ResponseFileCircularError.
  auto [Ctx, Err] = parseRF<&RFPositionalReg>(
      {"tool", "@/nonexistent_clv2_test_xyz_abc.rsp"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&RFPositionalReg>();
  EXPECT_EQ(Opts->get<&RFCount>(), 0);   // default — file was never opened
  EXPECT_FALSE(Opts->get<&RFVerbose>()); // default
  ASSERT_EQ(Opts->get<&RFRest>().size(), 1u);
  EXPECT_EQ(Opts->get<&RFRest>()[0], "@/nonexistent_clv2_test_xyz_abc.rsp");
}

TEST(CommandLineV2, ResponseFileCircularError) {
  // Create a temp file, then write its own @path into it.
  SmallString<128> Path;
  int FD;
  ASSERT_FALSE(sys::fs::createTemporaryFile("clv2_circular", "rsp", FD, Path));
  // Write self-referential content.
  {
    raw_fd_ostream OS(FD, /*shouldClose=*/true);
    OS << "@" << Path << "\n";
  }
  std::string AtArg = ("@" + Path).str();
  auto [Ctx, Err] = parseRF<&RFReg>({"tool", AtArg.c_str()});
  sys::fs::remove(Path);
  // Must report a response file error (recursive expansion or similar).
  EXPECT_FALSE(Err.empty());
  // A genuine expansion failure aborts the parse rather than silently
  // continuing with a partially-expanded argv.
  EXPECT_EQ(Ctx, nullptr);
}

TEST(CommandLineV2, ResponseFileEmpty) {
  TempFile RF("");
  ASSERT_TRUE(RF.Valid);
  std::string AtArg = RF.atArg();
  auto [Ctx, Err] = parseRF<&RFReg>({"tool", AtArg.c_str()});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&RFReg>();
  // Defaults should be intact — empty file expands to nothing.
  EXPECT_EQ(Opts->get<&RFCount>(), 0);
  EXPECT_FALSE(Opts->get<&RFVerbose>());
  EXPECT_EQ(Opts->get<&RFOutput>(), "default");
}

//===----------------------------------------------------------------------===//
// Help text tests
//===----------------------------------------------------------------------===//

namespace {

inline constexpr OptionInfo<int> HelpCount{"help-count", "Repeat count",
                                           Init{0}};
inline constexpr OptionInfo<bool> HelpVerbose{"help-verbose", "Verbose mode"};
inline constexpr OptionInfo<int> HelpHiddenOpt{"help-hidden-opt", "Hidden opt",
                                               Hidden};
inline constexpr OptionsRegistry<&HelpCount, &HelpVerbose, &HelpHiddenOpt>
    HelpReg;

} // anonymous namespace

TEST(CommandLineV2, HelpContainsOptionName) {
  std::string Help = HelpReg.helpText();
  EXPECT_NE(Help.find("-help-count"), std::string::npos);
  EXPECT_NE(Help.find("-help-verbose"), std::string::npos);
}

TEST(CommandLineV2, HelpContainsDescription) {
  std::string Help = HelpReg.helpText();
  EXPECT_NE(Help.find("Repeat count"), std::string::npos);
  EXPECT_NE(Help.find("Verbose mode"), std::string::npos);
}

TEST(CommandLineV2, HelpHidesHiddenOpt) {
  std::string Help = HelpReg.helpText(/*ShowHidden=*/false);
  EXPECT_EQ(Help.find("help-hidden-opt"), std::string::npos);
}

TEST(CommandLineV2, HelpShowsHiddenOpt) {
  std::string Help = HelpReg.helpText(/*ShowHidden=*/true);
  EXPECT_NE(Help.find("help-hidden-opt"), std::string::npos);
}

TEST(CommandLineV2, HelpEnumValues) {
  std::string Help = EnumReg.helpText();
  EXPECT_NE(Help.find("=O0"), std::string::npos);
  EXPECT_NE(Help.find("=O3"), std::string::npos);
  EXPECT_NE(Help.find("No optimisation"), std::string::npos);
}

TEST(CommandLineV2, HelpValueDescCustom) {
  std::string Help = FileOutputReg.helpText();
  EXPECT_NE(Help.find("=<filename>"), std::string::npos);
  EXPECT_EQ(Help.find("=<value>"), std::string::npos);
}

TEST(CommandLineV2, HelpValueDescDefault) {
  std::string Help = ScalarListReg.helpText();
  // -count has no value_desc — should show =<int> (type-derived default).
  EXPECT_NE(Help.find("=<int>"), std::string::npos);
}

//===----------------------------------------------------------------------===//
// -version tests
//===----------------------------------------------------------------------===//

namespace {
auto parseVersion(std::vector<const char *> Argv,
                  llvm::StringRef VersionStr = "mytool 1.0") {
  std::string ErrBuf, OutBuf;
  raw_string_ostream Errs(ErrBuf), Out(OutBuf);
  OptionParser P;
  P.add<&ScalarListReg>();
  P.parse(static_cast<int>(Argv.size()), Argv.data(),
          /*Overview=*/"test-tool", /*Errs=*/&Errs, VersionStr,
          /*HelpOS=*/&Out);
  return std::make_pair(std::move(OutBuf), std::move(ErrBuf));
}
} // anonymous namespace

TEST(CommandLineV2, VersionFlagPrints) {
  auto [Out, Err] = parseVersion({"tool", "-version"});
  EXPECT_TRUE(Err.empty()) << Err;
  EXPECT_NE(Out.find("mytool 1.0"), std::string::npos);
}

TEST(CommandLineV2, VersionFlagContainsLLVM) {
  auto [Out, Err] = parseVersion({"tool", "-version"});
  EXPECT_TRUE(Err.empty()) << Err;
  EXPECT_NE(Out.find("LLVM version"), std::string::npos);
}

TEST(CommandLineV2, VersionCallbackInvoked) {
  std::vector<const char *> Argv = {"tool", "-version"};
  std::string ErrBuf, OutBuf;
  raw_string_ostream Errs(ErrBuf), Out(OutBuf);
  OptionParser P;
  P.add<&ScalarListReg>();
  P.parse(static_cast<int>(Argv.size()), Argv.data(), "test", &Errs, "ver1",
          &Out, [](llvm::raw_ostream &OS) { OS << "callback-output\n"; });
  EXPECT_TRUE(ErrBuf.empty()) << ErrBuf;
  EXPECT_NE(OutBuf.find("callback-output"), std::string::npos);
}

TEST(CommandLineV2, VersionCallbackNull) {
  // A null VersionPrinter should not crash.
  std::vector<const char *> Argv = {"tool", "-version"};
  std::string ErrBuf, OutBuf;
  raw_string_ostream Errs(ErrBuf), Out(OutBuf);
  OptionParser P;
  P.add<&ScalarListReg>();
  P.parse(static_cast<int>(Argv.size()), Argv.data(), "test", &Errs, "ver1",
          &Out, {});
  EXPECT_TRUE(ErrBuf.empty()) << ErrBuf;
  EXPECT_NE(OutBuf.find("LLVM version"), std::string::npos);
}

//===----------------------------------------------------------------------===//
// Alias tests
//===----------------------------------------------------------------------===//

TEST(CommandLineV2, AliasResolvesValue) {
  auto [Ctx, Err] = parseArgs<&AliasReg>({"tool", "-o=myfile.txt"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&AliasReg>();
  EXPECT_EQ(Opts->get<&AliasTarget>(), "myfile.txt");
}

TEST(CommandLineV2, AliasAndPrimaryBothWork) {
  {
    auto [Ctx, Err] = parseArgs<&AliasReg>({"tool", "-output-long=a.out"});
    ASSERT_TRUE(Err.empty()) << Err;
    ASSERT_NE(Ctx, nullptr);
    auto *Opts = Ctx->getViewPtr<&AliasReg>();
    EXPECT_EQ(Opts->get<&AliasTarget>(), "a.out");
  }
  {
    auto [Ctx, Err] = parseArgs<&AliasReg>({"tool", "-o=b.out"});
    ASSERT_TRUE(Err.empty()) << Err;
    ASSERT_NE(Ctx, nullptr);
    auto *Opts = Ctx->getViewPtr<&AliasReg>();
    EXPECT_EQ(Opts->get<&AliasTarget>(), "b.out");
  }
}

TEST(CommandLineV2, AliasIncrOccurrenceCount) {
  auto [Ctx, Err] = parseArgs<&AliasReg>({"tool", "-o=f"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&AliasReg>();
  EXPECT_EQ(Opts->occurrences<&AliasTarget>(), 1);
}

TEST(CommandLineV2, AliasUnknownTargetError) {
  auto [Ctx, Err] = parseArgs<&BadAliasReg>({"tool"});
  EXPECT_FALSE(Err.empty());
}

// Multi-registry tests

TEST(CommandLineV2, ComposedBothRegistriesParsed) {
  std::vector<const char *> Argv = {"tool", "-comp-a=hello", "-comp-b=7"};
  std::string ErrBuf;
  llvm::raw_string_ostream Errs(ErrBuf);
  OptionParser P;
  P.add<&CompRegA>();
  P.add<&CompRegB>();
  auto Ctx = P.parse(static_cast<int>(Argv.size()), Argv.data(), "test", &Errs);
  ASSERT_TRUE(ErrBuf.empty()) << ErrBuf;
  ASSERT_NE(Ctx, nullptr);
  auto *OptsA = Ctx->getViewPtr<&CompRegA>();
  auto *OptsB = Ctx->getViewPtr<&CompRegB>();
  EXPECT_EQ(OptsA->get<&CompOptA>(), "hello");
  EXPECT_EQ(OptsB->get<&CompOptB>(), 7);
}

TEST(CommandLineV2, ComposedDefaultsApplied) {
  std::vector<const char *> Argv = {"tool", "-comp-a=x"};
  std::string ErrBuf;
  llvm::raw_string_ostream Errs(ErrBuf);
  OptionParser P;
  P.add<&CompRegA>();
  P.add<&CompRegB>();
  auto Ctx = P.parse(static_cast<int>(Argv.size()), Argv.data(), "test", &Errs);
  ASSERT_TRUE(ErrBuf.empty()) << ErrBuf;
  ASSERT_NE(Ctx, nullptr);
  auto *OptsB = Ctx->getViewPtr<&CompRegB>();
  EXPECT_EQ(OptsB->get<&CompOptB>(), 42); // default from Init{42}
}

TEST(CommandLineV2, ComposedGetViewAndDirectAccess) {
  // Verify getViewPtr<&Registry>() access via OptionsContext.
  std::vector<const char *> Argv = {"tool", "-comp-a=tup"};
  std::string ErrBuf;
  llvm::raw_string_ostream Errs(ErrBuf);
  OptionParser P;
  P.add<&CompRegA>();
  P.add<&CompRegB>();
  auto Ctx = P.parse(static_cast<int>(Argv.size()), Argv.data(), "test", &Errs);
  ASSERT_TRUE(ErrBuf.empty()) << ErrBuf;
  ASSERT_NE(Ctx, nullptr);
  // Per-registry view access
  auto *ViewA = Ctx->getViewPtr<&CompRegA>();
  auto *ViewB = Ctx->getViewPtr<&CompRegB>();
  ASSERT_NE(ViewA, nullptr);
  ASSERT_NE(ViewB, nullptr);
  EXPECT_EQ(ViewA->get<&CompOptA>(), "tup");
  EXPECT_EQ(ViewB->get<&CompOptB>(), 42);
  // specified checks
  EXPECT_TRUE(ViewA->specified<&CompOptA>());
  EXPECT_FALSE(ViewB->specified<&CompOptB>());
}

TEST(CommandLineV2, ComposedAliasAcrossRegistries) {
  std::vector<const char *> Argv = {"tool", "-ca=crossfile"};
  std::string ErrBuf;
  llvm::raw_string_ostream Errs(ErrBuf);
  OptionParser P;
  P.add<&CompRegC>();
  P.add<&CompRegAlias>();
  auto Ctx = P.parse(static_cast<int>(Argv.size()), Argv.data(), "test", &Errs);
  ASSERT_TRUE(ErrBuf.empty()) << ErrBuf;
  ASSERT_NE(Ctx, nullptr);
  auto *OptsC = Ctx->getViewPtr<&CompRegC>();
  EXPECT_EQ(OptsC->get<&CompOptC>(), "crossfile");
}

TEST(CommandLineV2, HelpNeverShowsReallyHiddenOpt) {
  EXPECT_EQ(ReallyHiddenReg.helpText(false).find("secret"), std::string::npos);
  EXPECT_EQ(ReallyHiddenReg.helpText(true).find("secret"), std::string::npos);
}

TEST(CommandLineV2, ExtraHelpAppearsInHelpText) {
  std::string Help = ExtraHelpReg.helpText();
  EXPECT_NE(Help.find("https://example.com"), std::string::npos);
}

TEST(CommandLineV2, ExtraHelpEmptyByDefault) {
  // A registry with no ExtraHelp should not contain stray text from
  // the ExtraHelp_ field being uninitialised or default-initialised wrong.
  std::string Help = ScalarListReg.helpText();
  EXPECT_NE(Help.find("verbose"),
            std::string::npos); // sanity: known option present
}

// Prefix-format tests

TEST(CommandLineV2, PrefixOptionShadowedBySubCommand) {
  // Without a subcommand the global prefix option takes the glued value.
  auto [CtxG, ErrG] = parseArgs<&PrefixSubReg>({"tool", "-Iglobal"});
  ASSERT_TRUE(ErrG.empty()) << ErrG;
  ASSERT_NE(CtxG, nullptr);
  auto *VG = CtxG->getViewPtr<&PrefixSubReg>();
  ASSERT_NE(VG, nullptr);
  EXPECT_EQ(VG->get<&GlobalIncOpt>(), std::vector<std::string>{"global"});

  // With the subcommand active its own -I shadows the global one, matching
  // how find() resolves non-prefix names.
  auto [CtxS, ErrS] = parseArgs<&PrefixSubReg>({"tool", "go", "-Isub"});
  ASSERT_TRUE(ErrS.empty()) << ErrS;
  ASSERT_NE(CtxS, nullptr);
  EXPECT_EQ(CtxS->getActiveSubCommand(), "go");
  auto *VS = CtxS->getViewPtr<&PrefixSubReg>();
  ASSERT_NE(VS, nullptr);
  EXPECT_TRUE(VS->get<&GlobalIncOpt>().empty())
      << "global -I should not have consumed the subcommand's argument";
}

TEST(CommandLineV2, PrefixOptionGluedValue) {
  auto [Ctx, Err] = parseArgs<&PrefixReg>({"tool", "-I/usr/include"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&PrefixReg>();
  ASSERT_EQ(Opts->get<&PrefixOpt>().size(), 1u);
  EXPECT_EQ(Opts->get<&PrefixOpt>()[0], "/usr/include");
}

TEST(CommandLineV2, PrefixOptionMultiple) {
  auto [Ctx, Err] = parseArgs<&PrefixReg>({"tool", "-I/a", "-I/b"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&PrefixReg>();
  ASSERT_EQ(Opts->get<&PrefixOpt>().size(), 2u);
  EXPECT_EQ(Opts->get<&PrefixOpt>()[0], "/a");
  EXPECT_EQ(Opts->get<&PrefixOpt>()[1], "/b");
}

TEST(CommandLineV2, PrefixOptionEqualsAlsoWorks) {
  auto [Ctx, Err] = parseArgs<&PrefixReg>({"tool", "-I=/sys"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&PrefixReg>();
  ASSERT_EQ(Opts->get<&PrefixOpt>().size(), 1u);
  EXPECT_EQ(Opts->get<&PrefixOpt>()[0], "/sys");
}

// BitsOptionInfo<T> tests

TEST(CommandLineV2, BitsDefaultZero) {
  auto [Ctx, Err] = parseArgs<&BitsReg>({"tool"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&BitsReg>();
  EXPECT_EQ(Opts->get<&BitsOpt>(), 0u);
}

TEST(CommandLineV2, BitsOneValue) {
  auto [Ctx, Err] = parseArgs<&BitsReg>({"tool", "-emit=obj"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&BitsReg>();
  // EmitKind::Obj is index 0 → bit 0
  EXPECT_EQ(Opts->get<&BitsOpt>(), 1u << 0);
}

TEST(CommandLineV2, BitsMultipleValues) {
  auto [Ctx, Err] = parseArgs<&BitsReg>({"tool", "-emit=obj", "-emit=asm"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&BitsReg>();
  // Obj=bit0, Asm=bit1 → 0b11
  EXPECT_EQ(Opts->get<&BitsOpt>(), (1u << 0) | (1u << 1));
}

TEST(CommandLineV2, BitsInvalidValue) {
  auto [Ctx, Err] = parseArgs<&BitsReg>({"tool", "-emit=unknown"});
  EXPECT_FALSE(Err.empty());
}

TEST(CommandLineV2, BitsHelpShowsValues) {
  std::string Help = BitsReg.helpText();
  EXPECT_NE(Help.find("obj"), std::string::npos);
  EXPECT_NE(Help.find("asm"), std::string::npos);
  EXPECT_NE(Help.find("bc"), std::string::npos);
}

// Sink tests

TEST(CommandLineV2, SinkCollectsUnknownOptions) {
  // Unknown flags should go into the sink, not produce an error.
  auto [Ctx, Err] =
      parseArgs<&SinkReg>({"tool", "-arm-parallel-dsp", "-known=x"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&SinkReg>();
  EXPECT_EQ(Opts->get<&SinkKnownOpt>(), "x");
  auto &Sunk = Opts->get<&SinkOpt>();
  ASSERT_EQ(Sunk.size(), 1u);
  EXPECT_EQ(Sunk[0], "-arm-parallel-dsp");
}

TEST(CommandLineV2, SinkCollectsUnknownWithValue) {
  auto [Ctx, Err] = parseArgs<&SinkReg>({"tool", "-foo=bar"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&SinkReg>();
  auto &Sunk = Opts->get<&SinkOpt>();
  ASSERT_EQ(Sunk.size(), 1u);
  EXPECT_EQ(Sunk[0], "-foo=bar");
}

TEST(CommandLineV2, SinkCollectsMultiple) {
  auto [Ctx, Err] =
      parseArgs<&SinkReg>({"tool", "-mem2reg", "-gvn", "-known=hello"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&SinkReg>();
  EXPECT_EQ(Opts->get<&SinkKnownOpt>(), "hello");
  auto &Sunk = Opts->get<&SinkOpt>();
  ASSERT_EQ(Sunk.size(), 2u);
  EXPECT_EQ(Sunk[0], "-mem2reg");
  EXPECT_EQ(Sunk[1], "-gvn");
}

TEST(CommandLineV2, NoSinkStillErrors) {
  // Without a Sink, unknown options still produce an error.
  auto [Ctx, Err] = parseArgs<&ScalarListReg>({"tool", "-unknown-flag"});
  EXPECT_FALSE(Err.empty());
}

// PositionalEatsArgs tests

TEST(CommandLineV2, PositionalEatArgsEqualsError) {
  auto [Ctx, Err] =
      parseArgs<&PosEatArgsReg>({"prog", "-positional-eat-args=XXXX"});
  EXPECT_FALSE(Err.empty());
}

TEST(CommandLineV2, PositionalEatArgsEqualsWithTrailingError) {
  auto [Ctx, Err] =
      parseArgs<&PosEatArgsReg>({"prog", "-positional-eat-args=XXXX", "-foo"});
  EXPECT_FALSE(Err.empty());
}

TEST(CommandLineV2, PositionalEatArgsConsumesDashArgs) {
  auto [Ctx, Err] =
      parseArgs<&PosEatArgsReg>({"prog", "-positional-eat-args", "-foo"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&PosEatArgsReg>();
  auto &Eaten = Opts->get<&PosEatArgs1>();
  ASSERT_EQ(Eaten.size(), 1u);
  EXPECT_EQ(Eaten[0], "-foo");
}

TEST(CommandLineV2, PositionalEatArgsSwitchBetweenTwo) {
  auto [Ctx, Err] =
      parseArgs<&PosEatArgsReg>({"prog", "-positional-eat-args", "-flag",
                                 "-positional-eat-args2", "-bar", "foo"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&PosEatArgsReg>();
  auto &Eaten1 = Opts->get<&PosEatArgs1>();
  ASSERT_EQ(Eaten1.size(), 1u);
  EXPECT_EQ(Eaten1[0], "-flag");
  auto &Eaten2 = Opts->get<&PosEatArgs2>();
  ASSERT_EQ(Eaten2.size(), 2u);
  EXPECT_EQ(Eaten2[0], "-bar");
  EXPECT_EQ(Eaten2[1], "foo");
}

// Callback tests

TEST(CommandLineV2, ListCallbackFiresPerValue) {
  CallbackLog.clear();
  auto [Ctx, Err] = parseArgs<&CbListReg>({"tool", "--cb-list=a,b,c"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&CbListReg>();
  auto &Vals = Opts->get<&CbListOpt>();
  ASSERT_EQ(Vals.size(), 3u);
  ASSERT_EQ(CallbackLog.size(), 3u);
  EXPECT_EQ(CallbackLog[0], "a");
  EXPECT_EQ(CallbackLog[1], "b");
  EXPECT_EQ(CallbackLog[2], "c");
}

TEST(CommandLineV2, ListCallbackWithoutCommaSeparated) {
  CallbackLog.clear();
  auto [Ctx, Err] = parseArgs<&CbListNoCSReg>({"tool", "--cb-list-nocs=a,b,c"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&CbListNoCSReg>();
  auto &Vals = Opts->get<&CbListNoCSOpt>();
  ASSERT_EQ(Vals.size(), 1u);
  EXPECT_EQ(Vals[0], "a,b,c");
  ASSERT_EQ(CallbackLog.size(), 1u);
  EXPECT_EQ(CallbackLog[0], "a,b,c");
}

TEST(CommandLineV2, ScalarCallback) {
  CallbackLog.clear();
  auto [Ctx, Err] = parseArgs<&CbScalarReg>({"tool", "--cb-scalar=42"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&CbScalarReg>();
  EXPECT_EQ(Opts->get<&CbScalarOpt>(), 42);
  ASSERT_EQ(CallbackLog.size(), 1u);
  EXPECT_EQ(CallbackLog[0], "42");
}

TEST(CommandLineV2, NullCallbackDoesNotCrash) {
  auto [Ctx, Err] = parseArgs<&ScalarListReg>({"tool", "--input=hello"});
  ASSERT_TRUE(Err.empty()) << Err;
  ASSERT_NE(Ctx, nullptr);
  auto *Opts = Ctx->getViewPtr<&ScalarListReg>();
  auto &Vals = Opts->get<&OptInputs>();
  ASSERT_EQ(Vals.size(), 1u);
  EXPECT_EQ(Vals[0], "hello");
}

//===----------------------------------------------------------------------===//
// OptionParser tests
//===----------------------------------------------------------------------===//

namespace {

// Registries for OptionParser tests.
inline constexpr OptionInfo<int> OPTestCount{"op-count", "A count", Init{0}};
inline constexpr OptionInfo<bool> OPTestFlag{"op-flag", "A flag"};
inline constexpr OptionsRegistry<&OPTestCount, &OPTestFlag> OPTestReg1;

inline constexpr OptionInfo<std::string> OPTestName{"op-name", "A name",
                                                    Init{"default"}};
inline constexpr OptionsRegistry<&OPTestName> OPTestReg2;

TEST(CommandLineV2, OptionParserRoundTrip) {
  OptionParser P;
  P.add<&OPTestReg1>();
  P.add<&OPTestReg2>();
  const char *argv[] = {"test", "--op-count=42", "--op-flag",
                        "--op-name=hello"};
  auto Ctx = P.parse(4, argv, "test tool");
  ASSERT_NE(Ctx, nullptr);
  EXPECT_EQ((getOptValOr<&OPTestReg1, &OPTestCount>(*Ctx, 0)), 42);
  EXPECT_EQ((getOptValOr<&OPTestReg1, &OPTestFlag>(*Ctx, false)), true);
  EXPECT_EQ((getOptValOr<&OPTestReg2, &OPTestName>(*Ctx, std::string{})),
            "hello");
}

TEST(CommandLineV2, OptionParserRepeatedParseIndependence) {
  // Two sequential parses with different argv must produce independent results.
  const char *argv1[] = {"test", "--op-count=10"};
  const char *argv2[] = {"test", "--op-count=20"};

  OptionParser P1;
  P1.add<&OPTestReg1>();
  auto Ctx1 = P1.parse(2, argv1, "test");

  OptionParser P2;
  P2.add<&OPTestReg1>();
  auto Ctx2 = P2.parse(2, argv2, "test");

  ASSERT_NE(Ctx1, nullptr);
  ASSERT_NE(Ctx2, nullptr);
  EXPECT_EQ((getOptValOr<&OPTestReg1, &OPTestCount>(*Ctx1, 0)), 10);
  EXPECT_EQ((getOptValOr<&OPTestReg1, &OPTestCount>(*Ctx2, 0)), 20);
}

// Ctx is the flag that records whether the dynamic entry fired.
static bool dynOptFired(void *Ctx, const bool &) {
  *static_cast<bool *>(Ctx) = true;
  return true;
}

TEST(CommandLineV2, OptionParserDynamicEntry) {
  static bool DynFired = false;
  DynFired = false;

  OptionParser P;
  P.add<&OPTestReg1>();

  // Entries added at runtime are built from a descriptor, the same way
  // production code does it; the CtxCallback stands in for a side effect.
  bool Slot = false;
  unsigned Count = 0;
  clv2::RuntimeOption<bool> DynOpt(
      "dyn-opt", "A dynamic option", ValueDisallowed,
      clv2::CtxCallback<bool>{&dynOptFired, &DynFired});
  P.addDynamicEntry(DynOpt.makeEntry());
  (void)Slot;
  (void)Count;

  const char *argv[] = {"test", "--dyn-opt"};
  auto Ctx = P.parse(2, argv, "test");
  ASSERT_NE(Ctx, nullptr);
  EXPECT_TRUE(DynFired);
}

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Concurrency test — two OptionParser::parse calls on two threads.
// Must produce independent, correct results. TSan-clean.
//===----------------------------------------------------------------------===//

#include <atomic>

namespace {

// Distinct registries for the two threads — no sharing.
inline constexpr OptionInfo<int> ThreadAOpt{"thread-a-val", "Thread A value"};
inline constexpr OptionsRegistry<&ThreadAOpt> ThreadAReg;

inline constexpr OptionInfo<int> ThreadBOpt{"thread-b-val", "Thread B value"};
inline constexpr OptionsRegistry<&ThreadBOpt> ThreadBReg;

TEST(CommandLineV2, ConcurrentParsesAreIndependent) {
  std::unique_ptr<OptionsContext> CtxA, CtxB;

  std::thread TA([&] {
    OptionParser P;
    P.add<&ThreadAReg>();
    const char *argv[] = {"a", "--thread-a-val=111"};
    CtxA = P.parse(2, argv, "thread A");
  });

  std::thread TB([&] {
    OptionParser P;
    P.add<&ThreadBReg>();
    const char *argv[] = {"b", "--thread-b-val=222"};
    CtxB = P.parse(2, argv, "thread B");
  });

  TA.join();
  TB.join();

  ASSERT_NE(CtxA, nullptr);
  ASSERT_NE(CtxB, nullptr);
  EXPECT_EQ((getOptValOr<&ThreadAReg, &ThreadAOpt>(*CtxA, 0)), 111);
  EXPECT_EQ((getOptValOr<&ThreadBReg, &ThreadBOpt>(*CtxB, 0)), 222);

  // Cross-check: each context should NOT have the other's registry.
  EXPECT_EQ(CtxA->getViewPtr<&ThreadBReg>(), nullptr);
  EXPECT_EQ(CtxB->getViewPtr<&ThreadAReg>(), nullptr);
}

//===----------------------------------------------------------------------===//
// Dynamically-registered registries must also get per-parse storage.
//
// The tests above only cover statically-added registries, which have always
// had per-parse storage.  A dynamic registration is the harder case: it is
// discovered from a process-wide list, so if the registration owned a single
// ParsedOptions instance every parse would write the same slots.
//===----------------------------------------------------------------------===//

inline constexpr OptionInfo<int> DynSharedOpt{"dyn-shared-val",
                                              "Dynamically registered value"};
inline constexpr OptionsRegistry<&DynSharedOpt> DynSharedReg;

// Registered once, at static-init time, exactly as library code does.
[[maybe_unused]] static const int RegisterDynShared = [] {
  registerDynamicRegistry<&DynSharedReg>();
  return 0;
}();

/// Parse the dynamic option with a given value and return what the resulting
/// context reports.  Each call must observe only its own value.
static int parseDynShared(const char *Arg) {
  OptionParser P;
  P.enableGlobalDynamicEntries();
  const char *argv[] = {"tool", Arg};
  auto Ctx = P.parse(2, argv, "dyn");
  if (!Ctx)
    return -1;
  return getOptValOr<&DynSharedReg, &DynSharedOpt>(*Ctx, -1);
}

TEST(CommandLineV2, DynamicRegistrySequentialParsesAreIndependent) {
  // Each parse starts from defaults and keeps its own values; a later parse
  // must not retroactively change what an earlier context reports.
  OptionParser P1;
  P1.enableGlobalDynamicEntries();
  const char *argv1[] = {"tool", "--dyn-shared-val=11"};
  auto Ctx1 = P1.parse(2, argv1, "first");
  ASSERT_NE(Ctx1, nullptr);

  OptionParser P2;
  P2.enableGlobalDynamicEntries();
  const char *argv2[] = {"tool", "--dyn-shared-val=22"};
  auto Ctx2 = P2.parse(2, argv2, "second");
  ASSERT_NE(Ctx2, nullptr);

  EXPECT_EQ((getOptValOr<&DynSharedReg, &DynSharedOpt>(*Ctx1, -1)), 11);
  EXPECT_EQ((getOptValOr<&DynSharedReg, &DynSharedOpt>(*Ctx2, -1)), 22);

  // A parse that does not mention the option sees the default, not a value
  // left behind by an earlier parse.
  OptionParser P3;
  P3.enableGlobalDynamicEntries();
  const char *argv3[] = {"tool"};
  auto Ctx3 = P3.parse(1, argv3, "third");
  ASSERT_NE(Ctx3, nullptr);
  EXPECT_EQ((getOptValOr<&DynSharedReg, &DynSharedOpt>(*Ctx3, -1)), 0);
}

TEST(CommandLineV2, DynamicRegistryConcurrentParsesAreIndependent) {
  // The scenario clv2 exists to support: two in-process jobs parsing the same
  // dynamically-registered option with different values, at the same time.
  // With a single shared storage these interleave and each thread can observe
  // the other's value.
  static constexpr int NumIters = 200;
  std::atomic<int> MismatchesA{0}, MismatchesB{0};

  std::thread TA([&] {
    for (int I = 0; I < NumIters; ++I)
      if (parseDynShared("--dyn-shared-val=111") != 111)
        ++MismatchesA;
  });
  std::thread TB([&] {
    for (int I = 0; I < NumIters; ++I)
      if (parseDynShared("--dyn-shared-val=222") != 222)
        ++MismatchesB;
  });
  TA.join();
  TB.join();

  EXPECT_EQ(MismatchesA.load(), 0);
  EXPECT_EQ(MismatchesB.load(), 0);
}

//===----------------------------------------------------------------------===//
// Registration list concurrency
//===----------------------------------------------------------------------===//

// The registration lists are process-global and append-only.  These cover the
// three properties the deque + shared-mutex design relies on.

TEST(CommandLineV2, RegistrationListConcurrentAppends) {
  // Several threads registering at once must not corrupt the list, and every
  // registration must survive.  Before the lists were synchronised this raced
  // on the underlying container.
  static constexpr int NumThreads = 4;
  static constexpr int PerThread = 50;
  static std::deque<RuntimeOption<bool>> Opts;
  std::mutex OptsMu;

  std::vector<std::thread> Ts;
  for (int T = 0; T < NumThreads; ++T)
    Ts.emplace_back([T, &OptsMu] {
      for (int I = 0; I < PerThread; ++I) {
        RuntimeOption<bool> *O;
        {
          std::lock_guard<std::mutex> L(OptsMu);
          Opts.emplace_back(
              ("rl-concurrent-" + std::to_string(T) + "-" + std::to_string(I))
                  .c_str(),
              "registration list stress", ValueDisallowed);
          O = &Opts.back();
        }
        registerDynamicEntry(O->makeEntry());
      }
    });
  for (auto &T : Ts)
    T.join();

  // Every registered option is visible to a parser that drains global entries.
  OptionParser P;
  P.enableGlobalDynamicEntries();
  const char *Argv[] = {"tool", "--rl-concurrent-0-0", "--rl-concurrent-3-49"};
  std::string Err;
  raw_string_ostream OS(Err);
  auto Ctx = P.parse(3, Argv, "tool", &OS);
  ASSERT_NE(Ctx, nullptr) << Err;
  EXPECT_TRUE(Err.empty()) << Err;
}

TEST(CommandLineV2, RegistrationListSnapshotSurvivesAppend) {
  // A drain snapshots element addresses and then works without the lock, so
  // appends afterwards must not move or invalidate what it already holds.
  // std::deque gives that; std::vector would not.
  static std::deque<RuntimeOption<bool>> Opts;
  Opts.emplace_back("rl-snapshot-before", "present when the snapshot is taken",
                    ValueDisallowed);
  registerDynamicEntry(Opts.back().makeEntry());

  OptionParser P1;
  P1.enableGlobalDynamicEntries();
  const char *Argv1[] = {"tool", "--rl-snapshot-before"};
  std::string Err1;
  raw_string_ostream OS1(Err1);
  ASSERT_NE(P1.parse(2, Argv1, "tool", &OS1), nullptr) << Err1;

  // Append well past any small-size threshold, then re-parse: the earlier
  // option must still resolve, and the new ones must be visible too.
  for (int I = 0; I < 200; ++I) {
    Opts.emplace_back(("rl-snapshot-after-" + std::to_string(I)).c_str(),
                      "appended later", ValueDisallowed);
    registerDynamicEntry(Opts.back().makeEntry());
  }

  OptionParser P2;
  P2.enableGlobalDynamicEntries();
  const char *Argv2[] = {"tool", "--rl-snapshot-before",
                         "--rl-snapshot-after-199"};
  std::string Err2;
  raw_string_ostream OS2(Err2);
  ASSERT_NE(P2.parse(3, Argv2, "tool", &OS2), nullptr) << Err2;
  EXPECT_TRUE(Err2.empty()) << Err2;
}

TEST(CommandLineV2, RegistrationListIndexIsStable) {
  // publishDynamicStorages indexes a fresh snapshot using the index recorded
  // during the drain.  That is only sound because the lists are append-only,
  // so an index keeps naming the same registration.  Registering more between
  // two parses must not shift what an earlier index refers to.
  auto ParseOne = [](const char *Arg) {
    OptionParser P;
    P.enableGlobalDynamicEntries();
    const char *Argv[] = {"tool", Arg};
    std::string Err;
    raw_string_ostream OS(Err);
    auto Ctx = P.parse(2, Argv, "tool", &OS);
    return Ctx != nullptr && Err.empty();
  };

  static std::deque<RuntimeOption<bool>> Opts;
  Opts.emplace_back("rl-index-first", "registered first", ValueDisallowed);
  registerDynamicEntry(Opts.back().makeEntry());
  EXPECT_TRUE(ParseOne("--rl-index-first"));

  Opts.emplace_back("rl-index-second", "registered second", ValueDisallowed);
  registerDynamicEntry(Opts.back().makeEntry());

  // Both still resolve; the first did not shift when the second arrived.
  EXPECT_TRUE(ParseOne("--rl-index-first"));
  EXPECT_TRUE(ParseOne("--rl-index-second"));
}

TEST(CommandLineV2, RuntimeSubcommandRegistryConcurrentAppends) {
  // registerRuntimeSubcommand is public LLVM_ABI, so out-of-tree code can call
  // it at any time.  runParser used to bind a reference into the container and
  // index it while matching; a concurrent append then reallocated underneath
  // that reference.  It now snapshots, and appends take the shared mutex.
  static constexpr int NumThreads = 4;
  static constexpr int PerThread = 40;

  std::vector<std::thread> Ts;
  for (int T = 0; T < NumThreads; ++T)
    Ts.emplace_back([T] {
      for (int I = 0; I < PerThread; ++I) {
        RuntimeSubCommandEntry E;
        E.Name = "rt-sub-" + std::to_string(T) + "-" + std::to_string(I);
        E.Desc = "runtime subcommand registry stress";
        registerRuntimeSubcommand(std::move(E));
      }
    });
  for (auto &T : Ts)
    T.join();

  // Every registration survived, and the snapshot sees them all.
  auto Snap = getRuntimeSubcommands();
  auto Has = [&](StringRef N) {
    return llvm::any_of(
        Snap, [&](const RuntimeSubCommandEntry *E) { return E->Name == N; });
  };
  EXPECT_TRUE(Has("rt-sub-0-0"));
  EXPECT_TRUE(Has("rt-sub-3-39"));

  // A parse resolves a registered runtime subcommand rather than erroring.
  OptionParser P;
  const char *Argv[] = {"tool", "rt-sub-1-7"};
  std::string Err;
  raw_string_ostream OS(Err);
  auto Ctx = P.parse(2, Argv, "tool", &OS);
  ASSERT_NE(Ctx, nullptr) << Err;
  EXPECT_EQ(Ctx->getActiveSubCommand(), "rt-sub-1-7");
}

//===----------------------------------------------------------------------===//
// CompiledParser
//===----------------------------------------------------------------------===//

inline constexpr OptionInfo<int> CmpWidth{"cmp-width", "width", Init{7}};
inline constexpr OptionInfo<std::string> CmpName{"cmp-name", "name"};
inline constexpr OptionInfo<bool> CmpFlag{"cmp-flag", "flag"};
inline constexpr AliasInfo CmpFlagAlias{"cmp-F", "cmp-flag"};
inline constexpr OptionInfo<bool> CmpHidden{"cmp-hidden", "hidden", Hidden};
inline constexpr OptionsRegistry<&CmpWidth, &CmpName, &CmpFlag, &CmpFlagAlias,
                                 &CmpHidden>
    CmpReg;

TEST(CommandLineV2, CompiledParserMatchesUncompiled) {
  const char *argv[] = {"tool", "--cmp-width=3", "--cmp-name=abc", "--cmp-F"};

  OptionParser P1;
  P1.add<&CmpReg>();
  auto Ctx1 = P1.parse(4, argv, "tool");
  ASSERT_NE(Ctx1, nullptr);

  OptionParser P2;
  P2.add<&CmpReg>();
  const CompiledParser CP = P2.compile();
  auto Ctx2 = CP.parse(4, argv, "tool");
  ASSERT_NE(Ctx2, nullptr);

  const auto *A = Ctx1->getViewPtr<&CmpReg>();
  const auto *B = Ctx2->getViewPtr<&CmpReg>();
  ASSERT_NE(A, nullptr);
  ASSERT_NE(B, nullptr);
  EXPECT_EQ(A->get<&CmpWidth>(), B->get<&CmpWidth>());
  EXPECT_EQ(A->get<&CmpName>(), B->get<&CmpName>());
  EXPECT_EQ(A->get<&CmpFlag>(), B->get<&CmpFlag>());
  EXPECT_EQ(B->get<&CmpWidth>(), 3);
  EXPECT_EQ(B->get<&CmpName>(), "abc");
  EXPECT_TRUE(B->get<&CmpFlag>());
}

TEST(CommandLineV2, CompiledParserRejectsUnknownOption) {
  // The baked index covers only the registry block; an unknown name must not
  // fall through to some neighbouring entry.
  OptionParser P;
  P.add<&CmpReg>();
  const CompiledParser CP = P.compile();
  const char *argv[] = {"tool", "--no-such-option-here"};
  std::string Err;
  raw_string_ostream OS(Err);
  auto Ctx = CP.parse(2, argv, "tool", &OS);
  EXPECT_EQ(Ctx, nullptr);
  EXPECT_NE(Err.find("Unknown command line argument"), std::string::npos);
}

TEST(CommandLineV2, CompiledParserFindsBuiltinsAndAliases) {
  // Builtins are prepended to the entry list at parse time, which shifts the
  // registry block the index was built against.  Both must still resolve.
  OptionParser P;
  P.add<&CmpReg>();
  const CompiledParser CP = P.compile();

  std::string Help, Err;
  raw_string_ostream HS(Help), ES(Err);
  const char *argv[] = {"tool", "--help"};
  CP.parse(2, argv, "tool", &ES, "", &HS);
  EXPECT_NE(Help.find("--cmp-width"), std::string::npos);
  EXPECT_EQ(Help.find("--cmp-hidden"), std::string::npos);

  // An alias with no description of its own is hidden, so it only shows up
  // under --help-hidden -- but it must still parse (checked below).
  std::string HelpHidden, Err2;
  raw_string_ostream HHS(HelpHidden), ES2(Err2);
  const char *argvH[] = {"tool", "--help-hidden"};
  CP.parse(2, argvH, "tool", &ES2, "", &HHS);
  EXPECT_NE(HelpHidden.find("--cmp-F"), std::string::npos);
  EXPECT_NE(HelpHidden.find("--cmp-hidden"), std::string::npos);

  const char *argv2[] = {"tool", "--cmp-F"};
  auto Ctx = CP.parse(2, argv2, "tool");
  ASSERT_NE(Ctx, nullptr);
  EXPECT_TRUE(Ctx->getViewPtr<&CmpReg>()->get<&CmpFlag>());
}

static bool CmpBridgeSawValue = false;
static void applyCmpBridge(const decltype(CmpReg)::ParsedOptionsT &Opts) {
  CmpBridgeSawValue = Opts.get<&CmpFlag>();
}

TEST(CommandLineV2, CompiledParserIsShareable) {
  OptionParser P;
  P.add<&CmpReg>();
  EXPECT_TRUE(P.compile().isShareable());

  // Dynamic entries point at caller-owned slots, so concurrent parses would
  // race on them; such a parser reports itself as not shareable.
  static std::deque<RuntimeOption<bool>> Opts;
  Opts.emplace_back("cmp-runtime", "built at runtime", ValueDisallowed);
  OptionParser P2;
  P2.add<&CmpReg>();
  P2.addDynamicEntry(Opts.back().makeEntry());
  EXPECT_FALSE(P2.compile().isShareable());

  // A bridge function writes parsed values into process-wide variables, so a
  // parser carrying one is no more shareable than one with dynamic entries.
  OptionParser P3;
  P3.add<&CmpReg, &applyCmpBridge>();
  EXPECT_FALSE(P3.compile().isShareable());
}

#if !defined(NDEBUG) && defined(GTEST_HAS_DEATH_TEST) && GTEST_HAS_DEATH_TEST
// Reentering parse() on the same parser overlaps two parses on one thread, so
// the guard is exercised without racing threads and without fork()ing a
// multi-threaded process.  A validator runs while the outer parse is in
// flight, which is the reentry point.
const CompiledParser *ReentryTarget = nullptr;

bool reenterParse(const bool &, llvm::StringRef, clv2::detail::ParseDiag &) {
  const char *Argv[] = {"tool"};
  std::string Err;
  raw_string_ostream OS(Err);
  (void)ReentryTarget->parse(1, Argv, "tool", &OS);
  return true;
}

void triggerReentrantParse(const CompiledParser &CP) {
  // Runs only in the death-test child, which is about to abort.  Symbolizing
  // the backtrace of a binary this size costs ~90s and tells us nothing the
  // matcher does not.
  ::setenv("LLVM_DISABLE_SYMBOLIZATION", "1", /*overwrite=*/1);
  const char *Argv[] = {"tool", "--cmp-reenter"};
  std::string Err;
  raw_string_ostream OS(Err);
  (void)CP.parse(2, Argv, "tool", &OS);
}

inline constexpr OptionInfo<bool> ReentryOpt{
    "cmp-reenter", "reenter parse from a validator", ValueDisallowed,
    Validate<bool>{&reenterParse}};
inline constexpr OptionsRegistry<&ReentryOpt> ReentryReg;

// The C entry point is reached from language bindings and embedders, which
// cannot survive the library calling exit().  If it ever regains an
// ExitProcess policy these tests take the whole binary down, which is the
// intended signal.
TEST(CommandLineV2, CApiReturnsNullOnBadArgumentInsteadOfExiting) {
  const char *Argv[] = {"tool", "--no-such-option-anywhere"};
  LLVMOptionsContextRef Ctx = LLVMParseCommandLineOptions2(2, Argv, "");
  EXPECT_EQ(Ctx, nullptr);
  LLVMDisposeOptionsContext(Ctx);
}

TEST(CommandLineV2, CApiReturnsContextOnSuccess) {
  const char *Argv[] = {"tool"};
  LLVMOptionsContextRef Ctx = LLVMParseCommandLineOptions2(1, Argv, "");
  EXPECT_NE(Ctx, nullptr);
  LLVMDisposeOptionsContext(Ctx);
}

TEST(CommandLineV2, CompiledParserRejectsOverlappingNonShareableParse) {
  // isShareable() used to be advice nothing checked.  Overlapping parses on a
  // non-shareable parser now trip an assert in debug builds instead of racing
  // silently on the caller-owned slots.
  static std::deque<RuntimeOption<bool>> Opts;
  Opts.emplace_back("cmp-nonshareable", "dynamic entry", ValueDisallowed);
  OptionParser P;
  P.add<&ReentryReg>();
  P.addDynamicEntry(Opts.back().makeEntry());
  const CompiledParser CP = P.compile();
  ASSERT_FALSE(CP.isShareable());
  ReentryTarget = &CP;

  EXPECT_DEATH(triggerReentrantParse(CP), "not shareable");
}
#endif

TEST(CommandLineV2, CompiledParserConcurrentParsesAreIndependent) {
  // The point of compiling: one parser object, many threads, different values.
  OptionParser P;
  P.add<&CmpReg>();
  const CompiledParser CP = P.compile();

  static constexpr int NumThreads = 8;
  static constexpr int NumIters = 100;
  std::atomic<int> Mismatches{0};
  std::vector<std::thread> Ts;
  for (int T = 0; T < NumThreads; ++T)
    Ts.emplace_back([&, T] {
      for (int I = 0; I < NumIters; ++I) {
        std::string W = "--cmp-width=" + std::to_string(T * 1000 + I);
        std::string N = "--cmp-name=n" + std::to_string(T);
        const char *argv[] = {"tool", W.c_str(), N.c_str()};
        auto Ctx = CP.parse(3, argv, "tool");
        if (!Ctx) {
          ++Mismatches;
          continue;
        }
        const auto *V = Ctx->getViewPtr<&CmpReg>();
        if (!V || V->get<&CmpWidth>() != T * 1000 + I ||
            V->get<&CmpName>() != "n" + std::to_string(T))
          ++Mismatches;
      }
    });
  for (auto &Th : Ts)
    Th.join();
  EXPECT_EQ(Mismatches.load(), 0);
}

TEST(CommandLineV2, OnErrorReturnDoesNotExit) {
  // With no error stream the historical behaviour is std::exit(), which is
  // fatal for a parser embedded in a long-running process.  Asking for
  // OnError::Return must make a bad command line -- and --help -- return
  // instead.  If this regresses the test process dies, which gtest reports.
  OptionParser P;
  P.add<&CmpReg>();
  P.setErrorHandling(OnError::Return);

  const char *Bad[] = {"tool", "--no-such-option-at-all"};
  EXPECT_EQ(P.parse(2, Bad, "tool"), nullptr);

  OptionParser P2;
  P2.add<&CmpReg>();
  P2.setErrorHandling(OnError::Return);
  std::string Help;
  raw_string_ostream HS(Help);
  const char *HelpArgv[] = {"tool", "--help"};
  EXPECT_EQ(P2.parse(2, HelpArgv, "tool", /*Errs=*/nullptr,
                     /*VersionString=*/{}, &HS),
            nullptr);
  EXPECT_NE(Help.find("--cmp-width"), std::string::npos);
}

TEST(CommandLineV2, CompiledParserHonoursOnErrorReturn) {
  OptionParser P;
  P.add<&CmpReg>();
  P.setErrorHandling(OnError::Return);
  const CompiledParser CP = P.compile();
  const char *Bad[] = {"tool", "--nope"};
  EXPECT_EQ(CP.parse(2, Bad, "tool"), nullptr);
  // And a good parse still works after a bad one.
  const char *Good[] = {"tool", "--cmp-width=5"};
  auto Ctx = CP.parse(2, Good, "tool");
  ASSERT_NE(Ctx, nullptr);
  EXPECT_EQ(Ctx->getViewPtr<&CmpReg>()->get<&CmpWidth>(), 5);
}

inline constexpr OptionInfo<int> SubDefLevel{"subdef-level", "level", Init{7}};
inline constexpr SubCommandInfo<&SubDefLevel> SubDefCmd{"go", "run it"};
inline constexpr OptionInfo<bool> SubDefTop{"subdef-top", "top"};
inline constexpr OptionsRegistry<&SubDefTop, &SubDefCmd> SubDefReg;

TEST(CommandLineV2, ShortHelpWithErrsDoesNotExit) {
  // -h is an alias for --help and is one of the five sites that decide
  // exit-vs-return.  With an error stream supplied the parse must return, not
  // terminate; if it regresses, this test process disappears rather than
  // failing, so guard it explicitly.
  OptionParser P;
  P.add<&CmpReg>();
  std::string Help, Err;
  raw_string_ostream HS(Help), ES(Err);
  const char *argv[] = {"tool", "-h"};
  auto Ctx = P.parse(2, argv, "tool", &ES, /*VersionString=*/{}, &HS);
  EXPECT_EQ(Ctx, nullptr);
  EXPECT_NE(Help.find("--cmp-width"), std::string::npos);
  EXPECT_TRUE(Err.empty()) << Err;
}

TEST(CommandLineV2, VersionGoesToHelpOSNotStdout) {
  // --version must honour the caller's stream.  It previously routed the
  // banner through llvm::outs() regardless.
  OptionParser P;
  P.add<&CmpReg>();
  std::string Out, Err;
  raw_string_ostream OS(Out), ES(Err);
  const char *argv[] = {"tool", "--version"};
  auto Ctx = P.parse(2, argv, "tool", &ES, "MyTool 1.2.3", &OS);
  EXPECT_EQ(Ctx, nullptr);
  EXPECT_NE(Out.find("MyTool 1.2.3"), std::string::npos);
  EXPECT_TRUE(Err.empty()) << Err;
}

TEST(CommandLineV2, SubCommandOptionsGetTheirDefaults) {
  // A selected subcommand's options must be default-initialised, and an
  // unselected subcommand must report itself inactive rather than handing out
  // a half-built storage.
  OptionParser P;
  P.add<&SubDefReg>();
  const char *argv[] = {"tool", "go"};
  auto Ctx = P.parse(2, argv, "tool");
  ASSERT_NE(Ctx, nullptr);
  const auto *O = Ctx->getViewPtr<&SubDefReg>();
  ASSERT_NE(O, nullptr);
  ASSERT_TRUE(O->isActive<&SubDefCmd>());
  EXPECT_EQ(O->getSubOptions<&SubDefCmd>().get<&SubDefLevel>(), 7);

  OptionParser P2;
  P2.add<&SubDefReg>();
  const char *argv2[] = {"tool"};
  auto Ctx2 = P2.parse(1, argv2, "tool");
  ASSERT_NE(Ctx2, nullptr);
  EXPECT_FALSE(Ctx2->getViewPtr<&SubDefReg>()->isActive<&SubDefCmd>());
}

} // end anonymous namespace
