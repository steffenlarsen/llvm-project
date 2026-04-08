//===--- CIRGenAction.cpp - LLVM Code generation Frontend Action ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/FrontendAction/CIRGenAction.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/DLTI/DLTI.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/GPU/IR/GPUDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinDialect.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/Bytecode/BytecodeWriter.h"
#include "mlir/Parser/Parser.h"
#include "clang/Basic/DiagnosticFrontend.h"
#include "clang/CIR/CIRGenerator.h"
#include "clang/CIR/CIRToCIRPasses.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/LowerToLLVM.h"
#include "clang/CodeGen/BackendUtil.h"
#include "clang/CodeGen/ModuleLinker.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendOptions.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/DiagnosticHandler.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/Internalize.h"

using namespace cir;
using namespace clang;

namespace cir {

static BackendAction
getBackendActionFromOutputType(CIRGenAction::OutputType Action) {
  switch (Action) {
  case CIRGenAction::OutputType::EmitCIR:
    assert(false &&
           "Unsupported output type for getBackendActionFromOutputType!");
    break; // Unreachable, but fall through to report that
  case CIRGenAction::OutputType::EmitAssembly:
    return BackendAction::Backend_EmitAssembly;
  case CIRGenAction::OutputType::EmitBC:
    return BackendAction::Backend_EmitBC;
  case CIRGenAction::OutputType::EmitLLVM:
    return BackendAction::Backend_EmitLL;
  case CIRGenAction::OutputType::EmitObj:
    return BackendAction::Backend_EmitObj;
  }
  // We should only get here if a non-enum value is passed in or we went through
  // the assert(false) case above
  llvm_unreachable("Unsupported output type!");
}

static std::unique_ptr<llvm::Module> lowerFromCIRToLLVMIR(
    mlir::ModuleOp MLIRModule, llvm::LLVMContext &LLVMCtx,
    llvm::StringRef mlirSaveTempsOutFile = {},
    llvm::vfs::FileSystem *fs = nullptr, bool enableOffloadSplit = false,
    llvm::ArrayRef<std::string> offloadArchs = {},
    bool isDeviceCompilation = false, unsigned deviceOptLevel = 2,
    const cir::CIROffloadConfig &offloadConfig = {}) {
  return direct::lowerDirectlyFromCIRToLLVMIR(
      MLIRModule, LLVMCtx, mlirSaveTempsOutFile, fs, enableOffloadSplit,
      offloadArchs, isDeviceCompilation, deviceOptLevel, offloadConfig);
}

class CIRGenConsumer : public clang::ASTConsumer {

  virtual void anchor();

  CIRGenAction::OutputType Action;

  CompilerInstance &CI;

  std::unique_ptr<raw_pwrite_stream> OutputStream;

  ASTContext *Context{nullptr};
  IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS;
  std::unique_ptr<CIRGenerator> Gen;
  const FrontendOptions &FEOptions;
  CodeGenOptions &CGO;

  llvm::LLVMContext &LLVMCtx;
  SmallVectorImpl<::clang::LinkModule> &LinkModules;

public:
  CIRGenConsumer(CIRGenAction::OutputType Action, CompilerInstance &CI,
                 CodeGenOptions &CGO, std::unique_ptr<raw_pwrite_stream> OS,
                 llvm::LLVMContext &LLVMCtx,
                 SmallVectorImpl<::clang::LinkModule> &LinkModules)
      : Action(Action), CI(CI), OutputStream(std::move(OS)),
        FS(&CI.getVirtualFileSystem()),
        Gen(std::make_unique<CIRGenerator>(CI.getDiagnostics(), std::move(FS),
                                           CI.getCodeGenOpts())),
        FEOptions(CI.getFrontendOpts()), CGO(CGO), LLVMCtx(LLVMCtx),
        LinkModules(LinkModules) {}

  void Initialize(ASTContext &Ctx) override {
    assert(!Context && "initialized multiple times");
    Context = &Ctx;
    Gen->Initialize(Ctx);
  }

  bool HandleTopLevelDecl(DeclGroupRef D) override {
    Gen->HandleTopLevelDecl(D);
    return true;
  }

  void HandleCXXStaticMemberVarInstantiation(clang::VarDecl *VD) override {
    Gen->HandleCXXStaticMemberVarInstantiation(VD);
  }

  void HandleOpenACCRoutineReference(const FunctionDecl *FD,
                                     const OpenACCRoutineDecl *RD) override {
    Gen->HandleOpenACCRoutineReference(FD, RD);
  }

