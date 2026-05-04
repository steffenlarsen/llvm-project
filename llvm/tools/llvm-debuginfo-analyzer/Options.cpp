//===-- options.cpp - Command line options for llvm-debuginfo-analyzer----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This handles the command line options for llvm-debuginfo-analyzer.
//
//===----------------------------------------------------------------------===//

#include "Options.h"
#include "llvm/DebugInfo/LogicalView/Core/LVOptions.h"
#include "llvm/DebugInfo/LogicalView/Core/LVSort.h"

using namespace llvm;
using namespace llvm::clv2;
using namespace llvm::logicalview;
using namespace llvm::logicalview::cmdline;

//===----------------------------------------------------------------------===//
// EnumVal tables
//===----------------------------------------------------------------------===//

const EnumVal<LVAttributeKind> cmdline::AttributeKindVals[] = {
    {"all", LVAttributeKind::All, "Include all attributes."},
    {"argument", LVAttributeKind::Argument,
     "Template parameters replaced by its arguments."},
    {"base", LVAttributeKind::Base, "Base types (int, bool, etc.)."},
    {"coverage", LVAttributeKind::Coverage, "Symbol location coverage."},
    {"directories", LVAttributeKind::Directories,
     "Directories referenced in the debug information."},
    {"discarded", LVAttributeKind::Discarded,
     "Discarded elements by the linker."},
    {"discriminator", LVAttributeKind::Discriminator,
     "Discriminators for inlined function instances."},
    {"encoded", LVAttributeKind::Encoded,
     "Template arguments encoded in the template name."},
    {"extended", LVAttributeKind::Extended, "Advanced attributes alias."},
    {"filename", LVAttributeKind::Filename,
     "Filename where the element is defined."},
    {"files", LVAttributeKind::Files,
     "Files referenced in the debug information."},
    {"format", LVAttributeKind::Format, "Object file format name."},
    {"gaps", LVAttributeKind::Gaps, "Missing debug location (gaps)."},
    {"generated", LVAttributeKind::Generated, "Compiler generated elements."},
    {"global", LVAttributeKind::Global,
     "Element referenced across Compile Units."},
    {"inserted", LVAttributeKind::Inserted,
     "Generated inlined abstract references."},
    {"language", LVAttributeKind::Language, "Source language name."},
    {"level", LVAttributeKind::Level,
     "Lexical scope level (File=0, Compile Unit=1)."},
    {"linkage", LVAttributeKind::Linkage, "Linkage name."},
    {"local", LVAttributeKind::Local,
     "Element referenced only in the Compile Unit."},
    {"location", LVAttributeKind::Location, "Element debug location."},
    {"offset", LVAttributeKind::Offset, "Debug information offset."},
    {"pathname", LVAttributeKind::Pathname,
     "Pathname where the element is defined."},
    {"producer", LVAttributeKind::Producer, "Toolchain identification name."},
    {"publics", LVAttributeKind::Publics, "Function names that are public."},
    {"qualified", LVAttributeKind::Qualified,
     "The element type include parents in its name."},
    {"qualifier", LVAttributeKind::Qualifier,
     "Line qualifiers (Newstatement, BasicBlock, etc.)."},
    {"range", LVAttributeKind::Range, "Debug location ranges."},
    {"reference", LVAttributeKind::Reference,
     "Element declaration and definition references."},
    {"register", LVAttributeKind::Register, "Processor register names."},
    {"size", LVAttributeKind::Size, "Type sizes."},
    {"standard", LVAttributeKind::Standard, "Basic attributes alias."},
    {"subrange", LVAttributeKind::Subrange,
     "Subrange encoding information for arrays."},
    {"system", LVAttributeKind::System, "Display PDB's MS system elements."},
    {"typename", LVAttributeKind::Typename, "Include Parameters in templates."},
    {"underlying", LVAttributeKind::Underlying,
     "Underlying type for type definitions."},
    {"zero", LVAttributeKind::Zero, "Zero line numbers."},
};
static_assert(sizeof(AttributeKindVals) / sizeof(AttributeKindVals[0]) ==
                  NumAttributeKindVals,
              "AttributeKindVals size mismatch");

