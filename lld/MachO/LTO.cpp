//===- LTO.cpp ------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LTO.h"
#include "Config.h"
#include "Driver.h"
#include "InputFiles.h"
#include "Symbols.h"
#include "Target.h"

#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/Filesystem.h"
#include "lld/Common/Strings.h"
#include "lld/Common/TargetOptionsCommandFlags.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/LTO/Config.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Support/Caching.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

using namespace lld;
using namespace lld::macho;
using namespace llvm;
using namespace llvm::MachO;
using namespace llvm::sys;

static std::string getThinLTOOutputFile(Ctx &ctx, StringRef modulePath) {
  return lto::getThinLTOOutputFile(modulePath, ctx.arg.thinLTOPrefixReplaceOld,
                                   ctx.arg.thinLTOPrefixReplaceNew);
}

static lto::Config createConfig(Ctx &ctx) {
  lto::Config c;
  c.Options = initTargetOptionsFromCodeGenFlags();
  c.Options.EmitAddrsig = ctx.arg.icfLevel == ICFLevel::safe ||
                          ctx.arg.icfLevel == ICFLevel::safe_thunks;
  for (StringRef C : ctx.arg.mllvmOpts)
    c.MllvmArgs.emplace_back(C.str());
  for (StringRef pluginFn : ctx.arg.passPlugins)
    c.PassPluginFilenames.push_back(std::string(pluginFn));
  c.OptPipeline = std::string(ctx.arg.ltoNewPmPasses);
  c.CodeModel = getCodeModelFromCMModel();
  c.CPU = getCPUStr();
  c.MAttrs = getMAttrs();
  c.DiagHandler = [&ctx](const DiagnosticInfo &di) {
    handleDiagnostic(ctx.e, di);
  };

  c.AlwaysEmitRegularLTOObj = !ctx.arg.ltoObjPath.empty();

  c.TimeTraceEnabled = ctx.arg.timeTraceEnabled;
  c.TimeTraceGranularity = ctx.arg.timeTraceGranularity;
  c.DebugPassManager = ctx.arg.ltoDebugPassManager;
  c.CSIRProfile = std::string(ctx.arg.csProfilePath);
  c.RunCSIRInstr = ctx.arg.csProfileGenerate;
  c.PGOWarnMismatch = ctx.arg.pgoWarnMismatch;
  c.DisableVerify = ctx.arg.disableVerify;
  c.OptLevel = ctx.arg.ltoo;
  c.CGOptLevel = ctx.arg.ltoCgo;

  c.PTO.LoopVectorization = c.OptLevel > 1;
  c.PTO.SLPVectorization = c.OptLevel > 1;

  if (ctx.arg.saveTemps)
    checkError(ctx.e, c.addSaveTemps(ctx.arg.outputFile.str() + ".",
                                     /*UseInputModulePath=*/true));

  if (ctx.arg.emitLLVM) {
    llvm::StringRef outputFile = ctx.arg.outputFile;
    c.PreCodeGenModuleHook = [&ctx, outputFile](size_t task, const Module &m) {
      if (std::unique_ptr<raw_fd_ostream> os =
              openLTOOutputFile(ctx.e, outputFile))
        WriteBitcodeToFile(m, *os, false);
      return false;
    };
  }

  return c;
}

// If `originalPath` exists, hardlinks `path` to `originalPath`. If that fails,
// or `originalPath` is not set, saves `buffer` to `path`.
static void saveOrHardlinkBuffer(Ctx &ctx, StringRef buffer, const Twine &path,
                                 std::optional<StringRef> originalPath) {
  if (originalPath) {
    // Delete the hardlink if it exists. Otherwise, it is possible for the
    // create_hard_link to fail (as the hardlink exists already), and when
    // saveBuffer is subsequently called the hardlink'd file may get truncated
    // and reading from it causes a crash.
    fs::remove(path);
    auto err = fs::create_hard_link(*originalPath, path);
    if (!err)
      return;
  }
  saveBuffer(ctx.e, buffer, path);
}

