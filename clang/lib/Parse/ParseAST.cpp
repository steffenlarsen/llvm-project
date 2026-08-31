//===--- ParseAST.cpp - Provide the clang::ParseAST method ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the clang::ParseAST method.
//
//===----------------------------------------------------------------------===//

#include "clang/Parse/ParseAST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclFingerprint.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/AST/ExternalASTSource.h"
#include "clang/AST/Stmt.h"
#include "clang/Lex/ConditionalRegionRecorder.h"
#include "clang/Lex/TokenStreamMerge.h"
#include "clang/Parse/Parser.h"
#include "clang/Sema/CodeCompleteConsumer.h"
#include "clang/Sema/EnterExpressionEvaluationContext.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaConsumer.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/TimeProfiler.h"
#include <cstdio>
#include <memory>
#include <optional>
#include <vector>

using namespace clang;

namespace {

/// Resets LLVM's pretty stack state so that stack traces are printed correctly
/// when there are nested CrashRecoveryContexts and the inner one recovers from
/// a crash.
class ResetStackCleanup
    : public llvm::CrashRecoveryContextCleanupBase<ResetStackCleanup,
                                                   const void> {
public:
  ResetStackCleanup(llvm::CrashRecoveryContext *Context, const void *Top)
      : llvm::CrashRecoveryContextCleanupBase<ResetStackCleanup, const void>(
            Context, Top) {}
  void recoverResources() override {
    llvm::RestorePrettyStackState(resource);
  }
};

/// If a crash happens while the parser is active, an entry is printed for it.
class PrettyStackTraceParserEntry : public llvm::PrettyStackTraceEntry {
  const Parser &P;
public:
  PrettyStackTraceParserEntry(const Parser &p) : P(p) {}
  void print(raw_ostream &OS) const override;
};

/// If a crash happens while the parser is active, print out a line indicating
/// what the current token is.
void PrettyStackTraceParserEntry::print(raw_ostream &OS) const {
  const Token &Tok = P.getCurToken();
  if (Tok.is(tok::eof)) {
    OS << "<eof> parser at end of file\n";
    return;
  }

  if (Tok.getLocation().isInvalid()) {
    OS << "<unknown> parser at unknown location\n";
    return;
  }

  const Preprocessor &PP = P.getPreprocessor();
  Tok.getLocation().print(OS, PP.getSourceManager());
  if (Tok.isAnnotation()) {
    OS << ": at annotation token\n";
  } else {
    // Do the equivalent of PP.getSpelling(Tok) except for the parts that would
    // allocate memory.
    bool Invalid = false;
    const SourceManager &SM = P.getPreprocessor().getSourceManager();
    unsigned Length = Tok.getLength();
    const char *Spelling = SM.getCharacterData(Tok.getLocation(), &Invalid);
    if (Invalid) {
      OS << ": unknown current parser token\n";
      return;
    }
    OS << ": current parser token '" << StringRef(Spelling, Length) << "'\n";
  }
}

}  // namespace

//===----------------------------------------------------------------------===//
// Public interface to the file
//===----------------------------------------------------------------------===//

/// ParseAST - Parse the entire file specified, notifying the ASTConsumer as
/// the file is parsed.  This inserts the parsed decls into the translation unit
/// held by Ctx.
///
/// PROTOTYPE (Stage 3.1): record the whole expanded token stream before
/// parsing, then parse from the recording rather than from the lexer. This is
/// the vehicle a merged multi-target stream needs: Stage 3 preprocesses once
/// per target and splices the recordings, so the parser must be able to run
/// off a recording at all.
static llvm::cl::opt<bool> RecordReplayTokens(
    "record-replay-tokens", llvm::cl::Hidden, llvm::cl::init(false),
    llvm::cl::desc("Prototype: parse from a recorded token stream rather than "
                   "directly from the lexer"));

/// Largest declaration, in tokens, worth re-parsing. Beyond this the extent was
/// almost certainly mis-derived from a source location rather than being a
/// genuinely huge declaration.
static void traceReject(Parser::DeclGroupPtrTy &Group, const char *Why,
                        unsigned Tokens, Sema &S);

static llvm::cl::opt<bool> TraceReparse(
    "trace-reparse", llvm::cl::Hidden,
    llvm::cl::desc("Prototype: report each declaration that is re-parsed"));

