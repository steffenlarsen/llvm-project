//===--- BackendConsumer.h - LLVM BackendConsumer Header File -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_BACKENDCONSUMER_H
#define LLVM_CLANG_LIB_CODEGEN_BACKENDCONSUMER_H

#include "clang/CodeGen/BackendUtil.h"
#include "clang/CodeGen/CodeGenAction.h"
#include "clang/CodeGen/ModuleLinker.h"
#include "clang/CodeGenUtils/BackendDiagnosticHandler.h"

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/Support/Timer.h"

namespace clang {
class ASTContext;
class CodeGenAction;
class CompilerInvocation;
class CoverageSourceInfo;
class TargetInfo;

class BackendConsumer : public ASTConsumer {
  virtual void anchor();
  CompilerInstance &CI;
  DiagnosticsEngine &Diags;
  const CodeGenOptions &CodeGenOpts;
  const TargetOptions &TargetOpts;
  const LangOptions &LangOpts;
  std::unique_ptr<raw_pwrite_stream> AsmOutStream;
  ASTContext *Context = nullptr;
  IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS;

  llvm::Timer LLVMIRGeneration;
  unsigned LLVMIRGenerationRefCount = 0;

  bool TimerIsEnabled = false;

  BackendAction Action;

  std::unique_ptr<CodeGenerator> Gen;

public:
  /// One entry per configured aux (device-arch) target. Each entry drives
  /// its own second CodeGenModule pipeline off the same shared AST as Gen,
  /// selecting its own Decl::TargetVariant. Only produces an in-memory
  /// llvm::Module: unlike Gen, no entry's module is linked, embedded, or run
  /// through the backend here.
  struct AuxGenEntry {
    /// This entry's Decl::TargetVariant (2..N+1).
    unsigned Variant;
    std::unique_ptr<CodeGenerator> Gen;
    /// Populated from -multi-target-aux-output=<variant>:<path>; null
    /// unless that flag configures this variant, in which case this
    /// entry's module is produced and discarded rather than written out.
    std::unique_ptr<raw_pwrite_stream> AsmOutStream;
    std::string OutputPath;
    const TargetInfo *TI = nullptr;
    /// What the entry is packaged as, e.g. gfx90a:xnack+.
    std::string TargetID;
    /// This entry's own -mlink-builtin-bitcode modules, from Invocation's
    /// LinkBitcodeFiles rather than the primary's: each arch links its own
    /// arch-specific set (e.g. oclc_isa_version_942.bc vs _900.bc).
    SmallVector<LinkModule, 4> LinkModules;
    /// This entry's own backend action. Hardcoded to bitcode-only: every
    /// device-arch cc1 job in the real HIP pipeline only emits
    /// -emit-llvm-bc; per-arch ISA codegen happens downstream in
    /// clang-linker-wrapper, out of this entry's own tail.
    BackendAction Action = Backend_EmitBC;
    /// This entry's own options, from -multi-target-aux-invocation, or
    /// synthesized from the primary's when there is none. Gen reads its
    /// CodeGenOptions, HeaderSearchOptions and PreprocessorOptions directly.
    std::shared_ptr<CompilerInvocation> Invocation;
    /// The LangOptions fields, by LangOptionField, that may differ per target
    /// and that Invocation sets differently from the primary. CodeGenModule
    /// reads the ASTContext's shared LangOptions, so these are swapped in
    /// around every call into Gen.
    struct LangOptValue {
      unsigned Field;
      unsigned Value;
    };
    SmallVector<LangOptValue, 8> LangOptDiffs;
  };

private:
  SmallVector<AuxGenEntry, 4> AuxGens;
  std::string InFile;
  CoverageSourceInfo *CoverageInfo;

  SmallVector<LinkModule, 4> LinkModules;

  // Translates LLVM backend diagnostics into clang diagnostics; shared with
  // CIR so that both LLVM-emitting pipelines report backend diagnostics
  // through the same mechanism.
  BackendDiagnosticConsumer DiagConsumer;

  /// Run \p Fn against every entry in AuxGens, each under its own target
  /// scope. A no-op if AuxGens is empty (no aux targets configured), so
  /// ordinary single-target builds pay one predicted-not-taken loop check and
  /// nothing else.
  void dispatchToAuxGens(llvm::function_ref<void(CodeGenerator &)> Fn);

  /// Creates a CodeGenModule for each aux target, from its own invocation.
  void initializeAuxGens(ASTContext &Ctx);

  /// Runs each aux target's backend and packages their bitcode.
  void finishAuxGens();

  /// Whether the host generates code only after the aux targets' package
  /// exists, which it embeds.
  bool reorderForPackaging() const;

  /// Writes each CodeGenModule's IR for comparison against separate
  /// single-target compiles, if asked to.
  void dumpModulesForDiff();

public:
  BackendConsumer(CompilerInstance &CI, BackendAction Action,
                  IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS,
                  llvm::LLVMContext &C, SmallVector<LinkModule, 4> LinkModules,
                  StringRef InFile, std::unique_ptr<raw_pwrite_stream> OS,
                  CoverageSourceInfo *CoverageInfo,
                  llvm::Module *CurLinkModule = nullptr);

  llvm::Module *getModule() const;
  std::unique_ptr<llvm::Module> takeModule();

  CodeGenerator *getCodeGenerator();

  void HandleCXXStaticMemberVarInstantiation(VarDecl *VD) override;
  void Initialize(ASTContext &Ctx) override;
  bool HandleTopLevelDecl(DeclGroupRef D) override;
  void HandleInlineFunctionDefinition(FunctionDecl *D) override;
  void HandleInterestingDecl(DeclGroupRef D) override;
  void HandleTranslationUnit(ASTContext &C) override;
  void HandleTagDeclDefinition(TagDecl *D) override;
  void HandleTagDeclRequiredDefinition(const TagDecl *D) override;
  void CompleteTentativeDefinition(VarDecl *D) override;
  void CompleteExternalDeclaration(DeclaratorDecl *D) override;
  void AssignInheritanceModel(CXXRecordDecl *RD) override;
  void HandleVTable(CXXRecordDecl *RD) override;

  // Links this consumer's own LinkModules (set at construction from
  // -mlink-builtin-bitcode/-mlink-bitcode-file) into \p M. Returns true on
  // error.
  bool LinkInModules(llvm::Module *M);
  /// Create an llvm::DiagnosticHandler that routes LLVM backend diagnostics
  /// through this consumer's clang diagnostics.
  std::unique_ptr<llvm::DiagnosticHandler> createDiagnosticHandler();

  // Links each entry in \p ModulesToLink into \p M, clearing it. Returns
  // true on error. Defaults to this consumer's own (host-bound) CodeGenOpts
  // and TargetOpts for PropagateAttrs merging; aux entries must pass their
  // own explicitly (see the 4-arg overload) so a linked-in bitcode library's
  // functions (e.g. ROCm's ockl.bc) get that entry's own target-cpu/
  // target-features and function defaults stamped on them, not the host's.
  bool LinkInModules(llvm::Module *M,
                     SmallVectorImpl<LinkModule> &ModulesToLink);
  bool LinkInModules(llvm::Module *M,
                     SmallVectorImpl<LinkModule> &ModulesToLink,
                     const CodeGenOptions &MergeCodeGenOpts,
                     const TargetOptions &MergeTargetOpts);

  // Run exactly one AuxGenEntry's own backend tail (link its own
  // bitcode-file set, embed-bitcode no-op, emit its own bitcode output).
  // A no-op if this entry has no configured output.
  void runAuxBackendTail(AuxGenEntry &Entry);
};

} // namespace clang
#endif
