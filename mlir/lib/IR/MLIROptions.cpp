//===- MLIROptions.cpp - MLIR core library option bridge
//-------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Bridge between clv2 OptionInfo declarations in MLIROptionsOptInfos.h and
// the runtime option registry.
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/MLIROptionsOptInfos.h"
#include "llvm/Support/OptionsContext.h"
using namespace llvm;
using namespace llvm::clv2;

const mlir::mlir_opts::MLIROptsRegOpts *
mlir::mlir_opts::getMLIROptsReg(const clv2::OptionsContext &Ctx) {
  return Ctx.getViewPtr<&MLIROptsReg>();
}