static void traceReject(Parser::DeclGroupPtrTy &Group, const char *Why,
                        unsigned Tokens, Sema &S) {
  if (LLVM_LIKELY(!TraceReparse))
    return;
  for (Decl *RD : Group.get())
    llvm::errs() << "reject " << Why << " " << Tokens << "t "
                 << RD->getDeclKindName() << " "
                 << (isa<NamedDecl>(RD) ? cast<NamedDecl>(RD)->getNameAsString()
                                        : std::string("<unnamed>"))
                 << " @ " << RD->getLocation().printToString(S.getSourceManager())
                 << "\n";
}

static llvm::cl::opt<unsigned> ReparseTokenCap(
    "reparse-token-cap", llvm::cl::Hidden, llvm::cl::init(20000),
    llvm::cl::desc("Prototype: skip re-parsing declarations wider than this"));

/// PROTOTYPE (Stage 4.2b-v): re-parse a shared declaration for the other
/// targets when its name lookups reached a target-specific entity.
///
/// Shared code is parsed once, so such a reference can only mean one target --
/// today the primary's, silently. Measured on ggml, 0.40-0.48% of declarations
/// do this, so re-parsing just those is cheap. Requires
/// -parse-all-target-alternatives.
static llvm::cl::opt<bool> ReparseDivergentUsers(
    "reparse-divergent-users", llvm::cl::Hidden, llvm::cl::init(false),
    llvm::cl::desc("Prototype: re-parse shared declarations that reference a "
                   "target-specific entity, once per target"));

/// PROTOTYPE (Stage 4.2): parse every target's alternative rather than only
/// the one being compiled for, producing a single AST that carries all of them.
/// Requires -widen-target-alternatives, since an alternative can only be parsed
/// on its own if it is a sequence of complete declarations.
static llvm::cl::opt<bool> ParseAllAlternatives(
    "parse-all-target-alternatives", llvm::cl::Hidden, llvm::cl::init(false),
    llvm::cl::desc("Prototype: parse each target's alternative and mark the "
                   "declarations with the target they belong to"));

/// PROTOTYPE (Stage 4.2): widen divergent regions to whole declarations, so
/// each alternative can be parsed on its own.
static llvm::cl::opt<bool> WidenAlternatives(
    "widen-target-alternatives", llvm::cl::Hidden, llvm::cl::init(false),
    llvm::cl::desc("Prototype: grow divergent token regions until they start "
                   "and end between top-level declarations"));

/// PROTOTYPE (Stage 4.2b-v): check the cheap tagging signal against structural
/// equivalence before merging anything on the strength of it.
static llvm::cl::opt<bool> ReportVariantEquivalence(
    "report-variant-equivalence", llvm::cl::Hidden, llvm::cl::init(false),
    llvm::cl::desc("Prototype: report how many target-tagged declarations are "
                   "structurally the same"));

/// PROTOTYPE (Stage 4): print a fingerprint of the AST for one target, so a
/// combined compilation can be checked against separate per-target ones once
/// byte-identical output is no longer available. -1 disables.
static llvm::cl::opt<int> DumpDeclFingerprints(
    "dump-decl-fingerprints", llvm::cl::Hidden, llvm::cl::init(-1),
    llvm::cl::desc("Prototype: print one line per declaration of the given "
                   "target variant; 0 prints all of them"));

/// PROTOTYPE (Stage 3.3): dump a content hash per recorded token, so two
/// targets' recordings can be aligned outside the compiler while the alignment
/// algorithm is being settled. Identifier and literal text is hashed rather
/// than pointers, which are not comparable across processes.
static llvm::cl::opt<bool> DumpTokenStream(
    "dump-token-stream", llvm::cl::Hidden, llvm::cl::init(false),
    llvm::cl::desc("Prototype: print a content hash for each recorded token"));

/// PROTOTYPE (Stage 3.2): dump the conditional-token-range map the recording
/// builds, so the boundaries a multi-target splice will use can be checked
/// against the source before anything is spliced.
static llvm::cl::opt<bool> DumpConditionalRegions(
    "dump-conditional-regions", llvm::cl::Hidden, llvm::cl::init(false),
    llvm::cl::desc("Prototype: print the token range each #if branch "
                   "contributed to the recorded stream"));