  void HandleInlineFunctionDefinition(FunctionDecl *D) override {
    Gen->HandleInlineFunctionDefinition(D);
  }

  void HandleTranslationUnit(ASTContext &C) override {
    Gen->HandleTranslationUnit(C);

    // In two-pass offload host-emit-CIR mode the module is intentionally
    // partial: gpu.launch_func ops reference kernel symbols that live in the
    // device cc1's CIR and are merged in by MergeOffloadModules.  Skip the
    // pre-pass verifier here; it runs again after the merge step.
    bool skipVerifier =
        FEOptions.ClangIRDisableCIRVerifier || CGO.ClangIROffloadHostEmitCIR;
    if (!skipVerifier) {
      if (!Gen->verifyModule()) {
        CI.getDiagnostics().Report(
            diag::err_cir_verification_failed_pre_passes);
        llvm::report_fatal_error(
            "CIR codegen: module verification error before running CIR passes");
        return;
      }
    }

    mlir::ModuleOp MlirModule = Gen->getModule();
    mlir::MLIRContext &MlirCtx = Gen->getMLIRContext();

    if (!FEOptions.ClangIRDisablePasses) {
      // Setup and run CIR pipeline.
      // In two-pass host-emit-CIR mode the module is partial (no device
      // functions yet), so suppress per-pass verification to avoid false
      // failures from gpu.launch_func referencing not-yet-merged symbols.
      bool enableVerifier = !FEOptions.ClangIRDisableCIRVerifier &&
                            !CGO.ClangIROffloadHostEmitCIR;
      if (runCIRToCIRPasses(MlirModule, MlirCtx, C, enableVerifier,
                            FEOptions.ClangIREnableIdiomRecognizer,
                            CGO.OptimizationLevel > 0)
              .failed()) {
        CI.getDiagnostics().Report(diag::err_cir_to_cir_transform_failed);
        return;
      }

      // CXXABILowering may leave gpu.return ops outside gpu.func when it
      // rewrites ops inside gpu.func bodies. Fix them up here.
      if (CGO.ClangIROffload) {
        llvm::SmallVector<mlir::gpu::ReturnOp> strayReturns;
        MlirModule->walk([&](mlir::gpu::ReturnOp ret) {
          if (!ret->getParentOfType<mlir::gpu::GPUFuncOp>())
            strayReturns.push_back(ret);
        });
        for (auto ret : strayReturns) {
          mlir::OpBuilder b(ret);
          cir::ReturnOp::create(b, ret.getLoc(), ret.getOperands());
          ret.erase();
        }
      }
    }

    // Collect GPU arch(s) and attach offload.target to the module so it
    // survives serialization to a .cir file.  The -x cir path reads it back
    // when the arch isn't available from cc1 flags.
    llvm::SmallVector<std::string> offloadArchs;
    if (CGO.ClangIROffload) {
      llvm::StringRef primaryCPU = C.getTargetInfo().getTargetOpts().CPU;
      if (!primaryCPU.empty() && !primaryCPU.starts_with("x86") &&
          !primaryCPU.starts_with("generic") && primaryCPU != "x86-64") {
        offloadArchs.push_back(primaryCPU.str()); // device cc1
      } else if (!CGO.ClangIROffloadArch.empty()) {
        offloadArchs.push_back(CGO.ClangIROffloadArch); // host cc1
      }
      if (!offloadArchs.empty()) {
        mlir::Builder b(MlirModule.getContext());
        llvm::SmallVector<mlir::Attribute> archAttrs;
        for (const std::string &arch : offloadArchs)
          archAttrs.push_back(b.getStringAttr(arch));
        // Store arch list as a plain ArrayAttr of StringAttr under
        // "offload.target".
        MlirModule->setAttr("offload.target", b.getArrayAttr(archAttrs));
      }
    }

    switch (Action) {
    case CIRGenAction::OutputType::EmitCIR:
      if (OutputStream && MlirModule) {
        mlir::OpPrintingFlags Flags;
        Flags.enableDebugInfo(/*enable=*/true, /*prettyForm=*/false);
        MlirModule->print(*OutputStream, Flags);
      }
      break;
    case CIRGenAction::OutputType::EmitLLVM:
    case CIRGenAction::OutputType::EmitBC:
    case CIRGenAction::OutputType::EmitObj:
    case CIRGenAction::OutputType::EmitAssembly: {
      StringRef saveTempsPrefix = CGO.SaveTempsFilePrefix;
      std::string cirSaveTempsOutFile, mlirSaveTempsOutFile;
      if (!saveTempsPrefix.empty()) {
        SmallString<128> stem(saveTempsPrefix);
        llvm::sys::path::replace_extension(stem, "cir");
        cirSaveTempsOutFile = std::string(stem);
        llvm::sys::path::replace_extension(stem, "mlir");
        mlirSaveTempsOutFile = std::string(stem);
      }

      if (!cirSaveTempsOutFile.empty()) {
        std::error_code ec;
        llvm::raw_fd_ostream out(cirSaveTempsOutFile, ec);
        if (!ec)
          MlirModule->print(out);
      }

      bool isDeviceCompilation = C.getLangOpts().CUDAIsDevice;
      cir::CIROffloadConfig offloadConfig;
      offloadConfig.tightenLaunchBounds = !CGO.ClangIRNoTightenLaunchBounds;
      offloadConfig.propagateBlockShape = !CGO.ClangIRNoPropagateBlockShape;
      offloadConfig.propagatePointerFacts =
          !CGO.ClangIRNoPropagatePointerFacts;
      offloadConfig.specializeScalarArgs =
          !CGO.ClangIRNoSpecializeScalarArgs;
      offloadConfig.multiversionDivisibility =
          !CGO.ClangIRNoMultiversionDivisibility;
      if (CGO.ClangIRNoDeadKernelElim)
        offloadConfig.deadKernelAction = "none";
      std::unique_ptr<llvm::Module> LLVMModule = lowerFromCIRToLLVMIR(
          MlirModule, LLVMCtx, mlirSaveTempsOutFile, &CI.getVirtualFileSystem(),
          CGO.ClangIROffload, offloadArchs, isDeviceCompilation,
          CGO.OptimizationLevel, offloadConfig);

      if (linkInModules(*LLVMModule))
        return;

      BackendAction BEAction = getBackendActionFromOutputType(Action);
      emitBackendOutput(
          CI, CI.getCodeGenOpts(), C.getTargetInfo().getDataLayoutString(),
          LLVMModule.get(), BEAction, FS, std::move(OutputStream));
      break;
    }
    }
  }

