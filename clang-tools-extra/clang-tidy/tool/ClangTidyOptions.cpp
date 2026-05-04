//===- ClangTidyOptions.cpp - clv2 bridge for clang-tidy options ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Bridge file that connects clv2 OptionInfo declarations from
// ClangToolsExtraOptionsOptInfos.h to the file-local statics in
// ClangTidyMain.cpp.
//
// This file sets up an OptionsContext so that tools migrating to
// OptionParser::parse() can use the constexpr option descriptors.
//
// This bridge is provided so that a future migration can use:
//
//   auto Opts = ClangTidyOptsReg.parse(argc, argv, "clang-tidy");
//   cte_opts::applyClangTidyOptions(Opts);
//
//===----------------------------------------------------------------------===//

#include "clang-tools-extra/ClangToolsExtraOptionsOptInfos.h"
#include "llvm/Support/CommandLineV2.h"

using namespace llvm;
using namespace llvm::clv2;

namespace cte_opts {

// The clang-tidy named options registry.  This groups only the options
// that belong to the clang-tidy tool.  Positional options and the
// "removed-arg" list option are handled separately by the runtime
// registration in ClangTidyMain.cpp.
inline constexpr OptionsRegistry<
    &CTE_CT_Checks, &CTE_CT_WarningsAsErrors, &CTE_CT_HeaderFilter,
    &CTE_CT_ExcludeHeaderFilter, &CTE_CT_SystemHeaders, &CTE_CT_LineFilter,
    &CTE_CT_Fix, &CTE_CT_FixErrors, &CTE_CT_FixNotes, &CTE_CT_FormatStyle,
    &CTE_CT_ListChecks, &CTE_CT_ExplainConfig, &CTE_CT_Config,
    &CTE_CT_ConfigFile, &CTE_CT_DumpConfig, &CTE_CT_EnableCheckProfile,
    &CTE_CT_StoreCheckProfile, &CTE_CT_AllowEnablingAnalyzerAlphaCheckers,
    &CTE_CT_EnableModuleHeadersParsing, &CTE_CT_ExportFixes, &CTE_CT_Quiet,
    &CTE_CT_VfsOverlay, &CTE_CT_UseColor, &CTE_CT_VerifyConfig,
    &CTE_CT_AllowNoChecks, &CTE_CT_ExperimentalCustomChecks,
    &CTE_CT_RemovedArgs>
    ClangTidyOptsReg;

// ParsedOpts type for clang-tidy.
using ParsedOpts = typename std::remove_reference_t<
    decltype(ClangTidyOptsReg)>::ParsedOptionsT;

} // namespace cte_opts
