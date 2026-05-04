//===- ClangDiff.cpp - compare source files by AST nodes ------*- C++ -*- -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a tool for syntax tree based comparison using
// Tooling/ASTDiff.
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/ASTDiff/ASTDiff.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/RegisterLLVMOptions.h"

using namespace llvm;
using namespace clang;
using namespace clang::tooling;

static cl::OptionCategory ClangDiffCategory("clang-diff options");

// --- constexpr option descriptors ---
inline constexpr clv2::OptionInfo<bool> CDASTDumpOpt{
    "ast-dump", "Print the internal representation of the AST."};

inline constexpr clv2::OptionInfo<bool> CDASTDumpJsonOpt{
    "ast-dump-json", "Print the internal representation of the AST as JSON."};

inline constexpr clv2::OptionInfo<bool> CDPrintMatchesOpt{
    "dump-matches", "Print the matched nodes."};

inline constexpr clv2::OptionInfo<bool> CDHtmlDiffOpt{
    "html", "Output a side-by-side diff in HTML."};

inline constexpr clv2::OptionInfo<std::string> CDSourcePathOpt{
    "", "<source>", clv2::Positional{}, clv2::Required};

inline constexpr clv2::OptionInfo<std::string> CDDestinationPathOpt{
    "", "<destination>", clv2::Positional{}};

inline constexpr clv2::OptionInfo<std::string> CDStopAfterOpt{
    "stop-diff-after", "<topdown|bottomup>"};

inline constexpr clv2::OptionInfo<int> CDMaxSizeOpt{"s", "<maxsize>",
                                                    clv2::Init{-1}};

inline constexpr clv2::OptionInfo<std::string> CDBuildPathOpt{"p",
                                                              "Build path"};

inline constexpr clv2::ListOptionInfo<std::string> CDArgsAfterOpt{
    "extra-arg", "Additional argument to append to the compiler command line"};

inline constexpr clv2::ListOptionInfo<std::string> CDArgsBeforeOpt{
    "extra-arg-before",
    "Additional argument to prepend to the compiler command line"};

inline constexpr clv2::OptionsRegistry<
    &CDASTDumpOpt, &CDASTDumpJsonOpt, &CDPrintMatchesOpt, &CDHtmlDiffOpt,
    &CDSourcePathOpt, &CDDestinationPathOpt, &CDStopAfterOpt, &CDMaxSizeOpt,
    &CDBuildPathOpt, &CDArgsAfterOpt, &CDArgsBeforeOpt>
    ClangDiffReg;

namespace {
struct ClangDiffOptions {
  bool ASTDump = false;
  bool ASTDumpJson = false;
  bool PrintMatches = false;
  bool HtmlDiff = false;
  std::string SourcePath;
  std::string DestinationPath;
  std::string StopAfter;
  int MaxSize = -1;
  std::string BuildPath;
  std::vector<std::string> ArgsAfter;
  std::vector<std::string> ArgsBefore;
};
} // namespace

static void
applyClangDiffOpts(const decltype(ClangDiffReg)::ParsedOptionsT &Opts,
                   ClangDiffOptions &DiffOpts) {
  DiffOpts.ASTDump = Opts.get<&CDASTDumpOpt>();
  DiffOpts.ASTDumpJson = Opts.get<&CDASTDumpJsonOpt>();
  DiffOpts.PrintMatches = Opts.get<&CDPrintMatchesOpt>();
  DiffOpts.HtmlDiff = Opts.get<&CDHtmlDiffOpt>();
  DiffOpts.SourcePath = Opts.get<&CDSourcePathOpt>();
  DiffOpts.DestinationPath = Opts.get<&CDDestinationPathOpt>();
  DiffOpts.StopAfter = Opts.get<&CDStopAfterOpt>();
  DiffOpts.MaxSize = Opts.get<&CDMaxSizeOpt>();
  DiffOpts.BuildPath = Opts.get<&CDBuildPathOpt>();
  DiffOpts.ArgsAfter = Opts.get<&CDArgsAfterOpt>();
  DiffOpts.ArgsBefore = Opts.get<&CDArgsBeforeOpt>();
}

