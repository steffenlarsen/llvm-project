//===- SandboxVectorizer.cpp - Vectorizer based on Sandbox IR -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Vectorize/SandboxVectorizer/SandboxVectorizer.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/SandboxIR/Constant.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/Regex.h"
#include "llvm/Transforms/Vectorize/SandboxVectorizer/Debug.h"
#include "llvm/Transforms/Vectorize/SandboxVectorizer/SandboxVectorizerPassBuilder.h"
#include "llvm/Transforms/Vectorize/VectorizeOptions.h"

using namespace llvm;

static bool getPrintPassPipeline(const Function &F) {
  return clv2::getOptValOrDefault<&clv2::VEC_PrintPassPipeline>(
      F.getContext().getOptionsContext());
}

/// A magic string for the default pass pipeline.
static const char *DefaultPipelineMagicStr = "*";

static std::string getUserDefinedPassPipeline(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValIfSpecified<&clv2::VectorizeOptsReg,
                                    &clv2::VEC_UserDefinedPassPipeline>(
      Ctx, std::string(DefaultPipelineMagicStr));
}

// This option is useful for bisection debugging.
static std::string getAllowFiles(const Function &F) {
  return clv2::getOptValIfSpecified<&clv2::VectorizeOptsReg,
                                    &clv2::VEC_AllowFiles>(
      F.getContext().getOptionsContext(), ".*");
}
static constexpr char AllowFilesDelim = ',';

SandboxVectorizerPass::SandboxVectorizerPass()
    : FPM("fpm"), PipelineInitialized(false) {}

void SandboxVectorizerPass::initPipeline(const Function &F) {
  if (PipelineInitialized)
    return;
  PipelineInitialized = true;
  std::string Pipeline =
      getUserDefinedPassPipeline(F.getContext().getOptionsContext());
  if (Pipeline == DefaultPipelineMagicStr) {
    FPM.setPassPipeline(
        "seed-collection<tr-save,bundle-vec(bottom-up),load-store-vec,"
        "tr-accept-or-revert>",
        sandboxir::SandboxVectorizerPassBuilder::createFunctionPass);
  } else {
    FPM.setPassPipeline(
        Pipeline, sandboxir::SandboxVectorizerPassBuilder::createFunctionPass);
  }
}

SandboxVectorizerPass::SandboxVectorizerPass(SandboxVectorizerPass &&) =
    default;

SandboxVectorizerPass::~SandboxVectorizerPass() = default;

PreservedAnalyses SandboxVectorizerPass::run(Function &F,
                                             FunctionAnalysisManager &AM) {
  TTI = &AM.getResult<TargetIRAnalysis>(F);
  AA = &AM.getResult<AAManager>(F);
  SE = &AM.getResult<ScalarEvolutionAnalysis>(F);

  bool Changed = runImpl(F);
  if (!Changed)
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}

bool SandboxVectorizerPass::allowFile(const std::string &SrcFilePath,
                                      const Function &LLVMF) {
  // Iterate over all files in AllowFiles separated by `AllowFilesDelim`.
  std::string AllowFilesVal = getAllowFiles(LLVMF);
  size_t DelimPos = 0;
  do {
    size_t LastPos = DelimPos != 0 ? DelimPos + 1 : DelimPos;
    DelimPos = AllowFilesVal.find(AllowFilesDelim, LastPos);
    auto FileNameToMatch = AllowFilesVal.substr(LastPos, DelimPos - LastPos);
    if (FileNameToMatch.empty())
      return false;
    // Note: This only runs when debugging so its OK not to reuse the regex.
    Regex FileNameRegex(".*" + FileNameToMatch + "$");
    assert(FileNameRegex.isValid() && "Bad regex!");
    if (FileNameRegex.match(SrcFilePath))
      return true;
  } while (DelimPos != std::string::npos);
  return false;
}

bool SandboxVectorizerPass::runImpl(Function &LLVMF) {
  initPipeline(LLVMF);
  if (Ctx == nullptr)
    Ctx = std::make_unique<sandboxir::Context>(LLVMF.getContext());

  if (getPrintPassPipeline(LLVMF)) {
    FPM.printPipeline(outs());
    return false;
  }

  // This is used for debugging.
  if (LLVM_UNLIKELY(getAllowFiles(LLVMF) != ".*")) {
    const auto &SrcFilePath = LLVMF.getParent()->getSourceFileName();
    if (!allowFile(SrcFilePath, LLVMF))
      return false;
  }

  // If the target claims to have no vector registers early return.
  if (!TTI->getNumberOfRegisters(TTI->getRegisterClassForType(true))) {
    LLVM_DEBUG(dbgs() << DEBUG_PREFIX
                      << "Target has no vector registers, return.\n");
    return false;
  }
  LLVM_DEBUG(dbgs() << DEBUG_PREFIX << "Analyzing " << LLVMF.getName()
                    << ".\n");
  // Early return if the attribute NoImplicitFloat is used.
  if (LLVMF.hasFnAttribute(Attribute::NoImplicitFloat)) {
    LLVM_DEBUG(dbgs() << DEBUG_PREFIX
                      << "NoImplicitFloat attribute, return.\n");
    return false;
  }

  // Create SandboxIR for LLVMF and run BundleVec on it.
  sandboxir::Function &F = *Ctx->createFunction(&LLVMF);
  sandboxir::Analyses A(*AA, *SE, *TTI);
  bool Change = FPM.runOnFunction(F, A);
  // Given that sandboxir::Context `Ctx` is defined at a pass-level scope, the
  // maps from LLVM IR to Sandbox IR may go stale as later passes remove LLVM IR
  // objects. To avoid issues caused by this clear the context's state.
  // NOTE: The alternative would be to define Ctx and FPM within runOnFunction()
  Ctx->clear();
  return Change;
}
