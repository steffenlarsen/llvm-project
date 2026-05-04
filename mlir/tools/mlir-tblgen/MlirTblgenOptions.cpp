//===- MlirTblgenOptions.cpp - Bridge parsed opts to backend globals ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MlirTblgenOptions.h"

// Extern declarations for the backend storage variables.
// Each backend .cpp file owns the definition; we just write into it here.

// AttrOrTypeDefGen.cpp
extern std::string attrDialect;
extern std::string typeDialect;

// BytecodeDialectGen.cpp
extern std::string selectedBcDialect;

// DialectGen.cpp
extern std::string selectedDialect;

// DirectiveCommonGen.cpp
extern std::string directivesDialect;

// FormatGen.cpp  (defined as mlir::tblgen::formatErrorIsFatal)
namespace mlir {
namespace tblgen {
extern bool formatErrorIsFatal;
} // namespace tblgen
} // namespace mlir

// LLVMIRIntrinsicGen.cpp
extern std::string nameFilter;
extern std::string opBaseClass;
extern std::string accessGroupRegexp;
extern std::string aliasAnalysisRegexp;

// OpDocGen.cpp
extern std::string stripPrefix;
extern bool allowHugoSpecificFeatures;
extern bool keepOpSourceOrder;

// OpGenHelpers.cpp
extern std::string opIncFilter;
extern std::string opExcFilter;
extern unsigned opShardCount;

// OpPythonBindingGen.cpp
extern std::string dialectNameStorage;
extern std::string clDialectExtensionName;

// PassCAPIGen.cpp
extern std::string groupNamePassCAPI;

// PassGen.cpp
extern std::string groupNamePassGen;

namespace mlir {
namespace tblgen_opts {

void applyMlirTblgenOptions(const ParsedOpts &Opts) {
  // AttrOrTypeDefGen.cpp
  attrDialect = Opts.get<&AttrdefsDialect>();
  typeDialect = Opts.get<&TypedefsDialect>();

  // BytecodeDialectGen.cpp
  selectedBcDialect = Opts.get<&BytecodeDialect>();

  // DialectGen.cpp
  selectedDialect = Opts.get<&Dialect>();

  // DirectiveCommonGen.cpp
  directivesDialect = Opts.get<&DirectivesDialect>();

  // FormatGen.cpp
  mlir::tblgen::formatErrorIsFatal = Opts.get<&AsmformatErrorIsFatal>();

  // LLVMIRIntrinsicGen.cpp
  nameFilter = Opts.get<&LlvmirIntrinsicsFilter>();
  opBaseClass = Opts.get<&DialectOpclassBase>();
  accessGroupRegexp = Opts.get<&LlvmirIntrinsicsAccessGroupRegexp>();
  aliasAnalysisRegexp = Opts.get<&LlvmirIntrinsicsAliasAnalysisRegexp>();

  // OpDocGen.cpp
  stripPrefix = Opts.get<&StripPrefix>();
  allowHugoSpecificFeatures = Opts.get<&AllowHugoSpecificFeatures>();
  keepOpSourceOrder = Opts.get<&KeepOpSourceOrder>();

  // OpGenHelpers.cpp
  opIncFilter = Opts.get<&OpIncludeRegex>();
  opExcFilter = Opts.get<&OpExcludeRegex>();
  opShardCount = Opts.get<&OpShardCount>();

  // OpPythonBindingGen.cpp
  dialectNameStorage = Opts.get<&BindDialect>();
  clDialectExtensionName = Opts.get<&DialectExtension>();

  // PassCAPIGen.cpp
  groupNamePassCAPI = Opts.get<&Prefix>();

  // PassGen.cpp
  groupNamePassGen = Opts.get<&Name>();
}

} // namespace tblgen_opts
} // namespace mlir
