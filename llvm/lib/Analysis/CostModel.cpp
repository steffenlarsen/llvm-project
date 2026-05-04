//===- CostModel.cpp ------ Cost Model Analysis ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the cost model analysis. It provides a very basic cost
// estimation for LLVM-IR. This analysis uses the services of the codegen
// to approximate the cost of any IR instruction when lowered to machine
// instructions. The cost results are unit-less and the cost number represents
// the throughput of the machine assuming that all loads hit the cache, all
// branches are predicted, etc. The cost numbers can be added in order to
// compare two or more transformation alternatives.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/CostModel.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

using CostKind = CostModelPrintOptions::CostKind;
using IntrinsicCostStrategy = CostModelPrintOptions::IntrinsicCostStrategy;

#define CM_NAME "cost-model"
#define DEBUG_TYPE CM_NAME

static InstructionCost getCost(Instruction &Inst, TTI::TargetCostKind Kind,
                               IntrinsicCostStrategy Strategy,
                               TargetTransformInfo &TTI) {
  auto *II = dyn_cast<IntrinsicInst>(&Inst);
  if (II && Strategy != IntrinsicCostStrategy::InstructionCost) {
    IntrinsicCostAttributes ICA(
        II->getIntrinsicID(), *II, InstructionCost::getInvalid(),
        /*TypeBasedOnly=*/Strategy ==
            IntrinsicCostStrategy::TypeBasedIntrinsicCost);
    return TTI.getIntrinsicInstrCost(ICA, Kind);
  }

  return TTI.getInstructionCost(&Inst, Kind);
}

static TTI::TargetCostKind toTargetCostKind(CostKind Kind) {
  switch (Kind) {
  case CostKind::RecipThroughput:
    return TTI::TCK_RecipThroughput;
  case CostKind::Latency:
    return TTI::TCK_Latency;
  case CostKind::CodeSize:
    return TTI::TCK_CodeSize;
  case CostKind::SizeAndLatency:
    return TTI::TCK_SizeAndLatency;
  default:
    llvm_unreachable("Unexpected CostKind!");
  };
}

PreservedAnalyses CostModelPrinterPass::run(Function &F,
                                            FunctionAnalysisManager &AM) {
  auto &TTI = AM.getResult<TargetIRAnalysis>(F);
  OS << "Printing analysis 'Cost Model Analysis' for function '" << F.getName() << "':\n";
  for (BasicBlock &B : F) {
    for (Instruction &Inst : B) {
      OS << "Cost Model: ";
      if (Options.Kind == CostKind::All) {
        OS << "Found costs of ";
        InstructionCost RThru = getCost(Inst, TTI::TCK_RecipThroughput,
                                        Options.IntrinsicStrategy, TTI);
        InstructionCost CodeSize =
            getCost(Inst, TTI::TCK_CodeSize, Options.IntrinsicStrategy, TTI);
        InstructionCost Lat =
            getCost(Inst, TTI::TCK_Latency, Options.IntrinsicStrategy, TTI);
        InstructionCost SizeLat = getCost(Inst, TTI::TCK_SizeAndLatency,
                                          Options.IntrinsicStrategy, TTI);
        if (RThru == CodeSize && RThru == Lat && RThru == SizeLat)
          OS << RThru;
        else
          OS << "RThru:" << RThru << " CodeSize:" << CodeSize << " Lat:" << Lat
             << " SizeLat:" << SizeLat;
        OS << " for: " << Inst << "\n";
      } else {
        InstructionCost Cost = getCost(Inst, toTargetCostKind(Options.Kind),
                                       Options.IntrinsicStrategy, TTI);
        if (Cost.isValid())
          OS << "Found an estimated cost of " << Cost.getValue();
        else
          OS << "Invalid cost";
        OS << " for instruction: " << Inst << "\n";
      }
    }
  }
  return PreservedAnalyses::all();
}