const EnumVal<LVCompareKind> cmdline::CompareKindVals[] = {
    {"all", LVCompareKind::All, "Compare all elements."},
    {"lines", LVCompareKind::Lines, "Lines."},
    {"scopes", LVCompareKind::Scopes, "Scopes."},
    {"symbols", LVCompareKind::Symbols, "Symbols."},
    {"types", LVCompareKind::Types, "Types."},
};
static_assert(sizeof(CompareKindVals) / sizeof(CompareKindVals[0]) ==
                  NumCompareKindVals,
              "CompareKindVals size mismatch");

const EnumVal<LVOutputKind> cmdline::OutputKindVals[] = {
    {"all", LVOutputKind::All, "All outputs."},
    {"split", LVOutputKind::Split, "Split the output by Compile Units."},
    {"text", LVOutputKind::Text, "Use a free form text output."},
    {"json", LVOutputKind::Json, "Use JSON as the output format."},
};
static_assert(sizeof(OutputKindVals) / sizeof(OutputKindVals[0]) ==
                  NumOutputKindVals,
              "OutputKindVals size mismatch");

const EnumVal<LVSortMode> cmdline::SortModeVals[] = {
    {"none", LVSortMode::None, "Unsorted output (i.e. as read from input)."},
    {"id", LVSortMode::ID, "Sort by unique element ID."},
    {"kind", LVSortMode::Kind, "Sort by element kind."},
    {"line", LVSortMode::Line, "Sort by element line number."},
    {"name", LVSortMode::Name, "Sort by element name."},
    {"offset", LVSortMode::Offset, "Sort by element offset."},
};
static_assert(sizeof(SortModeVals) / sizeof(SortModeVals[0]) == NumSortModeVals,
              "SortModeVals size mismatch");

const EnumVal<LVPrintKind> cmdline::PrintKindVals[] = {
    {"all", LVPrintKind::All, "All elements."},
    {"elements", LVPrintKind::Elements,
     "Instructions, lines, scopes, symbols and types."},
    {"instructions", LVPrintKind::Instructions, "Assembler instructions."},
    {"lines", LVPrintKind::Lines, "Lines referenced in the debug information."},
    {"scopes", LVPrintKind::Scopes, "A lexical block (Function, Class, etc.)."},
    {"sizes", LVPrintKind::Sizes,
     "Scope contributions to the debug information."},
    {"summary", LVPrintKind::Summary,
     "Summary of elements missing/added/matched/printed."},
    {"symbols", LVPrintKind::Symbols, "Symbols (Variable, Members, etc.)."},
    {"types", LVPrintKind::Types, "Types (Pointer, Reference, etc.)."},
    {"warnings", LVPrintKind::Warnings, "Warnings detected."},
};
static_assert(sizeof(PrintKindVals) / sizeof(PrintKindVals[0]) ==
                  NumPrintKindVals,
              "PrintKindVals size mismatch");

const EnumVal<LVReportKind> cmdline::ReportKindVals[] = {
    {"all", LVReportKind::All, "Generate all reports."},
    {"children", LVReportKind::Children,
     "Selected elements are displayed in a tree view "
     "(Include children)"},
    {"list", LVReportKind::List,
     "Selected elements are displayed in a tabular format."},
    {"parents", LVReportKind::Parents,
     "Selected elements are displayed in a tree view. "
     "(Include parents)"},
    {"view", LVReportKind::View,
     "Selected elements are displayed in a tree view "
     "(Include parents and children."},
};
static_assert(sizeof(ReportKindVals) / sizeof(ReportKindVals[0]) ==
                  NumReportKindVals,
              "ReportKindVals size mismatch");