/// PROTOTYPE (Stage 1.6): the target variant the translation unit is compiled
/// for. Setting it to 0 leaves no target in scope, which is what the frontend
/// did before this increment and how the missing-scope diagnostic is tested.
static llvm::cl::opt<unsigned> TUTargetVariant(
    "tu-target-variant", llvm::cl::Hidden, llvm::cl::init(1),
    llvm::cl::desc("Prototype: target variant the translation unit compiles "
                   "for; 0 leaves no target in scope"));



static void dumpTokenStream(ArrayRef<Token> Toks) {
  for (const Token &T : Toks)
    llvm::errs() << "TOK\t" << llvm::format_hex_no_prefix(clang::hashToken(T), 16)
                 << "\t" << T.getName() << "\n";
}

/// Report the recorded conditional regions.
///
/// The property a multi-target splice depends on is *not* that each branch is
/// bracket-balanced on its own. Real code writes
///
///   #if AMD_MFMA_AVAILABLE
///           if (i < I) {
///   #else
///           {
///   #endif
///
/// where every branch opens one brace that is closed after the #endif. Each
/// branch is individually unbalanced; what matters is that the branches *agree*
/// on the bracket delta, so that splicing them as alternatives leaves the
/// parser in the same state whichever it takes. Agreement can only be checked
/// across passes -- the branch skipped in one target's pass is the one taken in
/// another's -- so this emits per-region deltas for an external comparison and
/// summarises what a single pass can see.
static void dumpConditionalRegions(const SourceManager &SM,
                                   ArrayRef<Token> Toks,
                                   ArrayRef<ConditionalRegion> Regions) {
  unsigned Balanced = 0, Nonempty = 0, Empty = 0, Spanned = 0;
  for (const ConditionalRegion &R : Regions) {
    if (R.empty()) {
      ++Empty;
      continue;
    }
    ++Nonempty;
    if (R.Depth == 0)
      Spanned += R.size();
    int Brace = 0, Paren = 0, Square = 0;
    for (unsigned I = R.FirstToken; I != R.EndToken; ++I) {
      switch (Toks[I].getKind()) {
      case tok::l_brace:   ++Brace;  break;
      case tok::r_brace:   --Brace;  break;
      case tok::l_paren:   ++Paren;  break;
      case tok::r_paren:   --Paren;  break;
      case tok::l_square:  ++Square; break;
      case tok::r_square:  --Square; break;
      default: break;
      }
    }
    if (!Brace && !Paren && !Square)
      ++Balanced;
    llvm::errs() << "REGION\t" << R.IfLoc.printToString(SM) << "\t"
                 << R.DirectiveLoc.printToString(SM) << "\t" << R.FirstToken
                 << "\t" << R.EndToken << "\t" << Brace << "\t" << Paren
                 << "\t" << Square << "\n";
  }
  llvm::errs() << "conditional regions: " << Regions.size() << " branches ("
               << Nonempty << " with tokens, " << Empty << " skipped/empty)\n"
               << "  recorded tokens:    " << Toks.size() << "\n"
               << "  in depth-0 regions: " << Spanned;
  if (!Toks.empty())
    llvm::errs() << "  (" << (100 * Spanned / Toks.size()) << "%)";
  llvm::errs() << "\n  self-balanced:      " << Balanced << " of " << Nonempty;
  if (Nonempty)
    llvm::errs() << "  (" << (100 * Balanced / Nonempty) << "%)";
  llvm::errs() << "\n";
}

void clang::ParseAST(Preprocessor &PP, ASTConsumer *Consumer,
                     ASTContext &Ctx, bool PrintStats,
                     TranslationUnitKind TUKind,
                     CodeCompleteConsumer *CompletionConsumer,
                     bool SkipFunctionBodies) {

  std::unique_ptr<Sema> S(
      new Sema(PP, Ctx, *Consumer, TUKind, CompletionConsumer));

  // Recover resources if we crash before exiting this method.
  llvm::CrashRecoveryContextCleanupRegistrar<Sema> CleanupSema(S.get());

  ParseAST(*S, PrintStats, SkipFunctionBodies);
}


