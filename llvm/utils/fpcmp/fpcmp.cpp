//===- fpcmp.cpp - A fuzzy "cmp" that permits floating point noise --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// fpcmp is a tool that basically works like the 'cmp' tool, except that it can
// tolerate errors due to floating point noise, with the -r and -a options.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

inline constexpr clv2::OptionInfo<std::string> File1Opt{
    "", "input file #1", clv2::Positional{}, clv2::Required};
inline constexpr clv2::OptionInfo<std::string> File2Opt{
    "", "input file #2", clv2::Positional{}, clv2::Required};
inline constexpr clv2::OptionInfo<double> RelToleranceOpt{
    "r", "Relative error tolerated", clv2::Init{0.0}};
inline constexpr clv2::OptionInfo<double> AbsToleranceOpt{
    "a", "Absolute error tolerated", clv2::Init{0.0}};

static constexpr clv2::OptionsRegistry<&File1Opt, &File2Opt, &RelToleranceOpt,
                                       &AbsToleranceOpt>
    FpcmpReg;

int main(int argc, char **argv) {
  clv2::OptionParser P;
  P.add<&FpcmpReg>();
  auto OptsCtx = P.parse(argc, argv);
  auto *Opts = OptsCtx->getViewPtr<&FpcmpReg>();

  std::string ErrorMsg;
  int DF = DiffFilesWithTolerance(
      Opts->get<&File1Opt>(), Opts->get<&File2Opt>(),
      Opts->get<&AbsToleranceOpt>(), Opts->get<&RelToleranceOpt>(), &ErrorMsg);
  if (!ErrorMsg.empty())
    errs() << argv[0] << ": " << ErrorMsg << "\n";
  return DF;
}

