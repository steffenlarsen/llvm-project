//===- ConvertFromLLVMIR.cpp - MLIR to LLVM IR conversion -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the function that registers the translation between
// LLVM IR and the MLIR LLVM dialect.
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/DLTI/DLTI.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Target/LLVMIR/Dialect/All.h"
#include "mlir/Target/LLVMIR/Import.h"
#include "mlir/Target/MLIRTranslateOptionsOptInfos.h"
#include "mlir/Tools/mlir-translate/Translation.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"

using namespace mlir;

namespace mlir {
void registerFromLLVMIRTranslation() {
  TranslateToMLIRRegistration registration(
      "import-llvm", "Translate LLVMIR to MLIR",
      [](llvm::SourceMgr &sourceMgr,
         MLIRContext *context) -> OwningOpRef<Operation *> {
        llvm::SMDiagnostic err;
        llvm::LLVMContext llvmContext(llvm::clv2::defaultOptionsContext());
        std::unique_ptr<llvm::Module> llvmModule =
            llvm::parseIR(*sourceMgr.getMemoryBuffer(sourceMgr.getMainFileID()),
                          err, llvmContext);
        if (!llvmModule) {
          std::string errStr;
          llvm::raw_string_ostream errStream(errStr);
          err.print(/*ProgName=*/"", errStream);
          emitError(UnknownLoc::get(context)) << errStr;
          return {};
        }
        if (llvm::verifyModule(*llvmModule, &llvm::errs()))
          return nullptr;

        // Now that the translation supports importing debug records directly,
        // make it the default, but allow the user to override to old behavior.
        using namespace llvm::clv2;
        auto *O = mlir_translate_opts::getMLIRTranslateOptsReg(
            context->getOptionsContext());
        if (O && O->get<&MLIRT_ConvertDebugRecToIntrinsics>())
          llvmModule->convertFromNewDbgValues();

        return translateLLVMIRToModule(
            std::move(llvmModule), context,
            O ? O->get<&MLIRT_EmitExpensiveWarnings>() : false,
            O ? O->get<&MLIRT_DropDICompositeTypeElements>() : false,
            /*loadAllDialects=*/true,
            O ? O->get<&MLIRT_PreferUnregisteredIntrinsics>() : false,
            O ? O->get<&MLIRT_ImportStructsAsLiterals>() : false);
      },
      [](DialectRegistry &registry) {
        // Register the DLTI dialect used to express the data layout
        // specification of the imported module.
        registry.insert<DLTIDialect>();
        // Register all dialects that implement the LLVMImportDialectInterface
        // including the LLVM dialect.
        registerAllFromLLVMIRTranslations(registry);
      });
}
} // namespace mlir