const EnumVal<LVElementKind> cmdline::ElementKindVals[] = {
    {"Discarded", LVElementKind::Discarded,
     "Discarded elements by the linker."},
    {"Global", LVElementKind::Global,
     "Element referenced across Compile Units."},
    {"Optimized", LVElementKind::Optimized,
     "Generated inlined abstract references."},
};
static_assert(sizeof(ElementKindVals) / sizeof(ElementKindVals[0]) ==
                  NumElementKindVals,
              "ElementKindVals size mismatch");

const EnumVal<LVLineKind> cmdline::LineKindVals[] = {
    {"AlwaysStepInto", LVLineKind::IsAlwaysStepInto, "Always Step Into."},
    {"BasicBlock", LVLineKind::IsBasicBlock, "Basic block."},
    {"Discriminator", LVLineKind::IsDiscriminator, "Discriminator."},
    {"EndSequence", LVLineKind::IsEndSequence, "End sequence."},
    {"EpilogueBegin.", LVLineKind::IsEpilogueBegin, "Epilogue begin."},
    {"LineDebug", LVLineKind::IsLineDebug, "Debug line."},
    {"LineAssembler", LVLineKind::IsLineAssembler, "Assembler line."},
    {"NeverStepInto", LVLineKind::IsNeverStepInto, "Never Step Into."},
    {"NewStatement", LVLineKind::IsNewStatement, "New statement."},
    {"PrologueEnd", LVLineKind::IsPrologueEnd, "Prologue end."},
};
static_assert(sizeof(LineKindVals) / sizeof(LineKindVals[0]) == NumLineKindVals,
              "LineKindVals size mismatch");

const EnumVal<LVScopeKind> cmdline::ScopeKindVals[] = {
    {"Aggregate", LVScopeKind::IsAggregate, "Class, Structure or Union."},
    {"Array", LVScopeKind::IsArray, "Array."},
    {"Block", LVScopeKind::IsBlock, "Lexical block."},
    {"CallSite", LVScopeKind::IsCallSite, "Call site block."},
    {"CatchBlock", LVScopeKind::IsCatchBlock, "Exception catch block."},
    {"Class", LVScopeKind::IsClass, "Class."},
    {"CompileUnit", LVScopeKind::IsCompileUnit, "Compile unit."},
    {"EntryPoint", LVScopeKind::IsEntryPoint, "Function entry point."},
    {"Enumeration", LVScopeKind::IsEnumeration, "Enumeration."},
    {"Function", LVScopeKind::IsFunction, "Function."},
    {"FunctionType", LVScopeKind::IsFunctionType, "Function type."},
    {"InlinedFunction", LVScopeKind::IsInlinedFunction, "Inlined function."},
    {"Label", LVScopeKind::IsLabel, "Label."},
    {"LexicalBlock", LVScopeKind::IsLexicalBlock, "Lexical block."},
    {"Module", LVScopeKind::IsModule, "Module."},
    {"Namespace", LVScopeKind::IsNamespace, "Namespace."},
    {"Root", LVScopeKind::IsRoot, "Root."},
    {"Structure", LVScopeKind::IsStructure, "Structure."},
    {"Subprogram", LVScopeKind::IsSubprogram, "Subprogram."},
    {"Template", LVScopeKind::IsTemplate, "Template."},
    {"TemplateAlias", LVScopeKind::IsTemplateAlias, "Template alias."},
    {"TemplatePack", LVScopeKind::IsTemplatePack, "Template pack."},
    {"TryBlock", LVScopeKind::IsTryBlock, "Exception try block."},
    {"Union", LVScopeKind::IsUnion, "Union."},
};
static_assert(sizeof(ScopeKindVals) / sizeof(ScopeKindVals[0]) ==
                  NumScopeKindVals,
              "ScopeKindVals size mismatch");