  // TODO: share with BackendConsumer::LinkInModules once OG's CurLinkModule
  // diagnostic-handler indirection is abstracted behind a callback for CIR.
  bool linkInModules(llvm::Module &M) {
    for (auto &LM : LinkModules) {
      assert(LM.Module && "LinkModule does not actually have a module");

      if (LM.PropagateAttrs)
        for (llvm::Function &F : *LM.Module) {
          if (F.isIntrinsic())
            continue;
          clang::CodeGen::mergeDefaultFunctionDefinitionAttributes(
              F, CGO, CI.getLangOpts(), CI.getTargetOpts(), LM.Internalize);
        }

      bool Err;
      if (LM.Internalize) {
        Err = llvm::Linker::linkModules(
            M, std::move(LM.Module), LM.LinkFlags,
            [](llvm::Module &M, const llvm::StringSet<> &GVS) {
              llvm::internalizeModule(M, [&GVS](const llvm::GlobalValue &GV) {
                return !GV.hasName() || (GVS.count(GV.getName()) == 0);
              });
            });
      } else {
        Err = llvm::Linker::linkModules(M, std::move(LM.Module), LM.LinkFlags);
      }

      if (Err)
        return true;
    }

    LinkModules.clear();
    return false;
  }

  void HandleTagDeclDefinition(TagDecl *D) override {
    PrettyStackTraceDecl CrashInfo(D, SourceLocation(),
                                   Context->getSourceManager(),
                                   "CIR generation of declaration");
    Gen->HandleTagDeclDefinition(D);
  }

  void HandleTagDeclRequiredDefinition(const TagDecl *D) override {
    Gen->HandleTagDeclRequiredDefinition(D);
  }

  void CompleteTentativeDefinition(VarDecl *D) override {
    Gen->CompleteTentativeDefinition(D);
  }

  void HandleVTable(CXXRecordDecl *RD) override { Gen->HandleVTable(RD); }
};
} // namespace cir

void CIRGenConsumer::anchor() {}

CIRGenAction::CIRGenAction(OutputType Act, mlir::MLIRContext *MLIRCtx)
    : MLIRCtx(MLIRCtx ? MLIRCtx : new mlir::MLIRContext),
      Ctx(std::make_unique<llvm::LLVMContext>()), Action(Act) {}

