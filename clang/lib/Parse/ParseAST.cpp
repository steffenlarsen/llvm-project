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
#include "TargetAlternationReconciler.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclFingerprint.h"
#include "clang/AST/ExternalASTSource.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/TargetCopyMerge.h"
#include "clang/Basic/MultiTargetDebugOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/ConditionalRegionRecorder.h"
#include "clang/Lex/TokenStreamMerge.h"
#include "clang/Parse/Parser.h"
#include "clang/Sema/CodeCompleteConsumer.h"
#include "clang/Sema/EnterExpressionEvaluationContext.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaConsumer.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/Format.h"
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



static void dumpTokenStream(ArrayRef<Token> Toks) {
  for (const Token &T : Toks)
    llvm::errs() << "TOK\t"
                 << llvm::format_hex_no_prefix(clang::hashToken(T), 16) << "\t"
                 << T.getName() << "\n";
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
      case tok::l_brace:
        ++Brace;
        break;
      case tok::r_brace:
        --Brace;
        break;
      case tok::l_paren:
        ++Paren;
        break;
      case tok::r_paren:
        --Paren;
        break;
      case tok::l_square:
        ++Square;
        break;
      case tok::r_square:
        --Square;
        break;
      default:
        break;
      }
    }
    if (!Brace && !Paren && !Square)
      ++Balanced;
    llvm::errs() << "REGION\t" << R.IfLoc.printToString(SM) << "\t"
                 << R.DirectiveLoc.printToString(SM) << "\t" << R.FirstToken
                 << "\t" << R.EndToken << "\t" << Brace << "\t" << Paren << "\t"
                 << Square << "\n";
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
/// from macro expansions, which carry the expansion's location. Alternation
/// markers are not: they borrow the location of the token after them.
static llvm::DenseMap<unsigned, unsigned>
buildLocationIndex(ArrayRef<Token> Toks) {
  llvm::DenseMap<unsigned, unsigned> Index;
  Index.reserve(Toks.size());
  for (auto [I, T] : llvm::enumerate(Toks))
    if (T.getLocation().isValid() &&
        !T.isOneOf(tok::annot_target_alt_begin, tok::annot_target_alt_sep,
                   tok::annot_target_alt_end))
      Index.try_emplace(T.getLocation().getRawEncoding(), I);
  return Index;
}