const EnumVal<LVSymbolKind> cmdline::SymbolKindVals[] = {
    {"CallSiteParameter", LVSymbolKind::IsCallSiteParameter,
     "Call site parameter."},
    {"Constant", LVSymbolKind::IsConstant, "Constant."},
    {"Inheritance", LVSymbolKind::IsInheritance, "Inheritance."},
    {"Member", LVSymbolKind::IsMember, "Member."},
    {"Parameter", LVSymbolKind::IsParameter, "Parameter."},
    {"Unspecified", LVSymbolKind::IsUnspecified, "Unspecified parameter."},
    {"Variable", LVSymbolKind::IsVariable, "Variable."},
};
static_assert(sizeof(SymbolKindVals) / sizeof(SymbolKindVals[0]) ==
                  NumSymbolKindVals,
              "SymbolKindVals size mismatch");

const EnumVal<LVTypeKind> cmdline::TypeKindVals[] = {
    {"Base", LVTypeKind::IsBase, "Base Type (int, bool, etc.)."},
    {"Const", LVTypeKind::IsConst, "Constant specifier."},
    {"Enumerator", LVTypeKind::IsEnumerator, "Enumerator."},
    {"Import", LVTypeKind::IsImport, "Import."},
    {"ImportDeclaration", LVTypeKind::IsImportDeclaration,
     "Import declaration."},
    {"ImportModule", LVTypeKind::IsImportModule, "Import module."},
    {"Pointer", LVTypeKind::IsPointer, "Pointer."},
    {"PointerMember", LVTypeKind::IsPointerMember, "Pointer to member."},
    {"Reference", LVTypeKind::IsReference, "Reference type."},
    {"Restrict", LVTypeKind::IsRestrict, "Restrict specifier."},
    {"RvalueReference", LVTypeKind::IsRvalueReference, "Rvalue reference."},
    {"Subrange", LVTypeKind::IsSubrange, "Array subrange."},
    {"TemplateParam", LVTypeKind::IsTemplateParam, "Template Parameter."},
    {"TemplateTemplateParam", LVTypeKind::IsTemplateTemplateParam,
     "Template template parameter."},
    {"TemplateTypeParam", LVTypeKind::IsTemplateTypeParam,
     "Template type parameter."},
    {"TemplateValueParam", LVTypeKind::IsTemplateValueParam,
     "Template value parameter."},
    {"Typedef", LVTypeKind::IsTypedef, "Type definition."},
    {"Unspecified", LVTypeKind::IsUnspecified, "Unspecified type."},
    {"Volatile", LVTypeKind::IsVolatile, "Volatile specifier."},
};
static_assert(sizeof(TypeKindVals) / sizeof(TypeKindVals[0]) == NumTypeKindVals,
              "TypeKindVals size mismatch");

const EnumVal<LVWarningKind> cmdline::WarningKindVals[] = {
    {"all", LVWarningKind::All, "All warnings."},
    {"coverages", LVWarningKind::Coverages, "Invalid symbol coverages values."},
    {"lines", LVWarningKind::Lines, "Debug lines that are zero."},
    {"locations", LVWarningKind::Locations, "Invalid symbol locations."},
    {"ranges", LVWarningKind::Ranges, "Invalid code ranges."},
};
static_assert(sizeof(WarningKindVals) / sizeof(WarningKindVals[0]) ==
                  NumWarningKindVals,
              "WarningKindVals size mismatch");

const EnumVal<LVInternalKind> cmdline::InternalKindVals[] = {
    {"all", LVInternalKind::All, "Enable all traces."},
    {"cmdline", LVInternalKind::Cmdline, "Print command line."},
    {"id", LVInternalKind::ID, "Print unique element ID"},
    {"integrity", LVInternalKind::Integrity, "Check elements integrity."},
    {"none", LVInternalKind::None, "Ignore element line number."},
    {"tag", LVInternalKind::Tag, "Debug information tags."},
};
static_assert(sizeof(InternalKindVals) / sizeof(InternalKindVals[0]) ==
                  NumInternalKindVals,
              "InternalKindVals size mismatch");

//===----------------------------------------------------------------------===//
// Global state
//===----------------------------------------------------------------------===//

LVOptions cmdline::ReaderOptions;
