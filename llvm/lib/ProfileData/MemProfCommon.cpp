//=-- MemProfCommon.cpp - MemProf common utilities ---------------=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains MemProf common utilities.
//
//===----------------------------------------------------------------------===//

#include "llvm/ProfileData/MemProfCommon.h"
#include "llvm/ProfileData/MemProf.h"
#include "llvm/ProfileData/ProfileDataOptionsOptInfos.h"
#include "llvm/Support/BLAKE3.h"
#include "llvm/Support/HashBuilder.h"
#include "llvm/Support/OptionsContext.h"

using namespace llvm;
using namespace llvm::memprof;

static float
getMemProfLifetimeAccessDensityColdThreshold(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOrDefault<
      &clv2::PD_MemProfLifetimeAccessDensityColdThreshold>(Ctx);
}

static unsigned
getMemProfAveLifetimeColdThreshold(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOrDefault<&clv2::PD_MemProfAveLifetimeColdThreshold>(
      Ctx);
}

static unsigned getMemProfMinAveLifetimeAccessDensityHotThreshold(
    const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOrDefault<
      &clv2::PD_MemProfMinAveLifetimeAccessDensityHotThreshold>(Ctx);
}

static bool getMemProfUseHotHints(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOrDefault<&clv2::PD_MemProfUseHotHints>(Ctx);
}

AllocationType llvm::memprof::getAllocType(uint64_t TotalLifetimeAccessDensity,
                                           uint64_t AllocCount,
                                           uint64_t TotalLifetime,
                                           const clv2::OptionsContext &Ctx) {
  // The access densities are multiplied by 100 to hold 2 decimal places of
  // precision, so need to divide by 100.
  if (((float)TotalLifetimeAccessDensity) / AllocCount / 100 <
          getMemProfLifetimeAccessDensityColdThreshold(Ctx)
      // Lifetime is expected to be in ms, so convert the threshold to ms.
      && ((float)TotalLifetime) / AllocCount >=
             getMemProfAveLifetimeColdThreshold(Ctx) * 1000)
    return AllocationType::Cold;

  // The access densities are multiplied by 100 to hold 2 decimal places of
  // precision, so need to divide by 100.
  if (getMemProfUseHotHints(Ctx) &&
      ((float)TotalLifetimeAccessDensity) / AllocCount / 100 >
          getMemProfMinAveLifetimeAccessDensityHotThreshold(Ctx))
    return AllocationType::Hot;

  return AllocationType::NotCold;
}

uint64_t llvm::memprof::computeFullStackId(ArrayRef<Frame> CallStack) {
  llvm::HashBuilder<llvm::TruncatedBLAKE3<8>, llvm::endianness::little>
      HashBuilder;
  for (auto &F : CallStack)
    HashBuilder.add(F.Function, F.LineOffset, F.Column);
  llvm::BLAKE3Result<8> Hash = HashBuilder.final();
  uint64_t Id;
  std::memcpy(&Id, Hash.data(), sizeof(Hash));
  return Id;
}
