//===- ExpandVariadics.h - expand variadic functions ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_IPO_EXPANDVARIADICS_H
#define LLVM_TRANSFORMS_IPO_EXPANDVARIADICS_H

#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/IPO/ExpandVariadicsMode.h"

namespace llvm {

class Module;
class ModulePass;

class ExpandVariadicsPass : public RequiredPassInfoMixin<ExpandVariadicsPass> {
  const ExpandVariadicsMode Mode;

public:
  // Operates under passed mode unless overridden on commandline
  LLVM_ABI ExpandVariadicsPass(ExpandVariadicsMode Mode);

  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

LLVM_ABI ModulePass *createExpandVariadicsPass(ExpandVariadicsMode);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_EXPANDVARIADICS_H