/// Token index of each position in a stream, for finding a declaration's extent
/// after it has been parsed. Locations are unique per token, including those
/// from macro expansions, which carry the expansion's location.
static llvm::DenseMap<unsigned, unsigned>
buildLocationIndex(ArrayRef<Token> Toks) {
  llvm::DenseMap<unsigned, unsigned> Index;
  Index.reserve(Toks.size());
  for (auto [I, T] : llvm::enumerate(Toks))
    if (T.getLocation().isValid())
      Index.try_emplace(T.getLocation().getRawEncoding(), I);
  return Index;
}


/// Positions in \p Toks where a declaration can end: brace depth zero counting
/// only bodies, so a member of a `namespace` counts and a statement inside a
/// function does not.
static std::vector<bool> declarationBoundaries(ArrayRef<Token> Toks) {
  std::vector<bool> B(Toks.size() + 1, false);
  B.front() = B.back() = true;
  SmallVector<bool, 16> Open;
  unsigned BodyOpen = 0;
  for (auto [I, T] : llvm::enumerate(Toks)) {
    tok::TokenKind K = T.getKind();
    if (K == tok::l_brace) {
      // A container brace is preceded by `namespace [A::B]` or `extern "C"`.
      bool Container = false;
      if (I >= 2 && Toks[I - 1].is(tok::string_literal) &&
          Toks[I - 2].is(tok::kw_extern))
        Container = true;
      else {
        unsigned Q = I;
        while (Q && (Toks[Q - 1].is(tok::identifier) ||
                     Toks[Q - 1].is(tok::coloncolon)))
          --Q;
        Container = Q && Toks[Q - 1].is(tok::kw_namespace);
      }
      Open.push_back(Container);
      BodyOpen += !Container;
    } else if (K == tok::r_brace && !Open.empty()) {
      BodyOpen -= !Open.pop_back_val();
    }
    // A `}` that a `;` follows does not end the declaration -- the `;` does.
    // Marking the gap between them split every `struct X { ... };` in two, and
    // the re-parse then rejected all of them as spanning several
    // declarations: 13 of 37 rejections on binbcast, including every fp8 type
    // whose constructors call the divergent `cast_from_f8`.
    bool EndsHere = K == tok::semi || (K == tok::r_brace &&
                                       (I + 1 == Toks.size() ||
                                        !Toks[I + 1].is(tok::semi)));
    if (!BodyOpen && EndsHere)
      B[I + 1] = true;
  }
  return B;
}

/// The half-open token range \p Group occupies, or an empty range if it cannot
/// be located -- a declaration built from tokens the merged stream does not
/// contain verbatim cannot be re-parsed from it.
static std::pair<unsigned, unsigned>
tokenRangeOf(Parser::DeclGroupPtrTy Group,
             const llvm::DenseMap<unsigned, unsigned> &Index,
             ArrayRef<Token> Toks) {
  unsigned Lo = ~0u, Hi = 0;
  for (Decl *D : Group.get()) {
    // A group can carry declarations that were never written -- implicit
    // members, injected names -- which have no token of their own. They are
    // skipped rather than treated as a failure to locate the group.
    auto B = Index.find(D->getBeginLoc().getRawEncoding());
    auto E = Index.find(D->getEndLoc().getRawEncoding());
    if (B == Index.end() || E == Index.end())
      continue;
    Lo = std::min(Lo, B->second);
    Hi = std::max(Hi, E->second + 1);
  }
  if (Lo == ~0u)
    return {0, 0};
  // A declaration ends on ';' or '}'; getEndLoc names the token before it for
  // some kinds, so take the following semicolon if there is one.
  if (Hi && Hi < Toks.size() && Toks[Hi].is(tok::semi))
    ++Hi;
  return Lo <= Hi ? std::make_pair(Lo, Hi) : std::make_pair(0u, 0u);
}

/// An alternation marker, so the re-parse goes through the same path a spliced
/// alternative does: the parser enters the target's scope and marks whatever it
/// declares.
static Token altMarker(tok::TokenKind Kind, SourceLocation Loc, uintptr_t V) {
  Token T;
  T.startToken();
  T.setKind(Kind);
  T.setLocation(Loc);
  T.setAnnotationEndLoc(Loc);
  T.setAnnotationValue(reinterpret_cast<void *>(V));
  return T;
}