CIRGenAction::~CIRGenAction() { MLIRMod.release(); }

bool CIRGenAction::BeginSourceFileAction(CompilerInstance &CI) {
  if (clang::loadLinkModules(CI, *Ctx, LinkModules))
    return false;
  return ASTFrontendAction::BeginSourceFileAction(CI);
}

static std::unique_ptr<raw_pwrite_stream>
getOutputStream(CompilerInstance &CI, StringRef InFile,
                CIRGenAction::OutputType Action) {
  switch (Action) {
  case CIRGenAction::OutputType::EmitAssembly:
    return CI.createDefaultOutputFile(false, InFile, "s");
  case CIRGenAction::OutputType::EmitCIR:
    return CI.createDefaultOutputFile(false, InFile, "cir");
  case CIRGenAction::OutputType::EmitLLVM:
    return CI.createDefaultOutputFile(false, InFile, "ll");
  case CIRGenAction::OutputType::EmitBC:
    return CI.createDefaultOutputFile(true, InFile, "bc");
  case CIRGenAction::OutputType::EmitObj:
    return CI.createDefaultOutputFile(true, InFile, "o");
  }
  llvm_unreachable("Invalid CIRGenAction::OutputType");
}

std::unique_ptr<ASTConsumer>
CIRGenAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  // For pre-built CIR input (-x cir), we bypass AST parsing entirely in
  // ExecuteAction(). Return a no-op consumer so we don't construct CIRGenModule
  // (which would fail since there are no CUDA/C++ language opts).
  if (getCurrentFileKind().getLanguage() == clang::Language::CIR)
    return std::make_unique<ASTConsumer>();

  std::unique_ptr<llvm::raw_pwrite_stream> Out = CI.takeOutputStream();

  if (!Out)
    Out = getOutputStream(CI, InFile, Action);

  auto Result = std::make_unique<cir::CIRGenConsumer>(
      Action, CI, CI.getCodeGenOpts(), std::move(Out), *Ctx, LinkModules);

  return Result;
}

void CIRGenAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();
  // If the input is a pre-built CIR file (-x cir), bypass AST parsing and
  // directly load + lower the MLIR module to the requested output format.
  if (getCurrentFileKind().getLanguage() == clang::Language::CIR) {
    // Register the required dialects.  The merged CIR file produced by the
    // cir-opt merge step contains gpu.module / gpu.func / gpu.launch_func
    // (plus arith/func/memref ops), so all of these must be loaded before
    // parsing.
    MLIRCtx
        ->loadDialect<mlir::BuiltinDialect, cir::CIRDialect, mlir::DLTIDialect,
                      mlir::LLVM::LLVMDialect, mlir::gpu::GPUDialect,
                      mlir::arith::ArithDialect, mlir::func::FuncDialect,
                      mlir::memref::MemRefDialect>();

    // Parse the CIR file.  In the offload pipeline, the host CIR may have
    // verification issues (e.g., address space mismatches on get_global,
    // public visibility on declarations) that are tolerated in-memory but
    // would fail during post-parse verification.  Disable verification here;
    // the module will be verified after the merge + lowering passes.
    mlir::ParserConfig parserCfg(MLIRCtx, /*verifyAfterParse=*/false);
    mlir::OwningOpRef<mlir::ModuleOp> parsedMod =
        mlir::parseSourceFile<mlir::ModuleOp>(getCurrentFile(), parserCfg);
    if (!parsedMod) {
      CI.getDiagnostics().Report(diag::err_fe_error_reading)
          << getCurrentFile() << "failed to parse CIR/MLIR module";
      return;
    }

    mlir::ModuleOp MlirModule = *parsedMod;
    CodeGenOptions &CGO = const_cast<CodeGenOptions &>(CI.getCodeGenOpts());

    // Fix up verification issues in the parsed CIR module.  The host offload
    // CIR may have declaration-only cir.func with public visibility (MLIR
    // requires declarations to be private) and gpu.return ops outside gpu.func
    // (from CXXABILowering rewriting ops in gpu.func bodies).
    MlirModule->walk([](cir::FuncOp fn) {
      if (fn.isDeclaration() &&
          mlir::SymbolTable::getSymbolVisibility(fn) ==
              mlir::SymbolTable::Visibility::Public)
        mlir::SymbolTable::setSymbolVisibility(
            fn, mlir::SymbolTable::Visibility::Private);
    });

    // Fix gpu.return ops that ended up outside gpu.func (from CXXABILowering
    // rewriting ops inside gpu.func bodies). Replace with cir.return.
    llvm::SmallVector<mlir::gpu::ReturnOp> strayReturns;
    MlirModule->walk([&](mlir::gpu::ReturnOp ret) {
      if (!ret->getParentOfType<mlir::gpu::GPUFuncOp>())
        strayReturns.push_back(ret);
    });
    for (auto ret : strayReturns) {
      mlir::OpBuilder b(ret);
      cir::ReturnOp::create(b, ret.getLoc(), ret.getOperands());
      ret.erase();
    }

    // For EmitCIR, just print the module.
    if (Action == OutputType::EmitCIR) {
      std::unique_ptr<llvm::raw_pwrite_stream> Out =
          getOutputStream(CI, getCurrentFile(), Action);
      if (Out)
        MlirModule->print(*Out);
      return;
    }

    // Lower CIR → LLVM IR.
    llvm::LLVMContext LLVMCtx;
    llvm::SmallVector<std::string> offloadArchs;
    if (CGO.ClangIROffload) {
      if (!CGO.ClangIROffloadArch.empty())
        offloadArchs.push_back(CGO.ClangIROffloadArch);
      // Fall back to offload.target on the parsed module (set during codegen
      // or when reading a .cir file).
      if (offloadArchs.empty()) {
        if (auto archs =
                MlirModule->getAttrOfType<mlir::ArrayAttr>("offload.target")) {
          for (auto arch : archs)
            if (auto s = mlir::dyn_cast<mlir::StringAttr>(arch))
              offloadArchs.push_back(s.getValue().str());
        }
      }
    }
    cir::CIROffloadConfig offloadConfig;
    offloadConfig.tightenLaunchBounds = !CGO.ClangIRNoTightenLaunchBounds;
    offloadConfig.propagateBlockShape = !CGO.ClangIRNoPropagateBlockShape;
    offloadConfig.propagatePointerFacts = !CGO.ClangIRNoPropagatePointerFacts;
    offloadConfig.specializeScalarArgs = !CGO.ClangIRNoSpecializeScalarArgs;
    offloadConfig.multiversionDivisibility =
        !CGO.ClangIRNoMultiversionDivisibility;
    if (CGO.ClangIRNoDeadKernelElim)
      offloadConfig.deadKernelAction = "none";
    std::unique_ptr<llvm::Module> LLVMModule = lowerFromCIRToLLVMIR(
        MlirModule, LLVMCtx, /*mlirSaveTemps=*/"", /*fs=*/nullptr,
        CGO.ClangIROffload, offloadArchs, /*isDeviceCompilation=*/false,
        CGO.OptimizationLevel, offloadConfig);
    if (!LLVMModule)
      return;

    std::unique_ptr<llvm::raw_pwrite_stream> Out =
        getOutputStream(CI, getCurrentFile(), Action);
    BackendAction BEAction = getBackendActionFromOutputType(Action);
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS = &CI.getVirtualFileSystem();
    emitBackendOutput(CI, CGO, CI.getTarget().getDataLayoutString(),
                      LLVMModule.get(), BEAction, FS, std::move(Out));
    return;
  }

  // Normal AST-based path.
  ASTFrontendAction::ExecuteAction();
}

void EmitAssemblyAction::anchor() {}
EmitAssemblyAction::EmitAssemblyAction(mlir::MLIRContext *MLIRCtx)
    : CIRGenAction(OutputType::EmitAssembly, MLIRCtx) {}

void EmitCIRAction::anchor() {}
EmitCIRAction::EmitCIRAction(mlir::MLIRContext *MLIRCtx)
    : CIRGenAction(OutputType::EmitCIR, MLIRCtx) {}

void EmitLLVMAction::anchor() {}
EmitLLVMAction::EmitLLVMAction(mlir::MLIRContext *MLIRCtx)
    : CIRGenAction(OutputType::EmitLLVM, MLIRCtx) {}

void EmitBCAction::anchor() {}
EmitBCAction::EmitBCAction(mlir::MLIRContext *MLIRCtx)
    : CIRGenAction(OutputType::EmitBC, MLIRCtx) {}

void EmitObjAction::anchor() {}
EmitObjAction::EmitObjAction(mlir::MLIRContext *MLIRCtx)
    : CIRGenAction(OutputType::EmitObj, MLIRCtx) {}
