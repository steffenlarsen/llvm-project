//===- CostModel.h - --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_COSTMODEL_H
#define LLVM_ANALYSIS_COSTMODEL_H

#include "llvm/IR/PassManager.h"

namespace llvm {

struct CostModelPrintOptions {
  enum class CostKind {
    RecipThroughput,
    Latency,
    CodeSize,
    SizeAndLatency,
    All,
  };
  enum class IntrinsicCostStrategy {
    InstructionCost,
    IntrinsicCost,
    TypeBasedIntrinsicCost,
  };

  CostKind Kind = CostKind::RecipThroughput;
  IntrinsicCostStrategy IntrinsicStrategy =
      IntrinsicCostStrategy::InstructionCost;
};

/// Printer pass for cost modeling results.
class CostModelPrinterPass
    : public RequiredPassInfoMixin<CostModelPrinterPass> {
  raw_ostream &OS;
  CostModelPrintOptions Options;

public:
  explicit CostModelPrinterPass(raw_ostream &OS,
                                CostModelPrintOptions Options = {})
      : OS(OS), Options(Options) {}

  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // end namespace llvm

#endif // LLVM_ANALYSIS_COSTMODEL_H