void clang::ParseAST(Sema &S, bool PrintStats, bool SkipFunctionBodies,
                     ArrayRef<TargetRecording> AuxRecordings) {
  // Collect global stats on Decls/Stmts (until we have a module streamer).
  if (PrintStats) {
    Decl::EnableStatistics();
    Stmt::EnableStatistics();
  }

  // Also turn on collection of stats inside of the Sema object.
  bool OldCollectStats = PrintStats;
  std::swap(OldCollectStats, S.CollectStats);

  ASTConsumer *Consumer = &S.getASTConsumer();

  // PROTOTYPE (Stage 1.6): the translation-unit boundary. Everything below --
  // parsing, every HandleTopLevelDecl, and HandleTranslationUnit, which is
  // where CodeGen runs -- is work done for the target being compiled for.
  // Stage 5 subdivides this; today it is the frame the others nest inside.
  std::optional<ASTContext::TargetScope> TUTarget;
  if (LLVM_UNLIKELY(S.getASTContext().hasTargetDivergence()))
    TUTarget.emplace(S.getASTContext(), TUTargetVariant.getValue());

  std::unique_ptr<Parser> ParseOP(
      new Parser(S.getPreprocessor(), S, SkipFunctionBodies));
  Parser &P = *ParseOP;

  llvm::CrashRecoveryContextCleanupRegistrar<const void, ResetStackCleanup>
      CleanupPrettyStack(llvm::SavePrettyStackState());
  PrettyStackTraceParserEntry CrashInfo(P);

  // Recover resources if we crash before exiting this method.
  llvm::CrashRecoveryContextCleanupRegistrar<Parser>
    CleanupParser(ParseOP.get());

  // The recording has to outlive parsing: EnterTokenStream does not copy.
  std::vector<Token> RecordedToks;
  std::vector<Token> MergedStream;

  // Installed before the main file is entered. Parser::Initialize primes the
  // look-ahead with one token, and producing it processes every directive in
  // every header up to the first real token -- a recorder installed after that
  // would see none of them.
  ConditionalRegionRecorder *Regions = nullptr;
  if (LLVM_UNLIKELY(RecordReplayTokens) || !AuxRecordings.empty()) {
    auto Owned = std::make_unique<ConditionalRegionRecorder>(RecordedToks);
    Regions = Owned.get();
    S.getPreprocessor().addPPCallbacks(std::move(Owned));
  }

  S.getPreprocessor().EnterMainSourceFile();
  ExternalASTSource *External = S.getASTContext().getExternalSource();
  if (External)
    External->StartTranslationUnit(Consumer);

  // If a PCH through header is specified that does not have an include in
  // the source, or a PCH is being created with #pragma hdrstop with nothing
  // after the pragma, there won't be any tokens or a Lexer.
  bool HaveLexer = S.getPreprocessor().getCurrentLexer();

  if (HaveLexer) {
    llvm::TimeTraceScope TimeScope("Frontend", [&]() {
      llvm::TimeTraceMetadata M;
      if (llvm::isTimeTraceVerbose()) {
        const SourceManager &SM = S.getSourceManager();
        if (const auto *FE = SM.getFileEntryForID(SM.getMainFileID()))
          M.File = FE->tryGetRealPathName();
      }
      return M;
    });
    P.Initialize();

    // Draining has to happen after Parser::Initialize, which establishes
    // Sema's translation-unit scope but consumes no token. The Parser installs
    // Preprocessor callbacks -- comment handling and several pragmas -- that
    // reach into Sema as they are lexed, and those crash or misfire if the
    // stream is drained before Sema has a context.
    // Initialize primes the look-ahead with one ConsumeToken, so for a
    // translation unit with no declarations that token is already the eof and
    // there is nothing left to drain.
    if ((LLVM_UNLIKELY(RecordReplayTokens) || !AuxRecordings.empty()) &&
        P.getCurToken().isNot(tok::eof) &&
        P.getCurToken().isNot(tok::annot_repl_input_end)) {
      Preprocessor &PP = S.getPreprocessor();
      // Parser::Initialize primed the look-ahead with one token. Record it, so
      // this stream starts where every other target's does -- otherwise the
      // alignment is off by one and nothing matches -- and drop it again before
      // the parser is handed the stream, since it already holds that token.
      RecordedToks.push_back(P.getCurToken());

      // -fincremental-extensions makes the preprocessor end a chunk with
      // annot_repl_input_end instead of eof, so stopping only at eof spins
      // forever.
      Token T;
      do {
        PP.Lex(T);
        RecordedToks.push_back(T);
      } while (T.isNot(tok::eof) && T.isNot(tok::annot_repl_input_end));
      // With other targets' streams in hand, splice them together and parse the
      // merged stream instead. Recording 0 is this compilation's own, so its
      // tokens already belong to the identifier table and SourceManager the
      // parser uses; the others are translated as the stream is built.
      if (!AuxRecordings.empty()) {
        TargetRecording Primary;
        Primary.Triple = PP.getTargetInfo().getTriple().str();
        Primary.Tokens = RecordedToks;
        Primary.Hashes.reserve(RecordedToks.size());
        for (const Token &T : RecordedToks)
          Primary.Hashes.push_back(clang::hashToken(T));
        Primary.Regions.assign(Regions->regions().begin(),
                               Regions->regions().end());
        Primary.RegionKeys.reserve(Primary.Regions.size());
        const SourceManager &SM = S.getSourceManager();
        for (const ConditionalRegion &R : Primary.Regions) {
          PresumedLoc PL = SM.getPresumedLoc(R.IfLoc);
          Primary.RegionKeys.push_back(
              PL.isInvalid() ? std::string()
                             : (llvm::Twine(PL.getFilename()) + ":" +
                                llvm::Twine(PL.getLine()) + ":" +
                                llvm::Twine(PL.getColumn()))
                                   .str());
        }

        SmallVector<TargetRecording, 2> All;
        All.push_back(std::move(Primary));
        All.append(AuxRecordings.begin(), AuxRecordings.end());

        std::vector<MergedSegment> Segs =
            alignRecordings(All, WidenAlternatives || ParseAllAlternatives);
        if (LLVM_UNLIKELY(DumpConditionalRegions)) {
          reportAlignment(llvm::errs(), All, Segs);
          reportDeclarationGranularity(llvm::errs(), All, Segs);
          reportDivergenceClosure(llvm::errs(), All, Segs);
        }
        MergedStream = buildMergedStream(PP, All, Segs);
        // The merge is only trustworthy if every target reads back out of it
        // exactly; widening changes the segmentation, so this has to hold after
        // it too.
        if (LLVM_UNLIKELY(DumpConditionalRegions))
          verifyRoundTrip(llvm::errs(), All, MergedStream);
        if (ParseAllAlternatives) {
          // Hand over the whole thing, markers included: the parser enters each
          // alternative's target scope in turn and records which target the
          // declarations it produces belong to.
          RecordedToks = MergedStream;
        } else {
          // Otherwise hand it the stream this target would have seen. That is a
          // strict test of the merge: anything lost or misplaced shows up as a
          // changed compilation.
          RecordedToks = selectTarget(MergedStream, 0);
        }
      }

      // Drop the primed token: the parser is already holding it. Not when the
      // whole merged stream is being parsed, though -- that stream contains the
      // primed token itself, and may open with an alternation marker before it,
      // so the parser has to re-read from the stream instead.
      const bool ReprimeFromStream = ParseAllAlternatives && !AuxRecordings.empty();
      if (!ReprimeFromStream)
        RecordedToks.erase(RecordedToks.begin());

      // Reinjected: these tokens were already lexed once during recording, so
      // replaying them must not fire Preprocessor::OnToken or bump TokenCount
      // a second time. This is the same idiom late-parsed method bodies use.
      PP.EnterTokenStream(RecordedToks, /*DisableMacroExpansion=*/true,
                          /*IsReinject=*/true);
      if (ReprimeFromStream)
        P.ResetLookaheadFromStream();
      if (LLVM_UNLIKELY(DumpTokenStream))
        dumpTokenStream(RecordedToks);
      if (LLVM_UNLIKELY(DumpConditionalRegions))
        dumpConditionalRegions(S.getSourceManager(), RecordedToks,
                               Regions->regions());
    }

    Parser::DeclGroupPtrTy ADecl;
    Sema::ModuleImportState ImportState;
    EnterExpressionEvaluationContext PotentiallyEvaluated(
        S, Sema::ExpressionEvaluationContext::PotentiallyEvaluated);

    // Only built when it will be used: a map over a million tokens is not free.
    llvm::DenseMap<unsigned, unsigned> LocIndex;
    const bool Reparse =
        ReparseDivergentUsers && !AuxRecordings.empty() && !MergedStream.empty();
    std::vector<bool> Boundaries;
    if (Reparse) {
      LocIndex = buildLocationIndex(MergedStream);
      Boundaries = declarationBoundaries(MergedStream);
    }
    // Re-entered token buffers must outlive the parse that reads them.
    std::vector<std::unique_ptr<std::vector<Token>>> Reparsed;
    unsigned ReparseCandidates = 0, ReparseRejected = 0, ReparseTokens = 0,
             ReparseWidest = 0;

    for (bool AtEOF = P.ParseFirstTopLevelDecl(ADecl, ImportState); !AtEOF;
         AtEOF = P.ParseTopLevelDecl(ADecl, ImportState)) {
      // If we got a null return and something *was* parsed, ignore it.  This
      // is due to a top-level semicolon, an action override, or a parse error
      // skipping something.
      if (ADecl && !Consumer->HandleTopLevelDecl(ADecl.get()))
        return;

      // A shared declaration whose lookups reached a target-specific entity
      // means something different for each target, so parse it again for each
      // of the others. Bracketing the tokens with alternation markers routes it
      // through the same path a spliced alternative takes: the parser enters
      // that target's scope and marks whatever it declares.
      // Only shared declarations are candidates. A re-parsed copy arrives back
      // through this same loop, and without this it would be re-parsed again --
      // and its copies re-parsed, without end.
      if (!Reparse || !S.TouchedDivergentEntity || !ADecl ||
          P.inTargetAlternative()) {
        S.TouchedDivergentEntity = false;
        continue;
      }
      S.TouchedDivergentEntity = false;
      auto [Lo, Hi] = tokenRangeOf(ADecl, LocIndex, MergedStream);
      ++ReparseCandidates;
      // A declaration that CUDA attributes already pin to one target has one
      // meaning, so there is nothing for a second target to parse differently.
      // `__global__` is the sharpest case -- re-parsing `cumsum_kernel` for the
      // host produced a kernel nothing could launch and broke two-phase lookup
      // at its call site -- but `__device__`-only and `__host__`-only are the
      // same argument. 280 of the 438 host-primary re-parses were the
      // `__device__` overloads in `__clang_hip_cmath.h`, and re-parsing them
      // gave `abs` a second device definition.
      if (llvm::any_of(ADecl.get(), [](Decl *RD) {
            const auto *FD = dyn_cast<FunctionDecl>(RD);
            if (const auto *FTD = dyn_cast<FunctionTemplateDecl>(RD))
              FD = FTD->getTemplatedDecl();
            if (!FD)
              return false;
            return FD->hasAttr<CUDAGlobalAttr>() ||
                   (FD->hasAttr<CUDADeviceAttr>() !=
                    FD->hasAttr<CUDAHostAttr>());
          })) {
        ++ReparseRejected;
        traceReject(ADecl, "single-target", Hi - Lo, S);
        continue;
      }
      // A namespace or a linkage specification is a container, not an entity.
      // The divergent lookup happened in one declaration inside it, and
      // re-parsing the whole block re-creates every other declaration in it and
      // claims them all for the primary target: 72 of 100 candidates were
      // `namespace std`, 97.5% of the re-parsed tokens, and six copies of
      // std::basic_string, whose parameter list then had no default arguments.
      if (llvm::any_of(ADecl.get(), [](Decl *RD) {
            return isa<NamespaceDecl>(RD) || isa<LinkageSpecDecl>(RD);
          })) {
        ++ReparseRejected;
        traceReject(ADecl, "container", Hi - Lo, S);
        continue;
      }
      // A declaration's extent is derived from its begin and end locations, and
      // a location that maps to the wrong token gives a range spanning much of
      // the file. Re-entering that is not a slow path, it is an unbounded one.
      if (Lo == Hi || Hi - Lo > ReparseTokenCap) {
        ++ReparseRejected;
        ReparseWidest = std::max<unsigned>(ReparseWidest, Hi - Lo);
        traceReject(ADecl, Lo == Hi ? "empty-range" : "over-cap", Hi - Lo, S);
        continue;
      }
      // The extent comes from source locations, and one that maps to the wrong
      // token gives a range covering neighbouring declarations too. Re-parsing
      // those re-creates them, and the copies are visible alongside the
      // originals: 346 "call is ambiguous", both candidates at the same line.
      // A re-parse must cover exactly one declaration.
      if (Lo && !Boundaries[Lo]) {
        ++ReparseRejected;
        traceReject(ADecl, "not-at-a-boundary", Hi - Lo, S);
        continue;
      }
      unsigned ChunkEnd = Lo;
      do
        ++ChunkEnd;
      while (ChunkEnd < Boundaries.size() - 1 && !Boundaries[ChunkEnd]);
      if (Hi > ChunkEnd) {
        ++ReparseRejected;
        traceReject(ADecl, "spans-several-declarations", Hi - Lo, S);
        continue;
      }
      Hi = ChunkEnd;
      ReparseTokens += Hi - Lo;
      if (LLVM_UNLIKELY(TraceReparse))
        for (Decl *RD : ADecl.get())
          llvm::errs() << "reparse " << (Hi - Lo) << "t " << RD->getDeclKindName()
                       << " "
                       << (isa<NamedDecl>(RD)
                               ? cast<NamedDecl>(RD)->getNameAsString()
                               : std::string("<unnamed>"))
                       << " @ "
                       << RD->getLocation().printToString(S.getSourceManager())
                       << "\n";

      // Claim it for the primary first: while it still belongs to every target,
      // lookup during the re-parse finds it and merges the new declaration into
      // it, so nothing per-target is produced.
      P.ClaimForTargetVariant(ADecl, 1);

      SourceLocation Loc = MergedStream[Lo].getLocation();
      auto Buf = std::make_unique<std::vector<Token>>();
      for (unsigned V = 2; V <= AuxRecordings.size() + 1; ++V) {
        // annot_target_alt_sep carries a 0-based alternative index.
        Buf->push_back(altMarker(tok::annot_target_alt_sep, Loc,
                                 (V - 1) | Parser::FallbackReparseBit));
        Buf->insert(Buf->end(), MergedStream.begin() + Lo,
                    MergedStream.begin() + Hi);
        Buf->push_back(altMarker(tok::annot_target_alt_end, Loc, 0));
      }
      // The parser is already holding the first token of the *next*
      // declaration. A stream entered now would be spliced after it, cutting
      // that declaration in half, so it is appended and the look-ahead re-read
      // from the stream -- the same thing late-parsed method bodies do.
      Buf->push_back(P.getCurToken());
      S.getPreprocessor().EnterTokenStream(*Buf, /*DisableMacroExpansion=*/true,
                                           /*IsReinject=*/true);
      P.ResetLookaheadFromStream();
      Reparsed.push_back(std::move(Buf));
    }

    if (Reparse && LLVM_UNLIKELY(DumpConditionalRegions))
      llvm::errs() << "re-parsed divergent users: " << ReparseCandidates
                   << " candidates, " << ReparseRejected << " rejected (widest "
                   << ReparseWidest << " tokens), " << ReparseTokens
                   << " tokens re-parsed\n";
  }

  // Process any TopLevelDecls generated by #pragma weak.
  for (Decl *D : S.WeakTopLevelDecls())
    Consumer->HandleTopLevelDecl(DeclGroupRef(D));

  Consumer->HandleTranslationUnit(S.getASTContext());

  if (LLVM_UNLIKELY(clang::CountDivergentUses))
    llvm::errs() << "divergent users: " << S.DivergentUsers.size()
                 << " declarations in shared code reference a target-specific "
                    "entity\n";

  if (LLVM_UNLIKELY(ReportVariantEquivalence))
    reportVariantEquivalence(llvm::errs(), S.getASTContext());

  if (LLVM_UNLIKELY(DumpDeclFingerprints >= 0))
    printDeclFingerprints(llvm::outs(), S.getASTContext(),
                          DumpDeclFingerprints);

  std::swap(OldCollectStats, S.CollectStats);
  if (PrintStats) {
    llvm::errs() << "\nSTATISTICS:\n";
    if (HaveLexer) P.getActions().PrintStats();
    S.getASTContext().PrintStats();
    Decl::PrintStats();
    Stmt::PrintStats();
    Consumer->PrintStats();
  }
}