static void addExtraArgs(std::unique_ptr<CompilationDatabase> &Compilations,
                         const std::vector<std::string> &ArgsBefore,
                         const std::vector<std::string> &ArgsAfter) {
  if (!Compilations)
    return;
  auto AdjustingCompilations =
      std::make_unique<ArgumentsAdjustingCompilations>(
          std::move(Compilations));
  AdjustingCompilations->appendArgumentsAdjuster(
      getInsertArgumentAdjuster(ArgsBefore, ArgumentInsertPosition::BEGIN));
  AdjustingCompilations->appendArgumentsAdjuster(
      getInsertArgumentAdjuster(ArgsAfter, ArgumentInsertPosition::END));
  Compilations = std::move(AdjustingCompilations);
}

static std::unique_ptr<ASTUnit>
getAST(const std::unique_ptr<CompilationDatabase> &CommonCompilations,
       const StringRef Filename, const ClangDiffOptions &DiffOpts) {
  std::string ErrorMessage;
  std::unique_ptr<CompilationDatabase> Compilations;
  if (!CommonCompilations) {
    Compilations = CompilationDatabase::autoDetectFromSource(
        DiffOpts.BuildPath.empty() ? Filename : DiffOpts.BuildPath,
        ErrorMessage);
    if (!Compilations) {
      llvm::errs()
          << "Error while trying to load a compilation database, running "
             "without flags.\n"
          << ErrorMessage;
      Compilations =
          std::make_unique<clang::tooling::FixedCompilationDatabase>(
              ".", std::vector<std::string>());
    }
  }
  addExtraArgs(Compilations, DiffOpts.ArgsBefore, DiffOpts.ArgsAfter);
  std::array<std::string, 1> Files = {{std::string(Filename)}};
  ClangTool Tool(Compilations ? *Compilations : *CommonCompilations, Files);
  std::vector<std::unique_ptr<ASTUnit>> ASTs;
  Tool.buildASTs(ASTs);
  if (ASTs.size() != Files.size())
    return nullptr;
  return std::move(ASTs[0]);
}

static char hexdigit(int N) { return N &= 0xf, N + (N < 10 ? '0' : 'a' - 10); }

static const char HtmlDiffHeader[] = R"(
<html>
<head>
<meta charset='utf-8'/>
<style>
span.d { color: red; }
span.u { color: #cc00cc; }
span.i { color: green; }
span.m { font-weight: bold; }
span   { font-weight: normal; color: black; }
div.code {
  width: 48%;
  height: 98%;
  overflow: scroll;
  float: left;
  padding: 0 0 0.5% 0.5%;
  border: solid 2px LightGrey;
  border-radius: 5px;
}
</style>
</head>
<script type='text/javascript'>
highlightStack = []
function clearHighlight() {
  while (highlightStack.length) {
    var [l, r] = highlightStack.pop()
    document.getElementById(l).style.backgroundColor = 'inherit'
    if (r[1] != '-')
      document.getElementById(r).style.backgroundColor = 'inherit'
  }
}
function highlight(event) {
  var id = event.target['id']
  doHighlight(id)
}
function doHighlight(id) {
  clearHighlight()
  source = document.getElementById(id)
  if (!source.attributes['tid'])
    return
  var mapped = source
  while (mapped && mapped.parentElement && mapped.attributes['tid'].value.substr(1) === '-1')
    mapped = mapped.parentElement
  var tid = null, target = null
  if (mapped) {
    tid = mapped.attributes['tid'].value
    target = document.getElementById(tid)
  }
  if (source.parentElement && source.parentElement.classList.contains('code'))
    return
  source.style.backgroundColor = 'lightgrey'
  source.scrollIntoView()
  if (target) {
    if (mapped === source)
      target.style.backgroundColor = 'lightgrey'
    target.scrollIntoView()
  }
  highlightStack.push([id, tid])
  location.hash = '#' + id
}
function scrollToBoth() {
  doHighlight(location.hash.substr(1))
}
function changed(elem) {
  return elem.classList.length == 0
}
function nextChangedNode(prefix, increment, number) {
  do {
    number += increment
    var elem = document.getElementById(prefix + number)
  } while(elem && !changed(elem))
  return elem ? number : null
}
function handleKey(e) {
  var down = e.code === "KeyJ"
  var up = e.code === "KeyK"
  if (!down && !up)
    return
  var id = highlightStack[0] ? highlightStack[0][0] : 'R0'
  var oldelem = document.getElementById(id)
  var number = parseInt(id.substr(1))
  var increment = down ? 1 : -1
  var lastnumber = number
  var prefix = id[0]
  do {
    number = nextChangedNode(prefix, increment, number)
    var elem = document.getElementById(prefix + number)
    if (up && elem) {
      while (elem.parentElement && changed(elem.parentElement))
        elem = elem.parentElement
      number = elem.id.substr(1)
    }
  } while ((down && id !== 'R0' && oldelem.contains(elem)))
  if (!number)
    number = lastnumber
  elem = document.getElementById(prefix + number)
  doHighlight(prefix + number)
}
window.onload = scrollToBoth
window.onkeydown = handleKey
</script>
<body>
<div onclick='highlight(event)'>
)";

