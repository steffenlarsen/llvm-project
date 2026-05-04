//===-- TableGen.cpp - Top-Level TableGen implementation for Clang --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the main function for Clang's TableGen.
//
//===----------------------------------------------------------------------===//

#include "ASTTableGen.h"
#include "TableGenBackends.h" // Declares all backends.
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/TableGen/Main.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <deque>

using namespace llvm;
using namespace clang;

enum ActionType {
  PrintRecords,
  DumpJSON,
  GenCIRLowering,
  GenClangAttrClasses,
  GenClangAttrParserStringSwitches,
  GenClangAttrSubjectMatchRulesParserStringSwitches,
  GenClangAttrImpl,
  GenClangAttrList,
  GenClangAttrDocTable,
  GenClangAttrSubjectMatchRuleList,
  GenClangAttrPCHRead,
  GenClangAttrPCHWrite,
  GenClangRegularKeywordAttributeInfo,
  GenClangAttrHasAttributeImpl,
  GenClangAttrSpellingListIndex,
  GenClangAttrASTVisitor,
  GenClangAttrTemplateInstantiate,
  GenClangAttrParsedAttrList,
  GenClangAttrParsedAttrImpl,
  GenClangAttrParsedAttrKinds,
  GenClangAttrIsTypeDependent,
  GenClangAttrTextNodeDump,
  GenClangAttrNodeTraverse,
  GenClangAttrUndocumentedAttrList,
  GenClangBasicReader,
  GenClangBasicWriter,
  GenClangBuiltins,
  GenClangBuiltinTemplates,
  GenClangDiagsCompatIDs,
  GenClangDiagsDefs,
  GenClangDiagsEnums,
  GenClangDiagGroups,
  GenClangDiagsIndexName,
  GenClangDiagsStableIDs,
  GenClangDiagsInterface,
  GenClangCommentNodes,
  GenClangDeclNodes,
  GenClangStmtNodes,
  GenClangTypeNodes,
  GenClangTypeReader,
  GenClangTypeWriter,
  GenClangOpcodes,
  GenClangSACheckers,
  GenClangSyntaxNodeList,
  GenClangSyntaxNodeClasses,
  GenClangCommentHTMLTags,
  GenClangCommentHTMLTagsProperties,
  GenClangCommentHTMLNamedCharacterReferences,
  GenClangCommentCommandInfo,
  GenClangCommentCommandList,
  GenClangOpenCLBuiltins,
  GenClangOpenCLBuiltinHeader,
  GenClangOpenCLBuiltinTests,
  GenCXX11AttributeInfo,
  GenAttributeSpellingList,
  GenArmNeon,
  GenArmFP16,
  GenArmBF16,
  GenArmVectorType,
  GenArmNeonSema,
  GenArmNeonTest,
  GenArmImmCheckTypes,
  GenArmMveHeader,
  GenArmMveBuiltinDef,
  GenArmMveBuiltinSema,
  GenArmMveBuiltinCG,
  GenArmMveBuiltinAliases,
  GenArmSveHeader,
  GenArmSveBuiltins,
  GenArmSveBuiltinsJSON,
  GenArmSveBuiltinCG,
  GenArmSveTypeFlags,
  GenArmSveRangeChecks,
  GenArmSveStreamingAttrs,
  GenArmSmeHeader,
  GenArmSmeBuiltins,
  GenArmSmeBuiltinsJSON,
  GenArmSmeBuiltinCG,
  GenArmSmeRangeChecks,
  GenArmSmeStreamingAttrs,
  GenArmSmeBuiltinZAState,
  GenArmCdeHeader,
  GenArmCdeBuiltinDef,
  GenArmCdeBuiltinSema,
  GenArmCdeBuiltinCG,
  GenArmCdeBuiltinAliases,
  GenRISCVVectorHeader,
  GenRISCVVectorBuiltins,
  GenRISCVVectorBuiltinCG,
  GenRISCVVectorBuiltinSema,
  GenRISCVSiFiveVectorBuiltins,
  GenRISCVSiFiveVectorBuiltinCG,
  GenRISCVSiFiveVectorBuiltinSema,
  GenRISCVAndesVectorBuiltins,
  GenRISCVAndesVectorBuiltinCG,
  GenRISCVAndesVectorBuiltinSema,
  GenHLSLAliasIntrinsics,
  GenHLSLInlineIntrinsics,
  GenAttrDocs,
  GenBuiltinDocs,
  GenDiagDocs,
  GenOptDocs,
  GenDataCollectors,
  GenTestPragmaAttributeSupportedAttributes,
  GenClangBuiltinTraits
};