BitcodeCompiler::BitcodeCompiler(Ctx &ctx) : ctx(ctx) {
  // Initialize indexFile.
  if (!ctx.arg.thinLTOIndexOnlyArg.empty())
    indexFile = openFile(ctx.e, ctx.arg.thinLTOIndexOnlyArg);

  // Initialize ltoObj.
  lto::ThinBackend backend;
  auto onIndexWrite = [&](StringRef S) { thinIndices.erase(S); };
  if (ctx.arg.thinLTOIndexOnly) {
    backend = lto::createWriteIndexesThinBackend(
        llvm::hardware_concurrency(ctx.arg.thinLTOJobs),
        std::string(ctx.arg.thinLTOPrefixReplaceOld),
        std::string(ctx.arg.thinLTOPrefixReplaceNew),
        std::string(ctx.arg.thinLTOPrefixReplaceNativeObject),
        ctx.arg.thinLTOEmitImportsFiles, indexFile.get(), onIndexWrite);
  } else {
    backend = lto::createInProcessThinBackend(
        llvm::heavyweight_hardware_concurrency(ctx.arg.thinLTOJobs),
        onIndexWrite, ctx.arg.thinLTOEmitIndexFiles,
        ctx.arg.thinLTOEmitImportsFiles);
  }

  ltoObj = std::make_unique<lto::LTO>(createConfig(ctx), backend);
}

void BitcodeCompiler::add(BitcodeFile &f) {
  lto::InputFile &obj = *f.obj;

  if (ctx.arg.thinLTOEmitIndexFiles)
    thinIndices.insert(obj.getName());

  ArrayRef<lto::InputFile::Symbol> objSyms = obj.symbols();
  std::vector<lto::SymbolResolution> resols;
  resols.reserve(objSyms.size());

  // Provide a resolution to the LTO API for each symbol.
  bool exportDynamic =
      ctx.arg.outputType != MH_EXECUTE || ctx.arg.exportDynamic;
  auto symIt = f.symbols.begin();
  for (const lto::InputFile::Symbol &objSym : objSyms) {
    resols.emplace_back();
    lto::SymbolResolution &r = resols.back();
    Symbol *sym = *symIt++;

    // Ideally we shouldn't check for SF_Undefined but currently IRObjectFile
    // reports two symbols for module ASM defined. Without this check, lld
    // flags an undefined in IR with a definition in ASM as prevailing.
    // Once IRObjectFile is fixed to report only one symbol this hack can
    // be removed.
    r.Prevailing = !objSym.isUndefined() && sym->getFile() == &f;

    if (const auto *defined = dyn_cast<Defined>(sym)) {
      r.ExportDynamic =
          defined->isExternal() && !defined->privateExtern && exportDynamic;
      r.FinalDefinitionInLinkageUnit =
          !defined->isExternalWeakDef() && !defined->interposable;
    } else if (const auto *common = dyn_cast<CommonSymbol>(sym)) {
      r.ExportDynamic = !common->privateExtern && exportDynamic;
      r.FinalDefinitionInLinkageUnit = true;
    }

    r.VisibleToRegularObj =
        sym->isUsedInRegularObj || (r.Prevailing && r.ExportDynamic);

    // Un-define the symbol so that we don't get duplicate symbol errors when we
    // load the ObjFile emitted by LTO compilation.
    if (r.Prevailing)
      replaceSymbol<Undefined>(sym, ctx, sym->getName(), sym->getFile(),
                               RefState::Strong, /*wasBitcodeSymbol=*/true);

    // TODO: set the other resolution configs properly
  }
  checkError(ctx.e, ltoObj->add(std::move(f.obj), resols));
  hasFiles = true;
}

// If LazyObjFile has not been added to link, emit empty index files.
// This is needed because this is what GNU gold plugin does and we have a
// distributed build system that depends on that behavior.
static void thinLTOCreateEmptyIndexFiles(Ctx &ctx) {
  DenseSet<StringRef> linkedBitCodeFiles;
  for (InputFile *file : ctx.inputFiles)
    if (auto *f = dyn_cast<BitcodeFile>(file))
      if (!f->lazy)
        linkedBitCodeFiles.insert(f->getName());

  for (InputFile *file : ctx.inputFiles) {
    if (auto *f = dyn_cast<BitcodeFile>(file)) {
      if (!f->lazy)
        continue;
      if (linkedBitCodeFiles.contains(f->getName()))
        continue;
      std::string path = replaceThinLTOSuffix(
          ctx, getThinLTOOutputFile(ctx, f->obj->getName()));
      std::unique_ptr<raw_fd_ostream> os =
          openFile(ctx.e, path + ".thinlto.bc");
      if (!os)
        continue;

      ModuleSummaryIndex m(/*HaveGVs=*/false);
      m.setSkipModuleByDistributedBackend();
      writeIndexToFile(m, *os);
      if (ctx.arg.thinLTOEmitImportsFiles)
        openFile(ctx.e, path + ".imports");
    }
  }
}