static void printHtml(raw_ostream &OS, char C) {
  switch (C) {
  case '&':
    OS << "&amp;";
    break;
  case '<':
    OS << "&lt;";
    break;
  case '>':
    OS << "&gt;";
    break;
  case '\'':
    OS << "&#x27;";
    break;
  case '"':
    OS << "&quot;";
    break;
  default:
    OS << C;
  }
}

static void printHtml(raw_ostream &OS, const StringRef Str) {
  for (char C : Str)
    printHtml(OS, C);
}

static std::string getChangeKindAbbr(diff::ChangeKind Kind) {
  switch (Kind) {
  case diff::None:
    return "";
  case diff::Delete:
    return "d";
  case diff::Update:
    return "u";
  case diff::Insert:
    return "i";
  case diff::Move:
    return "m";
  case diff::UpdateMove:
    return "u m";
  }
  llvm_unreachable("Invalid enumeration value.");
}

static unsigned printHtmlForNode(raw_ostream &OS, const diff::ASTDiff &Diff,
                                 diff::SyntaxTree &Tree, bool IsLeft,
                                 diff::NodeId Id, unsigned Offset) {
  const diff::Node &Node = Tree.getNode(Id);
  char MyTag, OtherTag;
  diff::NodeId LeftId, RightId;
  diff::NodeId TargetId = Diff.getMapped(Tree, Id);
  if (IsLeft) {
    MyTag = 'L';
    OtherTag = 'R';
    LeftId = Id;
    RightId = TargetId;
  } else {
    MyTag = 'R';
    OtherTag = 'L';
    LeftId = TargetId;
    RightId = Id;
  }
  unsigned Begin, End;
  std::tie(Begin, End) = Tree.getSourceRangeOffsets(Node);
  const SourceManager &SrcMgr = Tree.getASTContext().getSourceManager();
  auto Code = SrcMgr.getBufferOrFake(SrcMgr.getMainFileID()).getBuffer();
  for (; Offset < Begin; ++Offset)
    printHtml(OS, Code[Offset]);
  OS << "<span id='" << MyTag << Id << "' "
     << "tid='" << OtherTag << TargetId << "' ";
  OS << "title='";
  printHtml(OS, Node.getTypeLabel());
  OS << "\n" << LeftId << " -> " << RightId;
  std::string Value = Tree.getNodeValue(Node);
  if (!Value.empty()) {
    OS << "\n";
    printHtml(OS, Value);
  }
  OS << "'";
  if (Node.Change != diff::None)
    OS << " class='" << getChangeKindAbbr(Node.Change) << "'";
  OS << ">";

  for (diff::NodeId Child : Node.Children)
    Offset = printHtmlForNode(OS, Diff, Tree, IsLeft, Child, Offset);

  for (; Offset < End; ++Offset)
    printHtml(OS, Code[Offset]);
  if (Id == Tree.getRootId()) {
    End = Code.size();
    for (; Offset < End; ++Offset)
      printHtml(OS, Code[Offset]);
  }
  OS << "</span>";
  return Offset;
}

static void printJsonString(raw_ostream &OS, const StringRef Str) {
  for (signed char C : Str) {
    switch (C) {
    case '"':
      OS << R"(\")";
      break;
    case '\\':
      OS << R"(\\)";
      break;
    case '\n':
      OS << R"(\n)";
      break;
    case '\t':
      OS << R"(\t)";
      break;
    default:
      if ('\x00' <= C && C <= '\x1f') {
        OS << R"(\u00)" << hexdigit(C >> 4) << hexdigit(C);
      } else {
        OS << C;
      }
    }
  }
}