namespace {
static ActionType Action = PrintRecords;
static std::string ClangComponent;

static unsigned ClangComponentCount = 0;
static constexpr clv2::OptionInfo<std::string> OI_ClangComponent{
    "clang-component", "Only use warnings from specified component",
    clv2::Hidden};

static bool FirstAction = true;

/// One flag per ActionType.  The flags are enumerated by regAction calls
/// rather than a table, so each descriptor is built at runtime; the deque
/// keeps their addresses stable for the parser.
namespace {
struct ActionOption {
  ActionType Val;
  std::optional<clv2::RuntimeOption<bool>> Opt;
};
} // namespace
static std::deque<ActionOption> ActionOptions;

/// Ctx is the ActionOption whose flag was given.
static bool selectAction(void *Ctx, const bool &) {
  Action = static_cast<ActionOption *>(Ctx)->Val;
  return true;
}

static void regAction(clv2::OptionParser &P, const char *Name, ActionType Val,
                      const char *Desc) {
  // RuntimeOption is deliberately immovable (the parser holds its address),
  // so construct in place rather than push_back-ing a temporary.
  ActionOptions.emplace_back();
  ActionOption &A = ActionOptions.back();
  A.Val = Val;
  A.Opt.emplace(Name, Desc, clv2::ValueDisallowed,
                clv2::CtxCallback<bool>{&selectAction, &A});
  // Group display has no descriptor spelling, so it is set on the option's own
  // static info.
  A.Opt->staticInfo().IsEnumGroupMember = true;
  if (FirstAction) {
    A.Opt->staticInfo().EnumGroupHeader = "Action to perform:";
    FirstAction = false;
  }
  clv2::detail::OptionEntry E = A.Opt->makeEntry();
  P.addDynamicEntry(std::move(E));
}

static void registerClangTblgenBackends(clv2::OptionParser &P) {

  regAction(P, "print-records", PrintRecords,
            "Print all records to stdout (default)");
  regAction(P, "dump-json", DumpJSON,
            "Dump all records as machine-readable JSON");
  regAction(P, "gen-cir-lowering", GenCIRLowering,
            "Generate CIR operation lowering patterns");
  regAction(P, "gen-clang-attr-classes", GenClangAttrClasses,
            "Generate clang attribute classes");
  regAction(P, "gen-clang-attr-parser-string-switches",
            GenClangAttrParserStringSwitches,
            "Generate all parser-related attribute string switches");
  regAction(P, "gen-clang-attr-subject-match-rules-parser-string-switches",
            GenClangAttrSubjectMatchRulesParserStringSwitches,
            "Generate all parser-related attribute subject match rule "
            "string switches");
  regAction(P, "gen-clang-attr-impl", GenClangAttrImpl,
            "Generate clang attribute implementations");
  regAction(P, "gen-clang-attr-list", GenClangAttrList,
            "Generate a clang attribute list");
  regAction(P, "gen-clang-attr-doc-table", GenClangAttrDocTable,
            "Generate a table of attribute documentation");
  regAction(P, "gen-clang-attr-subject-match-rule-list",
            GenClangAttrSubjectMatchRuleList,
            "Generate a clang attribute subject match rule list");
  regAction(P, "gen-clang-attr-pch-read", GenClangAttrPCHRead,
            "Generate clang PCH attribute reader");
  regAction(P, "gen-clang-attr-pch-write", GenClangAttrPCHWrite,
            "Generate clang PCH attribute writer");
  regAction(P, "gen-clang-regular-keyword-attr-info",
            GenClangRegularKeywordAttributeInfo,
            "Generate a list of regular keyword attributes with info "
            "about their arguments");
  regAction(P, "gen-clang-attr-has-attribute-impl",
            GenClangAttrHasAttributeImpl,
            "Generate a clang attribute spelling list");
  regAction(P, "gen-clang-attr-spelling-index", GenClangAttrSpellingListIndex,
            "Generate a clang attribute spelling index");
  regAction(P, "gen-clang-attr-ast-visitor", GenClangAttrASTVisitor,
            "Generate a recursive AST visitor for clang attributes");
  regAction(P, "gen-clang-attr-template-instantiate",
            GenClangAttrTemplateInstantiate,
            "Generate a clang template instantiate code");
  regAction(P, "gen-clang-attr-parsed-attr-list", GenClangAttrParsedAttrList,
            "Generate a clang parsed attribute list");
  regAction(P, "gen-clang-attr-parsed-attr-impl", GenClangAttrParsedAttrImpl,
            "Generate the clang parsed attribute helpers");
  regAction(P, "gen-clang-attr-parsed-attr-kinds", GenClangAttrParsedAttrKinds,
            "Generate a clang parsed attribute kinds");
  regAction(P, "gen-clang-attr-is-type-dependent", GenClangAttrIsTypeDependent,
            "Generate clang is type dependent attribute code");
  regAction(P, "gen-clang-attr-text-node-dump", GenClangAttrTextNodeDump,
            "Generate clang attribute text node dumper");
  regAction(P, "gen-clang-attr-node-traverse", GenClangAttrNodeTraverse,
            "Generate clang attribute traverser");
  regAction(P, "gen-clang-attr-undocumented-list",
            GenClangAttrUndocumentedAttrList,
            "Generate a list of undocumented attributes");
  regAction(P, "gen-clang-builtins", GenClangBuiltins,
            "Generate clang builtins list");
  regAction(P, "gen-clang-builtin-templates", GenClangBuiltinTemplates,
            "Generate clang builtins list");
  regAction(P, "gen-clang-diags-compat-ids", GenClangDiagsCompatIDs,
            "Generate Clang diagnostic compatibility ids");
  regAction(P, "gen-clang-diags-defs", GenClangDiagsDefs,
            "Generate Clang diagnostics definitions");
  regAction(P, "gen-clang-diags-enums", GenClangDiagsEnums,
            "Generate Clang diagnostic enums for selects");
  regAction(P, "gen-clang-diag-groups", GenClangDiagGroups,
            "Generate Clang diagnostic groups");
  regAction(P, "gen-clang-diags-index-name", GenClangDiagsIndexName,
            "Generate Clang diagnostic name index");
  regAction(P, "gen-clang-diags-stable-ids", GenClangDiagsStableIDs,
            "Generate Clang diagnostic stable IDs");
  regAction(P, "gen-clang-diags-iface", GenClangDiagsInterface,
            "Generate Clang diagnostic interface headers");
  regAction(P, "gen-clang-basic-reader", GenClangBasicReader,
            "Generate Clang BasicReader classes");
  regAction(P, "gen-clang-basic-writer", GenClangBasicWriter,
            "Generate Clang BasicWriter classes");
  regAction(P, "gen-clang-comment-nodes", GenClangCommentNodes,
            "Generate Clang AST comment nodes");
  regAction(P, "gen-clang-decl-nodes", GenClangDeclNodes,
            "Generate Clang AST declaration nodes");
  regAction(P, "gen-clang-stmt-nodes", GenClangStmtNodes,
            "Generate Clang AST statement nodes");
  regAction(P, "gen-clang-type-nodes", GenClangTypeNodes,
            "Generate Clang AST type nodes");
  regAction(P, "gen-clang-type-reader", GenClangTypeReader,
            "Generate Clang AbstractTypeReader class");
  regAction(P, "gen-clang-type-writer", GenClangTypeWriter,
            "Generate Clang AbstractTypeWriter class");
  regAction(P, "gen-clang-opcodes", GenClangOpcodes,
            "Generate Clang constexpr interpreter opcodes");
  regAction(P, "gen-clang-sa-checkers", GenClangSACheckers,
            "Generate Clang Static Analyzer checkers");
  regAction(P, "gen-clang-syntax-node-list", GenClangSyntaxNodeList,
            "Generate list of Clang Syntax Tree node types");
  regAction(P, "gen-clang-syntax-node-classes", GenClangSyntaxNodeClasses,
            "Generate definitions of Clang Syntax Tree node classes");
  regAction(P, "gen-clang-comment-html-tags", GenClangCommentHTMLTags,
            "Generate efficient matchers for HTML tag "
            "names that are used in documentation comments");
  regAction(P, "gen-clang-comment-html-tags-properties",
            GenClangCommentHTMLTagsProperties,
            "Generate efficient matchers for HTML tag properties");
  regAction(P, "gen-clang-comment-html-named-character-references",
            GenClangCommentHTMLNamedCharacterReferences,
            "Generate function to translate named character "
            "references to UTF-8 sequences");
  regAction(P, "gen-clang-comment-command-info", GenClangCommentCommandInfo,
            "Generate command properties for commands that "
            "are used in documentation comments");
  regAction(P, "gen-clang-comment-command-list", GenClangCommentCommandList,
            "Generate list of commands that are used in "
            "documentation comments");
  regAction(P, "gen-clang-opencl-builtins", GenClangOpenCLBuiltins,
            "Generate OpenCL builtin declaration handlers");
  regAction(P, "gen-clang-opencl-builtin-header", GenClangOpenCLBuiltinHeader,
            "Generate OpenCL builtin header");
  regAction(P, "gen-clang-opencl-builtin-tests", GenClangOpenCLBuiltinTests,
            "Generate OpenCL builtin declaration tests");
  regAction(P, "gen-cxx11-attribute-info", GenCXX11AttributeInfo,
            "Generate CXX11 attributes info");
  regAction(P, "gen-attribute-spelling-list", GenAttributeSpellingList,
            "Generate attribute spelling list");
  regAction(P, "gen-arm-neon", GenArmNeon, "Generate arm_neon.h for clang");
  regAction(P, "gen-arm-fp16", GenArmFP16, "Generate arm_fp16.h for clang");
  regAction(P, "gen-arm-bf16", GenArmBF16, "Generate arm_bf16.h for clang");
  regAction(P, "gen-arm-vector-type", GenArmVectorType,
            "Generate arm_vector_types.h for clang");
  regAction(P, "gen-arm-neon-sema", GenArmNeonSema,
            "Generate ARM NEON sema support for clang");
  regAction(P, "gen-arm-neon-test", GenArmNeonTest,
            "Generate ARM NEON tests for clang");
  regAction(P, "gen-arm-immcheck-types", GenArmImmCheckTypes,
            "Generate arm_immcheck_types.inc (immediate range check "
            "types) for clang");
  regAction(P, "gen-arm-mve-header", GenArmMveHeader,
            "Generate arm_mve.h for clang");
  regAction(P, "gen-arm-mve-builtin-def", GenArmMveBuiltinDef,
            "Generate ARM MVE builtin definitions for clang");
  regAction(P, "gen-arm-mve-builtin-sema", GenArmMveBuiltinSema,
            "Generate ARM MVE builtin sema checks for clang");
  regAction(P, "gen-arm-mve-builtin-codegen", GenArmMveBuiltinCG,
            "Generate ARM MVE builtin code-generator for clang");
  regAction(P, "gen-arm-mve-builtin-aliases", GenArmMveBuiltinAliases,
            "Generate list of valid ARM MVE builtin aliases for clang");
  regAction(P, "gen-arm-sve-header", GenArmSveHeader,
            "Generate arm_sve.h for clang");
  regAction(P, "gen-arm-sve-builtins", GenArmSveBuiltins,
            "Generate arm_sve_builtins.inc for clang");
  regAction(P, "gen-arm-sve-builtins-json", GenArmSveBuiltinsJSON,
            "Generate arm_sve_buitins.json");
  regAction(P, "gen-arm-sve-builtin-codegen", GenArmSveBuiltinCG,
            "Generate arm_sve_builtin_cg_map.inc for clang");
  regAction(P, "gen-arm-sve-typeflags", GenArmSveTypeFlags,
            "Generate arm_sve_typeflags.inc for clang");
  regAction(P, "gen-arm-sve-sema-rangechecks", GenArmSveRangeChecks,
            "Generate arm_sve_sema_rangechecks.inc for clang");
  regAction(P, "gen-arm-sve-streaming-attrs", GenArmSveStreamingAttrs,
            "Generate arm_sve_streaming_attrs.inc for clang");
  regAction(P, "gen-arm-sme-header", GenArmSmeHeader,
            "Generate arm_sme.h for clang");
  regAction(P, "gen-arm-sme-builtins", GenArmSmeBuiltins,
            "Generate arm_sme_builtins.inc for clang");
  regAction(P, "gen-arm-sme-builtins-json", GenArmSmeBuiltinsJSON,
            "Generate arm_sme_buitins.json");
  regAction(P, "gen-arm-sme-builtin-codegen", GenArmSmeBuiltinCG,
            "Generate arm_sme_builtin_cg_map.inc for clang");
  regAction(P, "gen-arm-sme-sema-rangechecks", GenArmSmeRangeChecks,
            "Generate arm_sme_sema_rangechecks.inc for clang");
  regAction(P, "gen-arm-sme-streaming-attrs", GenArmSmeStreamingAttrs,
            "Generate arm_sme_streaming_attrs.inc for clang");
  regAction(P, "gen-arm-sme-builtin-za-state", GenArmSmeBuiltinZAState,
            "Generate arm_sme_builtins_za_state.inc for clang");
  regAction(P, "gen-arm-cde-header", GenArmCdeHeader,
            "Generate arm_cde.h for clang");
  regAction(P, "gen-arm-cde-builtin-def", GenArmCdeBuiltinDef,
            "Generate ARM CDE builtin definitions for clang");
  regAction(P, "gen-arm-cde-builtin-sema", GenArmCdeBuiltinSema,
            "Generate ARM CDE builtin sema checks for clang");
  regAction(P, "gen-arm-cde-builtin-codegen", GenArmCdeBuiltinCG,
            "Generate ARM CDE builtin code-generator for clang");
  regAction(P, "gen-arm-cde-builtin-aliases", GenArmCdeBuiltinAliases,
            "Generate list of valid ARM CDE builtin aliases for clang");
  regAction(P, "gen-riscv-vector-header", GenRISCVVectorHeader,
            "Generate riscv_vector.h for clang");
  regAction(P, "gen-riscv-vector-builtins", GenRISCVVectorBuiltins,
            "Generate riscv_vector_builtins.inc for clang");
  regAction(P, "gen-riscv-vector-builtin-codegen", GenRISCVVectorBuiltinCG,
            "Generate riscv_vector_builtin_cg.inc for clang");
  regAction(P, "gen-riscv-vector-builtin-sema", GenRISCVVectorBuiltinSema,
            "Generate riscv_vector_builtin_sema.inc for clang");
  regAction(P, "gen-riscv-sifive-vector-builtins", GenRISCVSiFiveVectorBuiltins,
            "Generate riscv_sifive_vector_builtins.inc for clang");
  regAction(P, "gen-riscv-sifive-vector-builtin-codegen",
            GenRISCVSiFiveVectorBuiltinCG,
            "Generate riscv_sifive_vector_builtin_cg.inc for clang");
  regAction(P, "gen-riscv-sifive-vector-builtin-sema",
            GenRISCVSiFiveVectorBuiltinSema,
            "Generate riscv_sifive_vector_builtin_sema.inc for clang");
  regAction(P, "gen-riscv-andes-vector-builtins", GenRISCVAndesVectorBuiltins,
            "Generate riscv_andes_vector_builtins.inc for clang");
  regAction(P, "gen-riscv-andes-vector-builtin-codegen",
            GenRISCVAndesVectorBuiltinCG,
            "Generate riscv_andes_vector_builtin_cg.inc for clang");
  regAction(P, "gen-riscv-andes-vector-builtin-sema",
            GenRISCVAndesVectorBuiltinSema,
            "Generate riscv_andes_vector_builtin_sema.inc for clang");
  regAction(P, "gen-hlsl-alias-intrinsics", GenHLSLAliasIntrinsics,
            "Generate HLSL alias intrinsic overloads for "
            "hlsl_alias_intrinsics.h");
  regAction(P, "gen-hlsl-inline-intrinsics", GenHLSLInlineIntrinsics,
            "Generate HLSL inline intrinsic overloads for "
            "hlsl_intrinsics.h");
  regAction(P, "gen-attr-docs", GenAttrDocs,
            "Generate attribute documentation");
  regAction(P, "gen-builtin-docs", GenBuiltinDocs,
            "Generate builtin documentation");
  regAction(P, "gen-diag-docs", GenDiagDocs,
            "Generate diagnostic documentation");
  regAction(P, "gen-opt-docs", GenOptDocs, "Generate option documentation");
  regAction(P, "gen-clang-data-collectors", GenDataCollectors,
            "Generate data collectors for AST nodes");
  regAction(P, "gen-clang-test-pragma-attribute-supported-attributes",
            GenTestPragmaAttributeSupportedAttributes,
            "Generate a list of attributes supported by #pragma clang "
            "attribute for testing purposes");
  regAction(P, "gen-clang-builtin-traits", GenClangBuiltinTraits,
            "Generate BuiltinTraits.inc for clang");
  P.addDynamicEntry(
      clv2::makeEntry<&OI_ClangComponent>(ClangComponent, ClangComponentCount));
}

bool ClangTableGenMain(raw_ostream &OS, const RecordKeeper &Records) {
  switch (Action) {
  case PrintRecords:
    OS << Records;           // No argument, dump all contents
    break;
  case DumpJSON:
    EmitJSON(Records, OS);
    break;
  case GenCIRLowering:
    EmitCIRLowering(Records, OS);
    break;
  case GenClangAttrClasses:
    EmitClangAttrClass(Records, OS);
    break;
  case GenClangAttrParserStringSwitches:
    EmitClangAttrParserStringSwitches(Records, OS);
    break;
  case GenClangAttrSubjectMatchRulesParserStringSwitches:
    EmitClangAttrSubjectMatchRulesParserStringSwitches(Records, OS);
    break;
  case GenCXX11AttributeInfo:
    EmitCXX11AttributeInfo(Records, OS);
    break;
  case GenAttributeSpellingList:
    EmitAttributeSpellingList(Records, OS);
    break;
  case GenClangAttrImpl:
    EmitClangAttrImpl(Records, OS);
    break;
  case GenClangAttrList:
    EmitClangAttrList(Records, OS);
    break;
  case GenClangAttrDocTable:
    EmitClangAttrDocTable(Records, OS);
    break;
  case GenClangAttrSubjectMatchRuleList:
    EmitClangAttrSubjectMatchRuleList(Records, OS);
    break;
  case GenClangAttrPCHRead:
    EmitClangAttrPCHRead(Records, OS);
    break;
  case GenClangAttrPCHWrite:
    EmitClangAttrPCHWrite(Records, OS);
    break;
  case GenClangRegularKeywordAttributeInfo:
    EmitClangRegularKeywordAttributeInfo(Records, OS);
    break;
  case GenClangAttrHasAttributeImpl:
    EmitClangAttrHasAttrImpl(Records, OS);
    break;
  case GenClangAttrSpellingListIndex:
    EmitClangAttrSpellingListIndex(Records, OS);
    break;
  case GenClangAttrASTVisitor:
    EmitClangAttrASTVisitor(Records, OS);
    break;
  case GenClangAttrTemplateInstantiate:
    EmitClangAttrTemplateInstantiate(Records, OS);
    break;
  case GenClangAttrParsedAttrList:
    EmitClangAttrParsedAttrList(Records, OS);
    break;
  case GenClangAttrParsedAttrImpl:
    EmitClangAttrParsedAttrImpl(Records, OS);
    break;
  case GenClangAttrParsedAttrKinds:
    EmitClangAttrParsedAttrKinds(Records, OS);
    break;
  case GenClangAttrIsTypeDependent:
    EmitClangAttrIsTypeDependent(Records, OS);
    break;
  case GenClangAttrTextNodeDump:
    EmitClangAttrTextNodeDump(Records, OS);
    break;
  case GenClangAttrNodeTraverse:
    EmitClangAttrNodeTraverse(Records, OS);
    break;
  case GenClangAttrUndocumentedAttrList:
    EmitClangUndocumentedAttrList(Records, OS);
    break;
  case GenClangBuiltins:
    EmitClangBuiltins(Records, OS);
    break;
  case GenClangBuiltinTemplates:
    EmitClangBuiltinTemplates(Records, OS);
    break;
  case GenClangDiagsCompatIDs:
    EmitClangDiagsCompatIDs(Records, OS, ClangComponent);
    break;
  case GenClangDiagsDefs:
    EmitClangDiagsDefs(Records, OS, ClangComponent);
    break;
  case GenClangDiagsEnums:
    EmitClangDiagsEnums(Records, OS, ClangComponent);
    break;
  case GenClangDiagGroups:
    EmitClangDiagGroups(Records, OS);
    break;
  case GenClangDiagsIndexName:
    EmitClangDiagsIndexName(Records, OS);
    break;
  case GenClangDiagsStableIDs:
    EmitClangDiagsStableIDs(Records, OS);
    break;
  case GenClangDiagsInterface:
    EmitClangDiagsInterface(OS, ClangComponent);
    break;
  case GenClangCommentNodes:
    EmitClangASTNodes(Records, OS, CommentNodeClassName, "");
    break;
  case GenClangDeclNodes:
    EmitClangASTNodes(Records, OS, DeclNodeClassName, "Decl",
                      DeclContextNodeClassName);
    EmitClangDeclContext(Records, OS);
    break;
  case GenClangStmtNodes:
    EmitClangASTNodes(Records, OS, StmtNodeClassName, "");
    break;
  case GenClangTypeNodes:
    EmitClangTypeNodes(Records, OS);
    break;
  case GenClangTypeReader:
    EmitClangTypeReader(Records, OS);
    break;
  case GenClangTypeWriter:
    EmitClangTypeWriter(Records, OS);
    break;
  case GenClangBasicReader:
    EmitClangBasicReader(Records, OS);
    break;
  case GenClangBasicWriter:
    EmitClangBasicWriter(Records, OS);
    break;
  case GenClangOpcodes:
    EmitClangOpcodes(Records, OS);
    break;
  case GenClangSACheckers:
    EmitClangSACheckers(Records, OS);
    break;
  case GenClangCommentHTMLTags:
    EmitClangCommentHTMLTags(Records, OS);
    break;
  case GenClangCommentHTMLTagsProperties:
    EmitClangCommentHTMLTagsProperties(Records, OS);
    break;
  case GenClangCommentHTMLNamedCharacterReferences:
    EmitClangCommentHTMLNamedCharacterReferences(Records, OS);
    break;
  case GenClangCommentCommandInfo:
    EmitClangCommentCommandInfo(Records, OS);
    break;
  case GenClangCommentCommandList:
    EmitClangCommentCommandList(Records, OS);
    break;
  case GenClangOpenCLBuiltins:
    EmitClangOpenCLBuiltins(Records, OS);
    break;
  case GenClangOpenCLBuiltinHeader:
    EmitClangOpenCLBuiltinHeader(Records, OS);
    break;
  case GenClangOpenCLBuiltinTests:
    EmitClangOpenCLBuiltinTests(Records, OS);
    break;
  case GenClangSyntaxNodeList:
    EmitClangSyntaxNodeList(Records, OS);
    break;
  case GenClangSyntaxNodeClasses:
    EmitClangSyntaxNodeClasses(Records, OS);
    break;
  case GenArmNeon:
    EmitNeon(Records, OS);
    break;
  case GenArmFP16:
    EmitFP16(Records, OS);
    break;
  case GenArmVectorType:
    EmitVectorTypes(Records, OS);
    break;
  case GenArmBF16:
    EmitBF16(Records, OS);
    break;
  case GenArmNeonSema:
    EmitNeonSema(Records, OS);
    break;
  case GenArmNeonTest:
    EmitNeonTest(Records, OS);
    break;
  case GenArmImmCheckTypes:
    EmitImmCheckTypes(Records, OS);
    break;
  case GenArmMveHeader:
    EmitMveHeader(Records, OS);
    break;
  case GenArmMveBuiltinDef:
    EmitMveBuiltinDef(Records, OS);
    break;
  case GenArmMveBuiltinSema:
    EmitMveBuiltinSema(Records, OS);
    break;
  case GenArmMveBuiltinCG:
    EmitMveBuiltinCG(Records, OS);
    break;
  case GenArmMveBuiltinAliases:
    EmitMveBuiltinAliases(Records, OS);
    break;
  case GenArmSveHeader:
    EmitSveHeader(Records, OS);
    break;
  case GenArmSveBuiltins:
    EmitSveBuiltins(Records, OS);
    break;
  case GenArmSveBuiltinsJSON:
    EmitSveBuiltinsJSON(Records, OS);
    break;
  case GenArmSveBuiltinCG:
    EmitSveBuiltinCG(Records, OS);
    break;
  case GenArmSveTypeFlags:
    EmitSveTypeFlags(Records, OS);
    break;
  case GenArmSveRangeChecks:
    EmitSveRangeChecks(Records, OS);
    break;
  case GenArmSveStreamingAttrs:
    EmitSveStreamingAttrs(Records, OS);
    break;
  case GenArmSmeHeader:
    EmitSmeHeader(Records, OS);
    break;
  case GenArmSmeBuiltins:
    EmitSmeBuiltins(Records, OS);
    break;
  case GenArmSmeBuiltinsJSON:
    EmitSmeBuiltinsJSON(Records, OS);
    break;
  case GenArmSmeBuiltinCG:
    EmitSmeBuiltinCG(Records, OS);
    break;
  case GenArmSmeRangeChecks:
    EmitSmeRangeChecks(Records, OS);
    break;
  case GenArmSmeStreamingAttrs:
    EmitSmeStreamingAttrs(Records, OS);
    break;
  case GenArmSmeBuiltinZAState:
    EmitSmeBuiltinZAState(Records, OS);
    break;
  case GenArmCdeHeader:
    EmitCdeHeader(Records, OS);
    break;
  case GenArmCdeBuiltinDef:
    EmitCdeBuiltinDef(Records, OS);
    break;
  case GenArmCdeBuiltinSema:
    EmitCdeBuiltinSema(Records, OS);
    break;
  case GenArmCdeBuiltinCG:
    EmitCdeBuiltinCG(Records, OS);
    break;
  case GenArmCdeBuiltinAliases:
    EmitCdeBuiltinAliases(Records, OS);
    break;
  case GenRISCVVectorHeader:
    EmitRVVHeader(Records, OS);
    break;
  case GenRISCVVectorBuiltins:
    EmitRVVBuiltins(Records, OS);
    break;
  case GenRISCVVectorBuiltinCG:
    EmitRVVBuiltinCG(Records, OS);
    break;
  case GenRISCVVectorBuiltinSema:
    EmitRVVBuiltinSema(Records, OS);
    break;
  case GenRISCVSiFiveVectorBuiltins:
    EmitRVVBuiltins(Records, OS);
    break;
  case GenRISCVSiFiveVectorBuiltinCG:
    EmitRVVBuiltinCG(Records, OS);
    break;
  case GenRISCVSiFiveVectorBuiltinSema:
    EmitRVVBuiltinSema(Records, OS);
    break;
  case GenRISCVAndesVectorBuiltins:
    EmitRVVBuiltins(Records, OS);
    break;
  case GenRISCVAndesVectorBuiltinCG:
    EmitRVVBuiltinCG(Records, OS);
    break;
  case GenRISCVAndesVectorBuiltinSema:
    EmitRVVBuiltinSema(Records, OS);
    break;
  case GenHLSLAliasIntrinsics:
    EmitHLSLAliasIntrinsics(Records, OS);
    break;
  case GenHLSLInlineIntrinsics:
    EmitHLSLInlineIntrinsics(Records, OS);
    break;
  case GenAttrDocs:
    EmitClangAttrDocs(Records, OS);
    break;
  case GenBuiltinDocs:
    EmitClangBuiltinDocs(Records, OS);
    break;
  case GenDiagDocs:
    EmitClangDiagDocs(Records, OS);
    break;
  case GenOptDocs:
    EmitClangOptDocs(Records, OS);
    break;
  case GenDataCollectors:
    EmitClangDataCollectors(Records, OS);
    break;
  case GenTestPragmaAttributeSupportedAttributes:
    EmitTestPragmaAttributeSupportedAttributes(Records, OS);
    break;
  case GenClangBuiltinTraits:
    EmitClangBuiltinTraits(Records, OS);
    break;
  }

  return false;
}
}

int main(int argc, char **argv) {
  sys::PrintStackTraceOnErrorSignal(argv[0]);
  PrettyStackTraceProgram X(argc, argv);
  clv2::OptionParser P;
  registerTableGenMainOptions(P);
  registerClangTblgenBackends(P);
  llvm::TableGen::Emitter::registerBackendOptions(P);
  P.parse(argc, argv);

  llvm_shutdown_obj Y;

  return TableGenMain(argv[0], &ClangTableGenMain);
}

#ifdef __has_feature
#if __has_feature(address_sanitizer)
#include <sanitizer/lsan_interface.h>
// Disable LeakSanitizer for this binary as it has too many leaks that are not
// very interesting to fix. See compiler-rt/include/sanitizer/lsan_interface.h .
int __lsan_is_turned_off() { return 1; }
#endif  // __has_feature(address_sanitizer)
#endif  // defined(__has_feature)