// Merge all the bitcode files we have seen, codegen the result
// and return the resulting ObjectFile(s).
std::vector<ObjFile *> BitcodeCompiler::compile() {
  unsigned maxTasks = ltoObj->getMaxTasks();
  buf.resize(maxTasks);
  files.resize(maxTasks);

  // The -cache_path_lto option specifies the path to a directory in which
  // to cache native object files for ThinLTO incremental builds. If a path was
  // specified, configure LTO to use it as the cache directory.
  FileCache cache;
  if (!ctx.arg.thinLTOCacheDir.empty())
    cache = check(ctx.e, localCache("ThinLTO", "Thin", ctx.arg.thinLTOCacheDir,
                                    [&](size_t task, const Twine &moduleName,
                                        std::unique_ptr<MemoryBuffer> mb) {
                                      files[task] = std::move(mb);
                                    }));

  if (hasFiles)
    checkError(ctx.e,
               ltoObj->run(
                   [&](size_t task, const Twine &moduleName) {
                     return std::make_unique<CachedFileStream>(
                         std::make_unique<raw_svector_ostream>(buf[task]));
                   },
                   cache));

  // Emit empty index files for non-indexed files
  for (StringRef s : thinIndices) {
    std::string path = getThinLTOOutputFile(ctx, s);
    openFile(ctx.e, path + ".thinlto.bc");
    if (ctx.arg.thinLTOEmitImportsFiles)
      openFile(ctx.e, path + ".imports");
  }

  if (ctx.arg.thinLTOEmitIndexFiles)
    thinLTOCreateEmptyIndexFiles(ctx);

  // In ThinLTO mode, Clang passes a temporary directory in -object_path_lto,
  // while the argument is a single file in FullLTO mode.
  bool objPathIsDir = true;
  if (!ctx.arg.ltoObjPath.empty()) {
    if (std::error_code ec = fs::create_directories(ctx.arg.ltoObjPath))
      ctx.e.fatal("cannot create LTO object path " + ctx.arg.ltoObjPath + ": " +
                  ec.message());

    if (!fs::is_directory(ctx.arg.ltoObjPath)) {
      objPathIsDir = false;
      unsigned objCount =
          count_if(buf, [](const SmallString<0> &b) { return !b.empty(); });
      if (objCount > 1)
        ctx.e.fatal(
            "-object_path_lto must specify a directory when using ThinLTO");
    }
  }

  auto outputFilePath = [this, objPathIsDir](int i) {
    SmallString<261> filePath("/tmp/lto.tmp");
    if (!ctx.arg.ltoObjPath.empty()) {
      filePath = ctx.arg.ltoObjPath;
      if (objPathIsDir)
        path::append(filePath, Twine(i) + "." +
                                   getArchitectureName(ctx.arg.arch()) +
                                   ".lto.o");
    }
    return filePath;
  };

  // ThinLTO with index only option is required to generate only the index
  // files. After that, we exit from linker and ThinLTO backend runs in a
  // distributed environment.
  if (ctx.arg.thinLTOIndexOnly) {
    if (!ctx.arg.ltoObjPath.empty())
      saveBuffer(ctx.e, buf[0], outputFilePath(0));
    if (indexFile)
      indexFile->close();
    return {};
  }

  if (!ctx.arg.thinLTOCacheDir.empty())
    check(ctx.e, pruneCache(ctx.arg.thinLTOCacheDir, ctx.arg.thinLTOCachePolicy,
                            files));

  std::vector<ObjFile *> ret;
  for (unsigned i = 0; i < maxTasks; ++i) {
    // Get the native object contents either from the cache or from memory.  Do
    // not use the cached MemoryBuffer directly to ensure dsymutil does not
    // race with the cache pruner.
    StringRef objBuf;
    std::optional<StringRef> cachePath;
    if (files[i]) {
      objBuf = files[i]->getBuffer();
      cachePath = files[i]->getBufferIdentifier();
    } else {
      objBuf = buf[i];
    }
    if (objBuf.empty())
      continue;

    // FIXME: should `saveTemps` and `ltoObjPath` use the same file name?
    if (ctx.arg.saveTemps)
      saveBuffer(ctx.e, objBuf,
                 ctx.arg.outputFile + ((i == 0) ? "" : Twine(i)) + ".lto.o");

    auto filePath = outputFilePath(i);
    uint32_t modTime = 0;
    if (!ctx.arg.ltoObjPath.empty()) {
      saveOrHardlinkBuffer(ctx, objBuf, filePath, cachePath);
      modTime = getModTime(ctx, filePath);
    }
    ret.push_back(ctx.make<ObjFile>(
        ctx, MemoryBufferRef(objBuf, ctx.saver.save(filePath.str())), modTime,
        /*archiveName=*/"", /*lazy=*/false,
        /*forceHidden=*/false, /*compatArch=*/true, /*builtFromBitcode=*/true));
  }

  return ret;
}
