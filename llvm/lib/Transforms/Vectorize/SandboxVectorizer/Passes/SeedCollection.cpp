//===- SeedCollection.cpp - Seed collection pass --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Vectorize/SandboxVectorizer/Passes/SeedCollection.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/SandboxIR/Function.h"
#include "llvm/SandboxIR/Module.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Transforms/Vectorize/SandboxVectorizer/RegionWithScore.h"
#include "llvm/Transforms/Vectorize/SandboxVectorizer/SandboxVectorizerPassBuilder.h"
#include "llvm/Transforms/Vectorize/SandboxVectorizer/SeedCollector.h"
#include "llvm/Transforms/Vectorize/SandboxVectorizer/VecUtils.h"
#include "llvm/Transforms/Vectorize/VectorizeOptions.h"

namespace llvm {

static unsigned getOverrideVecRegBits(const llvm::Function &F) {
  return clv2::getOptValOrDefault<&clv2::VEC_OverrideVecRegBits>(
      F.getContext().getOptionsContext());
}

static bool getAllowNonPow2(const llvm::Function &F) {
  return clv2::getOptValOrDefault<&clv2::VEC_AllowNonPow2>(
      F.getContext().getOptionsContext());
}

#define LoadSeedsDef "loads"
#define StoreSeedsDef "stores"
static std::string getCollectSeeds(const llvm::Function &F) {
  return clv2::getOptValIfSpecified<&clv2::VectorizeOptsReg,
                                    &clv2::VEC_CollectSeeds>(
      F.getContext().getOptionsContext(), StoreSeedsDef);
}

namespace sandboxir {

SeedCollection::SeedCollection(StringRef Pipeline, StringRef AuxArg)
    : FunctionPass("seed-collection"),
      RPM("rpm", Pipeline, SandboxVectorizerPassBuilder::createRegionPass) {
  if (!AuxArg.empty()) {
    if (AuxArg != DiffTypesArgStr) {
      std::string ErrStr;
      raw_string_ostream ErrSS(ErrStr);
      ErrSS << "SeedCollection only supports '" << DiffTypesArgStr
            << "' aux argument!\n";
      reportFatalUsageError(ErrStr.c_str());
    }
    AllowDiffTypes = true;
  }
}

bool SeedCollection::runOnFunction(Function &F, const Analyses &A) {
  bool Change = false;
  const auto &LLVMF = F.getLLVMFunction();
  const auto &DL = F.getParent()->getDataLayout();
  bool CollectStores =
      getCollectSeeds(LLVMF).find(StoreSeedsDef) != std::string::npos;
  bool CollectLoads =
      getCollectSeeds(LLVMF).find(LoadSeedsDef) != std::string::npos;

  // TODO: Start from innermost BBs first
  for (auto &BB : F) {
    SeedCollector SC(&BB, A.getScalarEvolution(), CollectStores, CollectLoads,
                     AllowDiffTypes);
    for (auto &SeedRange : {SC.getStoreSeeds(), SC.getLoadSeeds()}) {
      for (SeedBundle &Seeds : SeedRange) {
        if (Seeds.allUsed())
          continue;
        unsigned FirstUnusedIdx = Seeds.getFirstUnusedElementIdx();
        unsigned ElmBits =
            Utils::getNumBits(VecUtils::getElementType(Utils::getExpectedType(
                                  Seeds[FirstUnusedIdx])),
                              DL);
        unsigned AS = getLoadStoreAddressSpace(Seeds[FirstUnusedIdx]);
        unsigned VecRegBits = getOverrideVecRegBits(LLVMF) != 0
                                  ? getOverrideVecRegBits(LLVMF)
                                  : A.getTTI().getLoadStoreVecRegBitWidth(AS);

        auto DivideBy2 = [](unsigned Num) {
          auto Floor = VecUtils::getFloorPowerOf2(Num);
          if (Floor == Num)
            return Floor / 2;
          return Floor;
        };
        // Try to create the largest vector supported by the target. If it fails
        // reduce the vector size by half.
        for (unsigned SliceElms = std::min(VecRegBits / ElmBits,
                                           Seeds.getNumUnusedBits() / ElmBits);
             SliceElms >= 2u; SliceElms = DivideBy2(SliceElms)) {
          if (Seeds.allUsed())
            break;
          // Keep trying offsets after FirstUnusedElementIdx, until we vectorize
          // the slice. This could be quite expensive, so we enforce a limit.
          for (unsigned Offset = Seeds.getFirstUnusedElementIdx(),
                        OE = Seeds.size();
               Offset + 1 < OE; Offset += 1) {
            // Seeds are getting used as we vectorize, so skip them.
            if (Seeds.isUsed(Offset))
              continue;
            if (Seeds.allUsed())
              break;

            auto SeedSlice = Seeds.getSlice(Offset, SliceElms * ElmBits,
                                            !getAllowNonPow2(LLVMF));
            if (SeedSlice.empty())
              continue;

            assert(SeedSlice.size() >= 2 && "Should have been rejected!");

            // Create a region containing the seed slice.
            auto &Ctx = F.getContext();
            RegionWithScore Rgn(Ctx, A.getTTI());
            Rgn.setAux(SeedSlice);
            // Run the region pass pipeline.
            Change |= RPM.runOnRegion(Rgn, A);
            Rgn.clearAux();
          }
        }
      }
    }
  }
  return Change;
}
} // namespace sandboxir
} // namespace llvm