static void printNodeAttributes(raw_ostream &OS, diff::SyntaxTree &Tree,
                                diff::NodeId Id) {
  const diff::Node &N = Tree.getNode(Id);
  OS << R"("id":)" << int(Id);
  OS << R"(,"type":")" << N.getTypeLabel() << '"';
  auto Offsets = Tree.getSourceRangeOffsets(N);
  OS << R"(,"begin":)" << Offsets.first;
  OS << R"(,"end":)" << Offsets.second;
  std::string Value = Tree.getNodeValue(N);
  if (!Value.empty()) {
    OS << R"(,"value":")";
    printJsonString(OS, Value);
    OS << '"';
  }
}

static void printNodeAsJson(raw_ostream &OS, diff::SyntaxTree &Tree,
                            diff::NodeId Id) {
  const diff::Node &N = Tree.getNode(Id);
  OS << "{";
  printNodeAttributes(OS, Tree, Id);
  auto Identifier = N.getIdentifier();
  auto QualifiedIdentifier = N.getQualifiedIdentifier();
  if (Identifier) {
    OS << R"(,"identifier":")";
    printJsonString(OS, *Identifier);
    OS << R"(")";
    if (QualifiedIdentifier && *Identifier != *QualifiedIdentifier) {
      OS << R"(,"qualified_identifier":")";
      printJsonString(OS, *QualifiedIdentifier);
      OS << R"(")";
    }
  }
  OS << R"(,"children":[)";
  if (N.Children.size() > 0) {
    printNodeAsJson(OS, Tree, N.Children[0]);
    for (size_t I = 1, E = N.Children.size(); I < E; ++I) {
      OS << ",";
      printNodeAsJson(OS, Tree, N.Children[I]);
    }
  }
  OS << "]}";
}

static void printNode(raw_ostream &OS, diff::SyntaxTree &Tree,
                      diff::NodeId Id) {
  if (Id.isInvalid()) {
    OS << "None";
    return;
  }
  OS << Tree.getNode(Id).getTypeLabel();
  std::string Value = Tree.getNodeValue(Id);
  if (!Value.empty())
    OS << ": " << Value;
  OS << "(" << Id << ")";
}

static void printTree(raw_ostream &OS, diff::SyntaxTree &Tree) {
  for (diff::NodeId Id : Tree) {
    for (int I = 0; I < Tree.getNode(Id).Depth; ++I)
      OS << " ";
    printNode(OS, Tree, Id);
    OS << "\n";
  }
}

static void printDstChange(raw_ostream &OS, diff::ASTDiff &Diff,
                           diff::SyntaxTree &SrcTree, diff::SyntaxTree &DstTree,
                           diff::NodeId Dst) {
  const diff::Node &DstNode = DstTree.getNode(Dst);
  diff::NodeId Src = Diff.getMapped(DstTree, Dst);
  switch (DstNode.Change) {
  case diff::None:
    break;
  case diff::Delete:
    llvm_unreachable("The destination tree can't have deletions.");
  case diff::Update:
    OS << "Update ";
    printNode(OS, SrcTree, Src);
    OS << " to " << DstTree.getNodeValue(Dst) << "\n";
    break;
  case diff::Insert:
  case diff::Move:
  case diff::UpdateMove:
    if (DstNode.Change == diff::Insert)
      OS << "Insert";
    else if (DstNode.Change == diff::Move)
      OS << "Move";
    else if (DstNode.Change == diff::UpdateMove)
      OS << "Update and Move";
    OS << " ";
    printNode(OS, DstTree, Dst);
    OS << " into ";
    printNode(OS, DstTree, DstNode.Parent);
    OS << " at " << DstTree.findPositionInParent(Dst) << "\n";
    break;
  }
}

