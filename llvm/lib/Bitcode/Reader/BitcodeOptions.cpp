//===- BitcodeOptions.cpp - Bitcode option bridge -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Bitcode/BitcodeOptionsOptInfos.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/OptionsContext.h"

using namespace llvm;
using namespace llvm::clv2;

bool llvm::getCombinedIndexMemProfContextEnabled(
    const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOrDefault<&clv2::BC_CombinedIndexMemProfContext>(Ctx);
}