/// Positions in \p Toks where a declaration can end: brace depth zero counting
/// only bodies, so a member of a `namespace` or a class/struct/union/enum
/// counts and a statement inside a function does not.
static std::vector<bool> declarationBoundaries(ArrayRef<Token> Toks) {
  std::vector<bool> B(Toks.size() + 1, false);
  B.front() = B.back() = true;
  SmallVector<bool, 16> Open;
  unsigned BodyOpen = 0;
  // A braced initializer in parentheses, as in `decltype(T{})`, ends nothing.
  unsigned ParensOpen = 0;
  for (auto [I, T] : llvm::enumerate(Toks)) {
    tok::TokenKind K = T.getKind();
    if (K == tok::l_paren || K == tok::l_square)
      ++ParensOpen;
    else if ((K == tok::r_paren || K == tok::r_square) && ParensOpen)
      --ParensOpen;
    bool OpensNamespace = false;
    if (K == tok::l_brace) {
      // A container brace is preceded by `namespace [A::B]`, `extern "C"`, or
      // a class/struct/union/enum tag -- its interior is a sequence of
      // declarations, so a boundary can fall between two of them just as it
      // can between two top-level declarations.
      bool Container = isContainerBrace(Toks, I);
      OpensNamespace = Container;
      if (!Container)
        Container = isTagBrace(Toks, I);
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
    // A namespace's or linkage specification's first declaration starts
    // right after its `{`.
    // A `}` that a `,` follows closes an initializer in a list of
    // declarators.
    bool EndsHere =
        K == tok::semi ||
        (K == tok::r_brace && (I + 1 == Toks.size() ||
                               !Toks[I + 1].isOneOf(tok::semi, tok::comma))) ||
        OpensNamespace;
    if (!BodyOpen && !ParensOpen && EndsHere)
      B[I + 1] = true;
  }
  return B;
}


void clang::ParseAST(Sema &S, bool PrintStats, bool SkipFunctionBodies,
                     MutableArrayRef<TargetRecording> AuxRecordings,
                     llvm::function_ref<void(const Token &)> OnDrainedToken) {
  // Collect global stats on Decls/Stmts (until we have a module streamer).
  if (PrintStats) {
    Decl::EnableStatistics();
    Stmt::EnableStatistics();
  }

  // Also turn on collection of stats inside of the Sema object.
  bool OldCollectStats = PrintStats;
  std::swap(OldCollectStats, S.CollectStats);

  ASTConsumer *Consumer = &S.getASTConsumer();

  // The translation-unit boundary. Everything below -- parsing, every
  // HandleTopLevelDecl, and HandleTranslationUnit, which is where CodeGen
  // runs -- is work done for the target being compiled for.
  std::optional<ASTContext::TargetScope> TUTarget;
  if (LLVM_UNLIKELY(S.getASTContext().hasTargetDivergence()))
    TUTarget.emplace(S.getASTContext(), 1);

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
  bool ParseMergedStream = false;
  // A compile for one target records the other side only to check the merge,
  // and parses its own stream.
  const bool ParseAll = S.getASTContext().isMultiTargetCompile();

  // Installed before the main file is entered. Parser::Initialize primes the
  // look-ahead with one token, and producing it processes every directive in
  // every header up to the first real token -- a recorder installed after that
  // would see none of them.
  ConditionalRegionRecorder *Regions = nullptr;
  if (LLVM_UNLIKELY(multitarget::RecordReplayTokens) ||
      !AuxRecordings.empty()) {
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
    // there is nothing left to drain -- but other targets may still see
    // declarations of their own.
    const bool PrimaryEmpty = P.getCurToken().is(tok::eof);
    if ((LLVM_UNLIKELY(multitarget::RecordReplayTokens) ||
         !AuxRecordings.empty()) &&
        (!PrimaryEmpty || (!AuxRecordings.empty() && ParseAll))) {
      Preprocessor &PP = S.getPreprocessor();
      // Parser::Initialize primed the look-ahead with one token. Record it, so
      // this stream starts where every other target's does -- otherwise the
      // alignment is off by one and nothing matches -- and drop it again before
      // the parser is handed the stream, since it already holds that token.
      // The other targets' streams are about this long; growing to it a token
      // at a time copies and unmaps ever larger buffers.
      if (!AuxRecordings.empty())
        RecordedToks.reserve(AuxRecordings.front().Tokens.size() * 9 / 8);
      RecordedToks.push_back(P.getCurToken());
      if (OnDrainedToken)
        OnDrainedToken(P.getCurToken());

      Token T = P.getCurToken();
      while (T.isNot(tok::eof)) {
        PP.Lex(T);
        RecordedToks.push_back(T);
        if (OnDrainedToken)
          OnDrainedToken(T);
      }
      // With other targets' streams in hand, splice them together and parse the
      // merged stream instead. Recording 0 is this compilation's own, so its
      // tokens already belong to the identifier table and SourceManager the
      // parser uses; the others are translated as the stream is built.
      //
      // CompilerInstance rejects more targets than alignRecordings accepts.
      static_assert(MaxTargetRecordings == Decl::TargetVariantRedundant - 1);
      assert(AuxRecordings.size() < MaxTargetRecordings &&
             "more targets than alignRecordings accepts");
      if (!AuxRecordings.empty()) {
        // RecordedToks is replaced by (a projection of) the merged stream
        // below, so the recording moves rather than copies; so do the others'.
        TargetRecording Primary;
        Primary.Triple = PP.getTargetInfo().getTriple().str();
        Primary.Tokens = std::move(RecordedToks);
        RecordedToks.clear();
        Primary.Hashes.reserve(Primary.Tokens.size());
        for (const Token &T : Primary.Tokens)
          Primary.Hashes.push_back(clang::hashToken(T));
        Primary.Regions.assign(Regions->regions().begin(),
                               Regions->regions().end());
        Primary.RegionKeys.reserve(Primary.Regions.size());
        const SourceManager &SM = S.getSourceManager();
        for (const ConditionalRegion &R : Primary.Regions) {
          PresumedLoc PL = SM.getPresumedLoc(R.IfLoc);
          Primary.RegionKeys.push_back(PL.isInvalid()
                                           ? std::string()
                                           : (llvm::Twine(PL.getFilename()) +
                                              ":" + llvm::Twine(PL.getLine()) +
                                              ":" + llvm::Twine(PL.getColumn()))
                                                 .str());
        }

        SmallVector<TargetRecording, 6> All;
        All.push_back(std::move(Primary));
        All.append(std::make_move_iterator(AuxRecordings.begin()),
                   std::make_move_iterator(AuxRecordings.end()));

        // Kinds 0/1/2/3 below match the fixed set NumPragmaScopeKinds sizes
        // for: force_cuda_host_device, pack, attribute, GCC visibility. Their
        // payloads are Sema/Parse types TokenStreamMerge (in clangLex) cannot
        // depend on, so this classifier is built here and passed down to it.
        auto ClassifyPragmaScope = [](const Token &T) -> PragmaScopeEvent {
          switch (T.getKind()) {
          case tok::annot_pragma_force_cuda_host_device:
            return {0, T.getAnnotationValue() ? 1 : -1};
          case tok::annot_pragma_pack: {
            auto *Info =
                static_cast<Sema::PragmaPackInfo *>(T.getAnnotationValue());
            if (Info->Action & Sema::PSK_Push)
              return {1, 1};
            if (Info->Action & Sema::PSK_Pop)
              return {1, -1};
            return {};
          }
          case tok::annot_pragma_attribute: {
            int D = getPragmaAttributeScopeDelta(T);
            return D ? PragmaScopeEvent{2, D} : PragmaScopeEvent{};
          }
          case tok::annot_pragma_vis:
            // Non-null payload names the pushed visibility type (push);
            // null means pop -- see PragmaGCCVisibilityHandler::HandlePragma.
            return {3, T.getAnnotationValue() ? 1 : -1};
          default:
            return {};
          }
        };
        std::vector<MergedSegment> Segs =
            alignRecordings(All, /*Widen=*/true, ClassifyPragmaScope);
        if (LLVM_UNLIKELY(multitarget::DumpConditionalRegions)) {
          reportAlignment(llvm::errs(), All, Segs);
          reportDeclarationGranularity(llvm::errs(), All, Segs);
          reportDivergenceClosure(llvm::errs(), All, Segs);
        }
        MergedStream = buildMergedStream(PP, All, Segs);
        // The merge is only trustworthy if every target reads back out of it
        // exactly; widening changes the segmentation, so this has to hold after
        // it too.
        if (LLVM_UNLIKELY(multitarget::DumpConditionalRegions))
          verifyRoundTrip(llvm::errs(), All, MergedStream);
        if (ParseAll) {
          // Hand over the whole thing, markers included: the parser enters each
          // alternative's target scope in turn and records which target the
          // declarations it produces belong to. It is parsed in place; the
          // reparse below only reads it.
          ParseMergedStream = true;
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
      const bool ReprimeFromStream =
          ParseAll && !AuxRecordings.empty();
      if (!ReprimeFromStream)
        RecordedToks.erase(RecordedToks.begin());
      ArrayRef<Token> ToParse = ParseMergedStream
                                    ? ArrayRef<Token>(MergedStream)
                                    : ArrayRef<Token>(RecordedToks);

      // Reinjected: these tokens were already lexed once during recording, so
      // replaying them must not fire Preprocessor::OnToken or bump TokenCount
      // a second time. This is the same idiom late-parsed method bodies use.
      PP.EnterTokenStream(ToParse, /*DisableMacroExpansion=*/true,
                          /*IsReinject=*/true);
      if (ReprimeFromStream)
        P.ResetLookaheadFromStream();
      if (LLVM_UNLIKELY(multitarget::DumpTokenStream))
        dumpTokenStream(ToParse);
      if (LLVM_UNLIKELY(multitarget::DumpConditionalRegions))
        dumpConditionalRegions(S.getSourceManager(), ToParse,
                               Regions->regions());
    }

    Parser::DeclGroupPtrTy ADecl;
    Sema::ModuleImportState ImportState;
    EnterExpressionEvaluationContext PotentiallyEvaluated(
        S, Sema::ExpressionEvaluationContext::PotentiallyEvaluated);

    // Only built when it will be used: a map over a million tokens is not free.
    llvm::DenseMap<unsigned, unsigned> LocIndex;
    const bool Reparse =
        !MergedStream.empty() && S.getASTContext().isMultiTargetCompile();
    std::vector<bool> Boundaries;
    if (Reparse) {
      LocIndex = buildLocationIndex(MergedStream);
      Boundaries = declarationBoundaries(MergedStream);
    }
    P.InitializeTargetAlternationReconciler(S, Consumer, MergedStream, LocIndex,
                                            Boundaries, Reparse);

    for (bool AtEOF = P.ParseFirstTopLevelDecl(ADecl, ImportState); !AtEOF;
         AtEOF = P.ParseTopLevelDecl(ADecl, ImportState)) {
      if (!P.ObserveForTargetReconciliation(ADecl, /*NotifyConsumer=*/true))
        return;
    }

    if (!P.FinishTargetAlternationReconciliation(/*FinalFlush=*/true))
      return;
  }

  // Process any TopLevelDecls generated by #pragma weak.
  for (Decl *D : S.WeakTopLevelDecls())
    Consumer->HandleTopLevelDecl(DeclGroupRef(D));

  Consumer->HandleTranslationUnit(S.getASTContext());

  if (LLVM_UNLIKELY(multitarget::ReportVariantEquivalence))
    reportVariantEquivalence(llvm::errs(), S.getASTContext());

  if (LLVM_UNLIKELY(multitarget::DumpDeclFingerprints >= 0))
    printDeclFingerprints(llvm::outs(), S.getASTContext(),
                          multitarget::DumpDeclFingerprints);

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