int main(int argc, const char **argv) {
  std::string ErrorMessage;
  std::unique_ptr<CompilationDatabase> CommonCompilations =
      FixedCompilationDatabase::loadFromCommandLine(argc, argv, ErrorMessage);
  if (!CommonCompilations && !ErrorMessage.empty())
    llvm::errs() << ErrorMessage;
  clv2::OptionParser P;
  ClangDiffOptions DiffOpts;
  {
    using ParsedT = decltype(ClangDiffReg)::ParsedOptionsT;
    auto *Storage = new ParsedT();
    decltype(ClangDiffReg)::applyDefaultsTo(*Storage);
    std::vector<clv2::detail::OptionEntry> Entries;
    std::vector<clv2::detail::AliasEntry> Aliases;
    std::vector<clv2::detail::SubCommandSpec> SubSpecs;
    decltype(ClangDiffReg)::staticBuildInto(*Storage, Entries, Aliases,
                                            SubSpecs);
    for (auto &E : Entries) {
      if (!E.Cat)
        E.Cat = &ClangDiffCategory;
      P.addDynamicEntry(std::move(E));
    }
    clv2::registerDynamicPostParseCallback(
        [Storage, &DiffOpts]() { applyClangDiffOpts(*Storage, DiffOpts); });
  }
  RegisterAllLLVMOptions(P);
  P.hideUnrelatedOptions({&ClangDiffCategory});
  P.parse(argc, argv);

  addExtraArgs(CommonCompilations, DiffOpts.ArgsBefore, DiffOpts.ArgsAfter);

  if (DiffOpts.ASTDump || DiffOpts.ASTDumpJson) {
    if (!DiffOpts.DestinationPath.empty()) {
      llvm::errs() << "Error: Please specify exactly one filename.\n";
      return 1;
    }
    std::unique_ptr<ASTUnit> AST =
        getAST(CommonCompilations, DiffOpts.SourcePath, DiffOpts);
    if (!AST)
      return 1;
    diff::SyntaxTree Tree(AST->getASTContext());
    if (DiffOpts.ASTDump) {
      printTree(llvm::outs(), Tree);
      return 0;
    }
    llvm::outs() << R"({"filename":")";
    printJsonString(llvm::outs(), DiffOpts.SourcePath);
    llvm::outs() << R"(","root":)";
    printNodeAsJson(llvm::outs(), Tree, Tree.getRootId());
    llvm::outs() << "}\n";
    return 0;
  }

  if (DiffOpts.DestinationPath.empty()) {
    llvm::errs() << "Error: Exactly two paths are required.\n";
    return 1;
  }

  std::unique_ptr<ASTUnit> Src =
      getAST(CommonCompilations, DiffOpts.SourcePath, DiffOpts);
  std::unique_ptr<ASTUnit> Dst =
      getAST(CommonCompilations, DiffOpts.DestinationPath, DiffOpts);
  if (!Src || !Dst)
    return 1;

  diff::ComparisonOptions Options;
  if (DiffOpts.MaxSize != -1)
    Options.MaxSize = DiffOpts.MaxSize;
  if (!DiffOpts.StopAfter.empty()) {
    if (DiffOpts.StopAfter == "topdown")
      Options.StopAfterTopDown = true;
    else if (DiffOpts.StopAfter != "bottomup") {
      llvm::errs() << "Error: Invalid argument for -stop-after\n";
      return 1;
    }
  }
  diff::SyntaxTree SrcTree(Src->getASTContext());
  diff::SyntaxTree DstTree(Dst->getASTContext());
  diff::ASTDiff Diff(SrcTree, DstTree, Options);

  if (DiffOpts.HtmlDiff) {
    llvm::outs() << HtmlDiffHeader << "<pre>";
    llvm::outs() << "<div id='L' class='code'>";
    printHtmlForNode(llvm::outs(), Diff, SrcTree, true, SrcTree.getRootId(), 0);
    llvm::outs() << "</div>";
    llvm::outs() << "<div id='R' class='code'>";
    printHtmlForNode(llvm::outs(), Diff, DstTree, false, DstTree.getRootId(),
                     0);
    llvm::outs() << "</div>";
    llvm::outs() << "</pre></div></body></html>\n";
    return 0;
  }

  for (diff::NodeId Dst : DstTree) {
    diff::NodeId Src = Diff.getMapped(DstTree, Dst);
    if (DiffOpts.PrintMatches && Src.isValid()) {
      llvm::outs() << "Match ";
      printNode(llvm::outs(), SrcTree, Src);
      llvm::outs() << " to ";
      printNode(llvm::outs(), DstTree, Dst);
      llvm::outs() << "\n";
    }
    printDstChange(llvm::outs(), Diff, SrcTree, DstTree, Dst);
  }
  for (diff::NodeId Src : SrcTree) {
    if (Diff.getMapped(SrcTree, Src).isInvalid()) {
      llvm::outs() << "Delete ";
      printNode(llvm::outs(), SrcTree, Src);
      llvm::outs() << "\n";
    }
  }

  return 0;
}
