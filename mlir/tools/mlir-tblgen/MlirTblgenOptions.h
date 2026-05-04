//===- MlirTblgenOptions.h - Constexpr option descriptors -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Compile-time option descriptors for all mlir-tblgen backend options.
// Each backend file declares its option storage as a non-static global;
// the bridge function applyMlirTblgenOptions() copies parsed values into
// those globals after OptionsRegistry::parse().
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_TOOLS_MLIR_TBLGEN_OPTIONS_H
#define MLIR_TOOLS_MLIR_TBLGEN_OPTIONS_H

#include "llvm/Support/CommandLineV2.h"

namespace mlir {
namespace tblgen_opts {
using namespace llvm::clv2;

// ---- AttrOrTypeDefGen.cpp ----
inline constexpr OptionInfo<std::string> AttrdefsDialect{
    "attrdefs-dialect", "Generate attributes for this dialect"};
inline constexpr OptionInfo<std::string> TypedefsDialect{
    "typedefs-dialect", "Generate types for this dialect"};

// ---- BytecodeDialectGen.cpp ----
inline constexpr OptionInfo<std::string> BytecodeDialect{
    "bytecode-dialect", "The dialect to gen for"};

// ---- DialectGen.cpp ----
inline constexpr OptionInfo<std::string> Dialect{"dialect",
                                                 "The dialect to gen for"};

// ---- DirectiveCommonGen.cpp ----
inline constexpr OptionInfo<std::string> DirectivesDialect{
    "directives-dialect", "Generate directives for this dialect"};

// ---- FormatGen.cpp ----
inline constexpr OptionInfo<bool> AsmformatErrorIsFatal{
    "asmformat-error-is-fatal", "Emit a fatal error if format parsing fails",
    Init{true}};

// ---- LLVMIRIntrinsicGen.cpp ----
inline constexpr OptionInfo<std::string> LlvmirIntrinsicsFilter{
    "llvmir-intrinsics-filter", "Only keep the intrinsics with the specified "
                                "substring in their record name"};
inline constexpr OptionInfo<std::string> DialectOpclassBase{
    "dialect-opclass-base",
    "The base class for the ops in the dialect we "
    "are planning to emit",
    Init{"LLVM_IntrOp"}};
inline constexpr OptionInfo<std::string> LlvmirIntrinsicsAccessGroupRegexp{
    "llvmir-intrinsics-access-group-regexp",
    "Mark intrinsics that match the specified "
    "regexp as taking an access group metadata"};
inline constexpr OptionInfo<std::string> LlvmirIntrinsicsAliasAnalysisRegexp{
    "llvmir-intrinsics-alias-analysis-regexp",
    "Mark intrinsics that match the specified "
    "regexp as taking alias.scopes, noalias, and tbaa metadata"};

// ---- OpDocGen.cpp ----
inline constexpr OptionInfo<std::string> StripPrefix{
    "strip-prefix", "Strip prefix of the fully qualified names",
    Init{"::mlir::"}};
inline constexpr OptionInfo<bool> AllowHugoSpecificFeatures{
    "allow-hugo-specific-features", "Allows using features specific to Hugo"};
inline constexpr OptionInfo<bool> KeepOpSourceOrder{
    "keep-op-source-order", "Do not sort ops alphabetically"};

// ---- OpGenHelpers.cpp ----
inline constexpr OptionInfo<std::string> OpIncludeRegex{
    "op-include-regex",
    "Regex of name of op's to include (no filter if empty)"};
inline constexpr OptionInfo<std::string> OpExcludeRegex{
    "op-exclude-regex",
    "Regex of name of op's to exclude (no filter if empty)"};
inline constexpr OptionInfo<unsigned> OpShardCount{
    "op-shard-count",
    "The number of shards into which the op classes will be divided", Init{1u}};

// ---- OpPythonBindingGen.cpp ----
inline constexpr OptionInfo<std::string> BindDialect{
    "bind-dialect", "The dialect to run the generator for"};
inline constexpr OptionInfo<std::string> DialectExtension{
    "dialect-extension", "The prefix of the dialect extension"};

// ---- PassCAPIGen.cpp ----
inline constexpr OptionInfo<std::string> Prefix{
    "prefix", "The prefix to use for this group of passes. The "
              "form will be mlirCreate<prefix><passname>, the "
              "prefix can avoid conflicts across libraries."};

// ---- PassGen.cpp ----
inline constexpr OptionInfo<std::string> Name{
    "name", "The name of this group of passes"};

// ---- Single registry for all mlir-tblgen backend options ----
inline constexpr OptionsRegistry<
    &AttrdefsDialect, &TypedefsDialect, &BytecodeDialect, &Dialect,
    &DirectivesDialect, &AsmformatErrorIsFatal, &LlvmirIntrinsicsFilter,
    &DialectOpclassBase, &LlvmirIntrinsicsAccessGroupRegexp,
    &LlvmirIntrinsicsAliasAnalysisRegexp, &StripPrefix,
    &AllowHugoSpecificFeatures, &KeepOpSourceOrder, &OpIncludeRegex,
    &OpExcludeRegex, &OpShardCount, &BindDialect, &DialectExtension, &Prefix,
    &Name>
    MlirTblgenOptsReg;

using ParsedOpts = decltype(MlirTblgenOptsReg)::ParsedOptionsT;

/// Bridge function: copies parsed option values into the backend globals.
void applyMlirTblgenOptions(const ParsedOpts &Opts);

} // namespace tblgen_opts
} // namespace mlir

#endif // MLIR_TOOLS_MLIR_TBLGEN_OPTIONS_H
