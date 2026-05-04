//===-- Inliner.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "flang/Common/FlangOptionsOptInfos.h"
#include "flang/Optimizer/Dialect/FIRDialect.h"
#include "mlir/IR/Operation.h"
#include "llvm/Support/OptionsContext.h"

/// Should we inline the callable `op` into region `reg`?
bool fir::canLegallyInline(mlir::Operation *op, mlir::Region *, bool,
                           mlir::IRMapping &) {
  auto &optsCtx = op->getContext()->getOptionsContext();
  return llvm::clv2::getOptValOrDefault<&llvm::clv2::FLANG_InlineAll>(optsCtx);
}

bool fir::canLegallyInline(mlir::Operation *op, mlir::Operation *, bool) {
  auto &optsCtx = op->getContext()->getOptionsContext();
  return llvm::clv2::getOptValOrDefault<&llvm::clv2::FLANG_InlineAll>(optsCtx);
}
