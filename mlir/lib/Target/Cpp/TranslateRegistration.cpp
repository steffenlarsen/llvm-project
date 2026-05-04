//===- TranslateRegistration.cpp - Register translation -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Target/Cpp/CppEmitter.h"
#include "mlir/Target/MLIRTranslateOptionsOptInfos.h"
#include "mlir/Tools/mlir-translate/Translation.h"

using namespace mlir;

namespace mlir {

void registerToCppTranslation() {
  TranslateFromMLIRRegistration reg(
      "mlir-to-cpp", "translate from mlir to cpp",
      [](Operation *op, raw_ostream &output) {
        bool declareVarsAtTop = false;
        std::string fid;
        if (auto *O = mlir_translate_opts::getMLIRTranslateOptsReg(
                op->getContext()->getOptionsContext())) {
          using namespace llvm::clv2;
          declareVarsAtTop = O->get<&MLIRT_DeclareVariablesAtTop>();
          fid = O->get<&MLIRT_FileId>();
        }
        return emitc::translateToCpp(op, output, declareVarsAtTop, fid);
      },
      [](DialectRegistry &registry) {
        // clang-format off
        registry.insert<cf::ControlFlowDialect,
                        emitc::EmitCDialect,
                        func::FuncDialect>();
        // clang-format on
      });
}

} // namespace mlir
