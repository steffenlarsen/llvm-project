//===- MLIRTranslateOptionsOptInfos.h - clv2 OptionInfo for translations
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// clv2 OptionInfo declarations for MLIR translation/target backend options.
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_TARGET_MLIRTRANSLATEOPTIONSOPTINFOS_H
#define MLIR_TARGET_MLIRTRANSLATEOPTIONSOPTINFOS_H

#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"

#define CLV2_OPTIONS_DECL
#include "mlir/Target/MLIRTranslateOptionsOptInfos.inc"
#undef CLV2_OPTIONS_DECL

namespace llvm::clv2 {
class OptionsContext;
}

namespace mlir::mlir_translate_opts {

using MLIRTranslateOptsRegOpts =
    decltype(llvm::clv2::MLIRTranslateOptsReg)::ParsedOptionsT;

LLVM_ABI const MLIRTranslateOptsRegOpts *
getMLIRTranslateOptsReg(const llvm::clv2::OptionsContext &Ctx);

} // namespace mlir::mlir_translate_opts

#define CLV2_OPTIONS_GETTERS
#include "mlir/Target/MLIRTranslateOptionsOptInfos.inc"
#undef CLV2_OPTIONS_GETTERS

#endif // MLIR_TARGET_MLIRTRANSLATEOPTIONSOPTINFOS_H
