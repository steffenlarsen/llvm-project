//===-- llvm-mc-disassemble-fuzzer.cpp - Fuzzer for the MC layer ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "llvm-c/Disassembler.h"
#include "llvm-c/Target.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/SubtargetFeature.h"

using namespace llvm;
using namespace llvm::clv2;

const unsigned AssemblyTextBufSize = 80;

inline constexpr OptionInfo<std::string> TripleNameOpt{
    "triple",
    "Target triple to assemble for, see -version for available targets"};

inline constexpr OptionInfo<std::string> MCPUOpt{
    "mcpu", "Target a specific cpu type (-mcpu=help for details)",
    value_desc("cpu-name")};

inline constexpr OptionInfo<unsigned> InsnLimitOpt{
    "insn-limit",
    "Limit the number of instructions to process (0 for no limit)",
    value_desc("count"), Init{0u}};

inline constexpr ListOptionInfo<std::string> MAttrsOpt{
    "mattr", "Target specific attributes (-mattr=help for details)",
    CommaSeparated, value_desc("a1,+a2,-a3,...")};

inline constexpr ListOptionInfo<std::string> FuzzerArgsOpt{
    "fuzzer-args", "Options to pass to the fuzzer", Positional{},
    PositionalEatsArgs};

// Parsed values — populated after parsing.
static std::string TripleName;
static std::string MCPU;
static unsigned InsnLimit = 0;
static std::vector<std::string> MAttrs;
static std::vector<std::string> FuzzerArgs;
std::string FeaturesStr;
static std::vector<char *> ModifiedArgv;

int DisassembleOneInput(const uint8_t *Data, size_t Size) {
  char AssemblyText[AssemblyTextBufSize];

  std::vector<uint8_t> DataCopy(Data, Data + Size);

  LLVMDisasmContextRef Ctx = LLVMCreateDisasmCPUFeatures(
      TripleName.c_str(), MCPU.c_str(), FeaturesStr.c_str(), nullptr, 0,
      nullptr, nullptr);
  assert(Ctx);
  uint8_t *p = DataCopy.data();
  unsigned Consumed;
  unsigned InstructionsProcessed = 0;
  do {
    Consumed = LLVMDisasmInstruction(Ctx, p, Size, 0, AssemblyText,
                                     AssemblyTextBufSize);
    Size -= Consumed;
    p += Consumed;

    InstructionsProcessed ++;
    if (InsnLimit != 0 && InstructionsProcessed < InsnLimit)
      break;
  } while (Consumed != 0);
  LLVMDisasmDispose(Ctx);
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  return DisassembleOneInput(Data, Size);
}

extern "C" LLVM_ATTRIBUTE_USED int LLVMFuzzerInitialize(int *argc,
                                                        char ***argv) {
  // The command line is unusual compared to other fuzzers due to the need to
  // specify the target. Options like -triple, -mcpu, and -mattr work like
  // their counterparts in llvm-mc, while -fuzzer-args collects options for the
  // fuzzer itself.
  //
  // Examples:
  //
  // Fuzz the big-endian MIPS32R6 disassembler using 100,000 inputs of up to
  // 4-bytes each and use the contents of ./corpus as the test corpus:
  //   llvm-mc-fuzzer -triple mips-linux-gnu -mcpu=mips32r6 -disassemble \
  //       -fuzzer-args -max_len=4 -runs=100000 ./corpus
  //
  // Infinitely fuzz the little-endian MIPS64R2 disassembler with the MSA
  // feature enabled using up to 64-byte inputs:
  //   llvm-mc-fuzzer -triple mipsel-linux-gnu -mcpu=mips64r2 -mattr=msa \
  //       -disassemble -fuzzer-args ./corpus
  //
  // If your aim is to find instructions that are not tested, then it is
  // advisable to constrain the maximum input size to a single instruction
  // using -max_len as in the first example. This results in a test corpus of
  // individual instructions that test unique paths. Without this constraint,
  // there will be considerable redundancy in the corpus.

  char **OriginalArgv = *argv;

  LLVMInitializeAllTargetInfos();
  LLVMInitializeAllTargetMCs();
  LLVMInitializeAllDisassemblers();

  static constexpr OptionsRegistry<&TripleNameOpt, &MCPUOpt, &InsnLimitOpt,
                                   &MAttrsOpt, &FuzzerArgsOpt>
      FuzzerToolReg;
  clv2::OptionParser P;
  P.add<&FuzzerToolReg>();
  RegisterAllLLVMOptions(P);
  auto OptsCtx = P.parse(*argc, OriginalArgv, "llvm MC disassembler fuzzer\n");
  auto *Opts = OptsCtx->getViewPtr<&FuzzerToolReg>();
  TripleName = Opts->get<&TripleNameOpt>();
  MCPU = Opts->get<&MCPUOpt>();
  InsnLimit = Opts->get<&InsnLimitOpt>();
  FuzzerArgs = Opts->get<&FuzzerArgsOpt>();
  MAttrs = Opts->get<&MAttrsOpt>();

  // Rebuild the argv without the arguments llvm-mc-fuzzer consumed so that
  // the driver can parse its arguments.
  //
  // FuzzerArgs cannot provide the non-const pointer that OriginalArgv needs.
  // Re-use the strings from OriginalArgv instead of copying FuzzerArg to a
  // non-const buffer to avoid the need to clean up when the fuzzer terminates.
  ModifiedArgv.push_back(OriginalArgv[0]);
  for (const auto &FuzzerArg : FuzzerArgs) {
    for (int i = 1; i < *argc; ++i) {
      if (FuzzerArg == OriginalArgv[i])
        ModifiedArgv.push_back(OriginalArgv[i]);
    }
  }
  *argc = ModifiedArgv.size();
  *argv = ModifiedArgv.data();

  // Package up features to be passed to target/subtarget
  // We have to pass it via a global since the callback doesn't
  // permit any user data.
  if (MAttrs.size()) {
    SubtargetFeatures Features;
    for (unsigned i = 0; i != MAttrs.size(); ++i)
      Features.AddFeature(MAttrs[i]);
    FeaturesStr = Features.getString();
  }

  if (TripleName.empty())
    TripleName = sys::getDefaultTargetTriple();

  return 0;
}
