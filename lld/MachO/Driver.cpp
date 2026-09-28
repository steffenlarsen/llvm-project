//===- Driver.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Driver.h"
#include "Config.h"
#include "ICF.h"
#include "InputFiles.h"
#include "LTO.h"
#include "MarkLive.h"
#include "ObjC.h"
#include "OutputSection.h"
#include "OutputSegment.h"
#include "SectionPriorities.h"
#include "StripSwiftForceLoad.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "UnwindInfoSection.h"
#include "Writer.h"

#include "lld/Common/Args.h"
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/LLVM.h"
#include "lld/Common/Memory.h"
#include "lld/Common/Reproduce.h"
#include "lld/Common/Version.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/CGData/CodeGenDataWriter.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Object/Archive.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/TarWriter.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TextAPI/Architecture.h"
#include "llvm/TextAPI/PackedVersion.h"

#if !_WIN32
#include <sys/mman.h>
#endif

using namespace llvm;
using namespace llvm::MachO;
using namespace llvm::object;
using namespace llvm::opt;
using namespace llvm::sys;
using namespace lld;
using namespace lld::macho;

static HeaderFileType getOutputType(const InputArgList &args) {
  // TODO: -r, -dylinker, -preload...
  Arg *outputArg = args.getLastArg(OPT_bundle, OPT_dylib, OPT_execute);
  if (outputArg == nullptr)
    return MH_EXECUTE;

  switch (outputArg->getOption().getID()) {
  case OPT_bundle:
    return MH_BUNDLE;
  case OPT_dylib:
    return MH_DYLIB;
  case OPT_execute:
    return MH_EXECUTE;
  default:
    llvm_unreachable("internal error");
  }
}

static std::optional<StringRef> findLibrary(Ctx &ctx, StringRef name) {
  CachedHashStringRef key(name);
  auto entry = ctx.resolvedLibraries.find(key);
  if (entry != ctx.resolvedLibraries.end())
    return entry->second;

  auto doFind = [&] {
    // Special case for Csu support files required for Mac OS X 10.7 and older
    // (crt1.o)
    if (name.ends_with(".o"))
      return findPathCombination(ctx, name, ctx.arg.librarySearchPaths, {""});
    if (ctx.arg.searchDylibsFirst) {
      if (std::optional<StringRef> path =
              findPathCombination(ctx, "lib" + name, ctx.arg.librarySearchPaths,
                                  {".tbd", ".dylib", ".so"}))
        return path;
      return findPathCombination(ctx, "lib" + name, ctx.arg.librarySearchPaths,
                                 {".a"});
    }
    return findPathCombination(ctx, "lib" + name, ctx.arg.librarySearchPaths,
                               {".tbd", ".dylib", ".so", ".a"});
  };

  std::optional<StringRef> path = doFind();
  if (path)
    ctx.resolvedLibraries[key] = *path;

  return path;
}

static std::optional<StringRef> findFramework(Ctx &ctx, StringRef name) {
  CachedHashStringRef key(name);
  auto entry = ctx.resolvedFrameworks.find(key);
  if (entry != ctx.resolvedFrameworks.end())
    return entry->second;

  SmallString<260> symlink;
  StringRef suffix;
  std::tie(name, suffix) = name.split(",");
  for (StringRef dir : ctx.arg.frameworkSearchPaths) {
    symlink = dir;
    path::append(symlink, name + ".framework", name);

    if (!suffix.empty()) {
      // NOTE: we must resolve the symlink before trying the suffixes, because
      // there are no symlinks for the suffixed paths.
      SmallString<260> location;
      if (!fs::real_path(symlink, location)) {
        // only append suffix if realpath() succeeds
        Twine suffixed = location + suffix;
        if (fs::exists(suffixed))
          return ctx.resolvedFrameworks[key] = ctx.saver.save(suffixed.str());
      }
      // Suffix lookup failed, fall through to the no-suffix case.
    }

    if (std::optional<StringRef> path = resolveDylibPath(ctx, symlink.str()))
      return ctx.resolvedFrameworks[key] = *path;
  }
  return {};
}

static bool warnIfNotDirectory(Ctx &ctx, StringRef option, StringRef path) {
  if (!fs::exists(path)) {
    ctx.e.warn("directory not found for option -" + option + path);
    return false;
  } else if (!fs::is_directory(path)) {
    ctx.e.warn("option -" + option + path + " references a non-directory path");
    return false;
  }
  return true;
}

static std::vector<StringRef>
getSearchPaths(Ctx &ctx, unsigned optionCode, InputArgList &args,
               const std::vector<StringRef> &roots,
               const SmallVector<StringRef, 2> &systemPaths) {
  std::vector<StringRef> paths;
  StringRef optionLetter{optionCode == OPT_F ? "F" : "L"};
  for (StringRef path : args::getStrings(args, optionCode))
    for (StringRef searchPath : getRerootedSearchPaths(ctx, path, roots))
      if (searchPath != path ||
          warnIfNotDirectory(ctx, optionLetter, searchPath))
        paths.push_back(searchPath);

  // `-Z` suppresses the standard "system" search paths.
  if (args.hasArg(OPT_Z))
    return paths;

  for (const StringRef &path : systemPaths) {
    for (const StringRef &root : roots) {
      SmallString<261> buffer(root);
      path::append(buffer, path);
      if (fs::is_directory(buffer))
        paths.push_back(ctx.saver.save(buffer.str()));
    }
  }
  return paths;
}

static std::vector<StringRef> getSystemLibraryRoots(InputArgList &args) {
  std::vector<StringRef> roots;
  for (const Arg *arg : args.filtered(OPT_syslibroot))
    roots.push_back(arg->getValue());
  // NOTE: the final `-syslibroot` being `/` will ignore all roots
  if (!roots.empty() && roots.back() == "/")
    roots.clear();
  // NOTE: roots can never be empty - add an empty root to simplify the library
  // and framework search path computation.
  if (roots.empty())
    roots.emplace_back("");
  return roots;
}

static std::vector<StringRef>
getLibrarySearchPaths(Ctx &ctx, InputArgList &args,
                      const std::vector<StringRef> &roots) {
  return getSearchPaths(ctx, OPT_L, args, roots,
                        {"/usr/lib", "/usr/local/lib"});
}

static std::vector<StringRef>
getFrameworkSearchPaths(Ctx &ctx, InputArgList &args,
                        const std::vector<StringRef> &roots) {
  return getSearchPaths(ctx, OPT_F, args, roots,
                        {"/Library/Frameworks", "/System/Library/Frameworks"});
}

static llvm::CachePruningPolicy getLTOCachePolicy(Ctx &ctx,
                                                  InputArgList &args) {
  SmallString<128> ltoPolicy;
  auto add = [&ltoPolicy](Twine val) {
    if (!ltoPolicy.empty())
      ltoPolicy += ":";
    val.toVector(ltoPolicy);
  };
  for (const Arg *arg :
       args.filtered(OPT_thinlto_cache_policy_eq, OPT_prune_interval_lto,
                     OPT_prune_after_lto, OPT_max_relative_cache_size_lto)) {
    switch (arg->getOption().getID()) {
    case OPT_thinlto_cache_policy_eq:
      add(arg->getValue());
      break;
    case OPT_prune_interval_lto:
      if (!strcmp("-1", arg->getValue()))
        add("prune_interval=87600h"); // 10 years
      else
        add(Twine("prune_interval=") + arg->getValue() + "s");
      break;
    case OPT_prune_after_lto:
      add(Twine("prune_after=") + arg->getValue() + "s");
      break;
    case OPT_max_relative_cache_size_lto:
      add(Twine("cache_size=") + arg->getValue() + "%");
      break;
    }
  }
  return CHECK(parseCachePruningPolicy(ltoPolicy), "invalid LTO cache policy");
}

// What caused a given library to be loaded. Only relevant for archives.
// Note that this does not tell us *how* we should load the library, i.e.
// whether we should do it lazily or eagerly (AKA force loading). The "how" is
// decided within addFile().
enum class LoadType {
  CommandLine,      // Library was passed as a regular CLI argument
  CommandLineForce, // Library was passed via `-force_load`
  LCLinkerOption,   // Library was passed via LC_LINKER_OPTIONS
};

static void saveThinArchiveToRepro(Ctx &ctx, ArchiveFile const *file) {
  assert(ctx.tar && file->getArchive().isThin());

  Error e = Error::success();
  for (const object::Archive::Child &c : file->getArchive().children(e)) {
    MemoryBufferRef mb = CHECK(c.getMemoryBufferRef(),
                               toString(file) + ": failed to get buffer");
    ctx.tar->append(relativeToRoot(CHECK(c.getFullName(), file)),
                    mb.getBuffer());
  }
  if (e)
    ctx.e.error(toString(file) +
                ": Archive::children failed: " + toString(std::move(e)));
}

struct DeferredFile {
  StringRef path;
  bool isLazy;
  MemoryBufferRef buffer;
  LoadType loadType = LoadType::CommandLine;
  bool isNeeded = false;
  bool isWeak = false;
  bool isReexport = false;
  bool isHidden = false;
  bool isExplicit = true;
};
using DeferredFiles = std::vector<DeferredFile>;

#if LLVM_ENABLE_THREADS
class macho::SerialBackgroundWorkQueue {
  std::deque<std::function<void()>> queue;
  std::thread *running = nullptr;
  std::mutex mutex;

public:
  std::atomic_bool stopAllWork = false;

  // The worker thread reads the linker context, so it must be finished before
  // the context goes away.
  ~SerialBackgroundWorkQueue() {
    stopAllWork = true;
    if (running) {
      running->join();
      delete running;
    }
  }

  void queueWork(std::function<void()> work) {
    mutex.lock();
    if (running && queue.empty()) {
      mutex.unlock();
      running->join();
      mutex.lock();
      delete running;
      running = nullptr;
    }

    if (work) {
      queue.emplace_back(std::move(work));
      if (!running)
        running = new std::thread([&]() {
          while (!stopAllWork) {
            mutex.lock();
            if (queue.empty()) {
              mutex.unlock();
              break;
            }
            auto work = std::move(queue.front());
            mutex.unlock();
            work();
            mutex.lock();
            queue.pop_front();
            mutex.unlock();
          }
        });
    }
    mutex.unlock();
  }
};

// Most input files have been mapped but not yet paged in.
// This code forces the page-ins on multiple threads so
// the process is not stalled waiting on disk buffer i/o.
void multiThreadedPageInBackground(Ctx &ctx, DeferredFiles &deferred) {
  static const size_t pageSize = Process::getPageSizeEstimate();
  static const size_t largeArchive = 10 * 1024 * 1024;
#ifndef NDEBUG
  using namespace std::chrono;
  static std::atomic_uint64_t totalBytes = 0;
  std::atomic_int numDeferedFilesAdvised = 0;
  auto t0 = high_resolution_clock::now();
#endif

  auto preloadDeferredFile = [&](const DeferredFile &deferredFile) {
    const StringRef &buff = deferredFile.buffer.getBuffer();
    if (buff.size() > largeArchive)
      return;

#ifndef NDEBUG
    totalBytes += buff.size();
    numDeferedFilesAdvised += 1;
#endif
#if _WIN32
    // Reference all file's mmap'd pages to load them into memory.
    for (const char *page = buff.data(), *end = page + buff.size();
         page < end && !ctx.pageInQueue->stopAllWork; page += pageSize) {
      [[maybe_unused]] volatile char t = *page;
      (void)t;
    }
#else
#define DEBUG_TYPE "lld-madvise"
    auto aligned =
        llvm::alignDown(reinterpret_cast<uintptr_t>(buff.data()), pageSize);
    if (madvise((void *)aligned, buff.size(), MADV_WILLNEED) < 0)
      LLVM_DEBUG(llvm::dbgs() << "madvise error: " << strerror(errno) << "\n");
#undef DEBUG_TYPE
#endif
  };

  { // Create scope for waiting for the taskGroup
    std::atomic_size_t index = 0;
    llvm::parallel::TaskGroup taskGroup;
    for (int w = 0; w < ctx.arg.readWorkers; w++)
      taskGroup.spawn([&ctx, &index, &preloadDeferredFile, &deferred]() {
        while (!ctx.pageInQueue->stopAllWork) {
          size_t localIndex = index.fetch_add(1);
          if (localIndex >= deferred.size())
            break;
          preloadDeferredFile(deferred[localIndex]);
        }
      });
  }

#ifndef NDEBUG
  auto dt = high_resolution_clock::now() - t0;
  if (Process::GetEnv("LLD_MULTI_THREAD_PAGE"))
    llvm::dbgs() << "multiThreadedPageIn " << totalBytes << "/"
                 << numDeferedFilesAdvised << "/" << deferred.size() << "/"
                 << duration_cast<milliseconds>(dt).count() / 1000. << "\n";
#endif
}

static void multiThreadedPageIn(Ctx &ctx, const DeferredFiles &deferred) {
  ctx.pageInQueue->queueWork([=, &ctx]() {
    DeferredFiles files = deferred;
    multiThreadedPageInBackground(ctx, files);
  });
}
#endif

static InputFile *processFile(Ctx &ctx, std::optional<MemoryBufferRef> buffer,
                              DeferredFiles *archiveContents, StringRef path,
                              LoadType loadType, bool isLazy = false,
                              bool isExplicit = true,
                              bool isBundleLoader = false,
                              bool isForceHidden = false) {
  if (!buffer)
    return nullptr;
  MemoryBufferRef mbref = *buffer;
  InputFile *newFile = nullptr;

  file_magic magic = identify_magic(mbref.getBuffer());
  switch (magic) {
  case file_magic::archive: {
    bool isCommandLineLoad = loadType != LoadType::LCLinkerOption;
    // Avoid loading archives twice. If the archives are being force-loaded,
    // loading them twice would create duplicate symbol errors. In the
    // non-force-loading case, this is just a minor performance optimization.
    // We don't take a reference to cachedFile here because the
    // loadArchiveMember() call below may recursively call addFile() and
    // invalidate this reference.
    auto entry = ctx.loadedArchives.find(path);

    ArchiveFile *file;
    if (entry == ctx.loadedArchives.end()) {
      // No cached archive, we need to create a new one
      std::unique_ptr<object::Archive> archive = CHECK(
          object::Archive::create(mbref), path + ": failed to parse archive");

      file = ctx.make<ArchiveFile>(ctx, std::move(archive), isForceHidden);

      if (ctx.tar && file->getArchive().isThin())
        saveThinArchiveToRepro(ctx, file);
    } else {
      file = entry->second.file;
      // Command-line loads take precedence. If file is previously loaded via
      // command line, or is loaded via LC_LINKER_OPTION and being loaded via
      // LC_LINKER_OPTION again, using the cached archive is enough.
      if (entry->second.isCommandLineLoad || !isCommandLineLoad)
        return file;
    }

    bool isLCLinkerForceLoad = loadType == LoadType::LCLinkerOption &&
                               ctx.arg.forceLoadSwift &&
                               path::filename(path).starts_with("libswift");
    if ((isCommandLineLoad && ctx.arg.allLoad) ||
        loadType == LoadType::CommandLineForce || isLCLinkerForceLoad) {
      if (readFile(ctx, path)) {
        Error e = Error::success();
        for (const object::Archive::Child &c : file->getArchive().children(e)) {
          StringRef reason;
          switch (loadType) {
          case LoadType::LCLinkerOption:
            reason = "LC_LINKER_OPTION";
            break;
          case LoadType::CommandLineForce:
            reason = "-force_load";
            break;
          case LoadType::CommandLine:
            reason = "-all_load";
            break;
          }
          if (Error e = file->fetch(c, reason)) {
            if (ctx.arg.warnThinArchiveMissingMembers)
              ctx.e.warn(
                  toString(file) + ": " + reason +
                  " failed to load archive member: " + toString(std::move(e)));
            else
              llvm::consumeError(std::move(e));
          }
        }
        if (e)
          ctx.e.error(toString(file) +
                      ": Archive::children failed: " + toString(std::move(e)));
      }
    } else if (isCommandLineLoad && ctx.arg.forceLoadObjC) {
      if (file->getArchive().hasSymbolTable()) {
        for (const object::Archive::Symbol &sym : file->getArchive().symbols())
          if (sym.getName().starts_with(objc::symbol_names::klass))
            file->fetch(sym);
      }

      // TODO: no need to look for ObjC sections for a given archive member if
      // we already found that it contains an ObjC symbol.
      if (readFile(ctx, path)) {
        Error e = Error::success();
        for (const object::Archive::Child &c : file->getArchive().children(e)) {
          Expected<MemoryBufferRef> mb = c.getMemoryBufferRef();
          if (!mb) {
            // We used to create broken repro tarballs that only included those
            // object files from thin archives that ended up being used.
            if (ctx.arg.warnThinArchiveMissingMembers)
              ctx.e.warn(toString(file) +
                         ": -ObjC failed to open archive member: " +
                         toString(mb.takeError()));
            else
              llvm::consumeError(mb.takeError());
            continue;
          }

          if (ctx.arg.readWorkers && archiveContents)
            archiveContents->push_back({path, isLazy, *mb});
          if (!hasObjCSection(ctx, *mb))
            continue;
          if (Error e = file->fetch(c, "-ObjC"))
            ctx.e.error(toString(file) +
                        ": -ObjC failed to load archive member: " +
                        toString(std::move(e)));
        }
        if (e)
          ctx.e.error(toString(file) +
                      ": Archive::children failed: " + toString(std::move(e)));
      }
    }
    if (!archiveContents || archiveContents->empty())
      file->addLazySymbols();
    ctx.loadedArchives[path] = ArchiveFileInfo{file, isCommandLineLoad};
    newFile = file;
    break;
  }
  case file_magic::macho_object:
    newFile = ctx.make<ObjFile>(ctx, mbref, getModTime(ctx, path), "", isLazy);
    break;
  case file_magic::macho_dynamically_linked_shared_lib:
  case file_magic::macho_dynamically_linked_shared_lib_stub:
  case file_magic::tapi_file:
    if (DylibFile *dylibFile = loadDylib(ctx, mbref, nullptr,
                                         /*isBundleLoader=*/false, isExplicit))
      newFile = dylibFile;
    break;
  case file_magic::bitcode:
    newFile = ctx.make<BitcodeFile>(ctx, mbref, "", 0, isLazy);
    break;
  case file_magic::macho_executable:
  case file_magic::macho_bundle:
    // We only allow executable and bundle type here if it is used
    // as a bundle loader.
    if (!isBundleLoader)
      ctx.e.error(path + ": unhandled file type");
    if (DylibFile *dylibFile = loadDylib(ctx, mbref, nullptr, isBundleLoader))
      newFile = dylibFile;
    break;
  default:
    ctx.e.error(path + ": unhandled file type");
  }
  if (newFile && !isa<DylibFile>(newFile)) {
    if ((isa<ObjFile>(newFile) || isa<BitcodeFile>(newFile)) && newFile->lazy &&
        ctx.arg.forceLoadObjC) {
      for (Symbol *sym : newFile->symbols)
        if (sym && sym->getName().starts_with(objc::symbol_names::klass)) {
          extract(*newFile, "-ObjC");
          break;
        }
      if (newFile->lazy && hasObjCSection(ctx, mbref))
        extract(*newFile, "-ObjC");
    }

    // printArchiveMemberLoad(ctx) prints both .a and .o names, so no need to
    // print the .a name here. Similarly skip lazy files.
    if (ctx.arg.printEachFile && magic != file_magic::archive && !isLazy)
      ctx.e.message(toString(newFile), ctx.e.outs());
    ctx.inputFiles.insert(newFile);
  }
  return newFile;
}

static InputFile *addFile(Ctx &ctx, StringRef path, LoadType loadType,
                          bool isLazy = false, bool isExplicit = true,
                          bool isBundleLoader = false,
                          bool isForceHidden = false) {
  return processFile(ctx, readFile(ctx, path), nullptr, path, loadType, isLazy,
                     isExplicit, isBundleLoader, isForceHidden);
}

static void applyDylibMetadata(Ctx &ctx, InputFile *file, bool isNeeded,
                               bool isWeak, bool isReexport) {
  if (auto *dylibFile = dyn_cast_or_null<DylibFile>(file)) {
    dylibFile->forceNeeded |= isNeeded;
    dylibFile->forceWeakImport |= isWeak;
    if (isReexport) {
      ctx.arg.hasReexports = true;
      dylibFile->reexport = true;
    }
  }
}

static void checkAndCacheFramework(Ctx &ctx, InputFile *file, StringRef path) {
  if (isa_and_nonnull<ObjFile>(file) || isa_and_nonnull<BitcodeFile>(file)) {
    if (path.contains(".framework"))
      ctx.loadedObjectFrameworks.insert(path);
  }
}

static void deferFile(Ctx &ctx, StringRef path, bool isLazy,
                      DeferredFiles &deferred,
                      LoadType loadType = LoadType::CommandLine,
                      bool isNeeded = false, bool isWeak = false,
                      bool isReexport = false, bool isHidden = false,
                      bool isExplicit = true) {
  std::optional<MemoryBufferRef> buffer = readFile(ctx, path);
  if (!buffer)
    return;
  if (ctx.arg.readWorkers)
    deferred.push_back({path, isLazy, *buffer, loadType, isNeeded, isWeak,
                        isReexport, isHidden, isExplicit});
  else {
    if (ctx.loadedObjectFrameworks.contains(path))
      return;

    InputFile *file =
        processFile(ctx, buffer, nullptr, path, loadType, isLazy, isExplicit,
                    /*isBundleLoader=*/false, isHidden);
    applyDylibMetadata(ctx, file, isNeeded, isWeak, isReexport);
    checkAndCacheFramework(ctx, file, path);
  }
}

static void addLibrary(Ctx &ctx, StringRef name, bool isNeeded, bool isWeak,
                       bool isReexport, bool isHidden, bool isExplicit,
                       LoadType loadType, DeferredFiles &deferred) {
  if (std::optional<StringRef> path = findLibrary(ctx, name)) {
    deferFile(ctx, *path, /*isLazy=*/false, deferred, loadType, isNeeded,
              isWeak, isReexport, isHidden, isExplicit);
    return;
  }
  if (loadType == LoadType::LCLinkerOption) {
    ctx.missingAutolinkWarnings.push_back(
        ctx.saver.save("auto-linked library not found for -l" + name));
    return;
  }
  ctx.e.error("library not found for -l" + name);
}

static void addFramework(Ctx &ctx, StringRef name, bool isNeeded, bool isWeak,
                         bool isReexport, bool isExplicit, LoadType loadType,
                         DeferredFiles &deferred) {
  if (std::optional<StringRef> path = findFramework(ctx, name)) {
    if (ctx.loadedObjectFrameworks.contains(*path))
      return;

    deferFile(ctx, *path, /*isLazy=*/false, deferred, loadType, isNeeded,
              isWeak, isReexport, /*isHidden=*/false, isExplicit);
    return;
  }
  if (loadType == LoadType::LCLinkerOption) {
    ctx.missingAutolinkWarnings.push_back(ctx.saver.save(
        "auto-linked framework not found for -framework " + name));
    return;
  }
  ctx.e.error("framework not found for -framework " + name);
}

// Parses LC_LINKER_OPTION contents, which can add additional command line
// flags. This directly parses the flags instead of using the standard argument
// parser to improve performance.
void macho::parseLCLinkerOption(
    Ctx &ctx, llvm::SmallVectorImpl<StringRef> &LCLinkerOptions, InputFile *f,
    unsigned argc, StringRef data) {
  if (ctx.arg.ignoreAutoLink)
    return;

  SmallVector<StringRef, 4> argv;
  size_t offset = 0;
  for (unsigned i = 0; i < argc && offset < data.size(); ++i) {
    argv.push_back(data.data() + offset);
    offset += strlen(data.data() + offset) + 1;
  }
  if (argv.size() != argc || offset > data.size())
    ctx.e.fatal(toString(f) + ": invalid LC_LINKER_OPTION");

  unsigned i = 0;
  StringRef arg = argv[i];
  if (arg.consume_front("-l")) {
    if (ctx.arg.ignoreAutoLinkOptions.contains(arg))
      return;
  } else if (arg == "-framework") {
    StringRef name = argv[++i];
    if (ctx.arg.ignoreAutoLinkOptions.contains(name))
      return;
  } else {
    ctx.e.error(arg + " is not allowed in LC_LINKER_OPTION");
  }

  LCLinkerOptions.append(argv);
}

void macho::resolveLCLinkerOptions(Ctx &ctx) {
  while (!ctx.unprocessedLCLinkerOptions.empty()) {
    SmallVector<StringRef> LCLinkerOptions(ctx.unprocessedLCLinkerOptions);
    ctx.unprocessedLCLinkerOptions.clear();

    DeferredFiles deferred;
    SmallVector<StringRef> frameworks;
    SmallVector<StringRef> libraries;

    for (unsigned i = 0; i < LCLinkerOptions.size(); ++i) {
      StringRef arg = LCLinkerOptions[i];
      if (arg.consume_front("-l")) {
        assert(!ctx.arg.ignoreAutoLinkOptions.contains(arg));
        libraries.push_back(arg);
      } else if (arg == "-framework") {
        StringRef name = LCLinkerOptions[++i];
        assert(!ctx.arg.ignoreAutoLinkOptions.contains(name));
        frameworks.push_back(name);
      } else {
        ctx.e.error(arg + " is not allowed in LC_LINKER_OPTION");
      }
    }

    llvm::sort(frameworks);
    llvm::sort(libraries);

    frameworks.erase(std::unique(frameworks.begin(), frameworks.end()),
                     frameworks.end());
    libraries.erase(std::unique(libraries.begin(), libraries.end()),
                    libraries.end());

    for (const StringRef framework : frameworks) {
      addFramework(ctx, framework, /*isNeeded=*/false, /*isWeak=*/false,
                   /*isReexport=*/false, /*isExplicit=*/false,
                   LoadType::LCLinkerOption, deferred);
    }

    for (const StringRef library : libraries) {
      addLibrary(ctx, library, /*isNeeded=*/false, /*isWeak=*/false,
                 /*isReexport=*/false, /*isHidden=*/false,
                 /*isExplicit=*/false, LoadType::LCLinkerOption, deferred);
    }

    for (auto &file : deferred) {
      if (ctx.loadedObjectFrameworks.contains(file.path))
        continue;

      auto inputFile = processFile(ctx, file.buffer, nullptr, file.path,
                                   file.loadType, file.isLazy, file.isExplicit,
                                   /*isBundleLoader=*/false, file.isHidden);
      applyDylibMetadata(ctx, inputFile, file.isNeeded, file.isWeak,
                         file.isReexport);
      checkAndCacheFramework(ctx, inputFile, file.path);
    }
  }
}

static void addFileList(Ctx &ctx, StringRef path, bool isLazy,
                        DeferredFiles &deferredFiles) {
  std::optional<MemoryBufferRef> buffer = readFile(ctx, path);
  if (!buffer)
    return;
  MemoryBufferRef mbref = *buffer;
  for (StringRef path : args::getLines(mbref))
    deferFile(ctx, rerootPath(ctx, path), isLazy, deferredFiles);
}

// We expect sub-library names of the form "libfoo", which will match a dylib
// with a path of .*/libfoo.{dylib, tbd}.
// XXX ld64 seems to ignore the extension entirely when matching sub-libraries;
// I'm not sure what the use case for that is.
static bool markReexport(Ctx &ctx, StringRef searchName,
                         ArrayRef<StringRef> extensions) {
  for (InputFile *file : ctx.inputFiles) {
    if (auto *dylibFile = dyn_cast<DylibFile>(file)) {
      StringRef filename = path::filename(dylibFile->getName());
      if (filename.consume_front(searchName) &&
          (filename.empty() || llvm::is_contained(extensions, filename))) {
        dylibFile->reexport = true;
        return true;
      }
    }
  }
  return false;
}

// This function is called on startup. We need this for LTO since
// LTO calls LLVM functions to compile bitcode files to native code.
// Technically this can be delayed until we read bitcode files, but
// we don't bother to do lazily because the initialization is fast.
static void initLLVM() {
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmPrinters();
  InitializeAllAsmParsers();
}

static bool compileBitcodeFiles(Ctx &ctx) {
  TimeTraceScope timeScope("LTO");
  auto *lto = ctx.make<BitcodeCompiler>(ctx);
  for (InputFile *file : ctx.inputFiles)
    if (auto *bitcodeFile = dyn_cast<BitcodeFile>(file))
      if (!file->lazy)
        lto->add(*bitcodeFile);

  std::vector<ObjFile *> compiled = lto->compile();
  ctx.inputFiles.insert_range(compiled);

  return !compiled.empty();
}

// Replaces common symbols with defined symbols residing in __common sections.
// This function must be called after all symbol names are resolved (i.e. after
// all InputFiles have been loaded.) As a result, later operations won't see
// any CommonSymbols.
static void replaceCommonSymbols(Ctx &ctx) {
  TimeTraceScope timeScope("Replace common symbols");
  ConcatOutputSection *osec = nullptr;
  for (Symbol *sym : ctx.symtab->getSymbols()) {
    auto *common = dyn_cast<CommonSymbol>(sym);
    if (common == nullptr)
      continue;

    // Casting to size_t will truncate large values on 32-bit architectures,
    // but it's not really worth supporting the linking of 64-bit programs on
    // 32-bit archs.
    ArrayRef<uint8_t> data = {nullptr, static_cast<size_t>(common->size)};
    // FIXME avoid creating one Section per symbol?
    auto *section =
        ctx.make<Section>(ctx, common->getFile(), segment_names::data,
                          section_names::common, S_ZEROFILL, /*addr=*/0);
    auto *isec = ctx.make<ConcatInputSection>(*section, data, common->align);
    if (!osec)
      osec = ConcatOutputSection::getOrCreateForInput(ctx, isec);
    isec->parent = osec;
    addInputSection(ctx, isec);

    // FIXME: CommonSymbol should store isReferencedDynamically, noDeadStrip
    // and pass them on here.
    replaceSymbol<Defined>(
        sym, ctx, sym->getName(), common->getFile(), isec, /*value=*/0,
        common->size,
        /*isWeakDef=*/false, /*isExternal=*/true, common->privateExtern,
        /*includeInSymtab=*/true, /*isReferencedDynamically=*/false,
        /*noDeadStrip=*/false);
  }
}

static void initializeSectionRenameMap(Ctx &ctx) {
  if (ctx.arg.dataConst) {
    SmallVector<StringRef> v{section_names::got,
                             section_names::authGot,
                             section_names::authPtr,
                             section_names::nonLazySymbolPtr,
                             section_names::const_,
                             section_names::cfString,
                             section_names::moduleInitFunc,
                             section_names::moduleTermFunc,
                             section_names::objcClassList,
                             section_names::objcNonLazyClassList,
                             section_names::objcCatList,
                             section_names::objcNonLazyCatList,
                             section_names::objcProtoList,
                             section_names::objCImageInfo};
    for (StringRef s : v)
      ctx.arg.sectionRenameMap[{segment_names::data, s}] = {
          segment_names::dataConst, s};
  }
  ctx.arg.sectionRenameMap[{segment_names::text, section_names::staticInit}] = {
      segment_names::text, section_names::text};
  ctx.arg.sectionRenameMap[{segment_names::import, section_names::pointers}] = {
      ctx.arg.dataConst ? segment_names::dataConst : segment_names::data,
      section_names::nonLazySymbolPtr};
}

static inline char toLowerDash(char x) {
  if (x >= 'A' && x <= 'Z')
    return x - 'A' + 'a';
  else if (x == ' ')
    return '-';
  return x;
}

static std::string lowerDash(StringRef s) {
  return std::string(map_iterator(s.begin(), toLowerDash),
                     map_iterator(s.end(), toLowerDash));
}

struct PlatformVersion {
  PlatformType platform = PLATFORM_UNKNOWN;
  llvm::VersionTuple minimum;
  llvm::VersionTuple sdk;
};

static PlatformVersion parsePlatformVersion(Ctx &ctx, const Arg *arg) {
  assert(arg->getOption().getID() == OPT_platform_version);
  StringRef platformStr = arg->getValue(0);
  StringRef minVersionStr = arg->getValue(1);
  StringRef sdkVersionStr = arg->getValue(2);

  PlatformVersion platformVersion;

  // TODO(compnerd) see if we can generate this case list via XMACROS
  platformVersion.platform =
      StringSwitch<PlatformType>(lowerDash(platformStr))
          .Cases({"macos", "1"}, PLATFORM_MACOS)
          .Cases({"ios", "2"}, PLATFORM_IOS)
          .Cases({"tvos", "3"}, PLATFORM_TVOS)
          .Cases({"watchos", "4"}, PLATFORM_WATCHOS)
          .Cases({"bridgeos", "5"}, PLATFORM_BRIDGEOS)
          .Cases({"mac-catalyst", "6"}, PLATFORM_MACCATALYST)
          .Cases({"ios-simulator", "7"}, PLATFORM_IOSSIMULATOR)
          .Cases({"tvos-simulator", "8"}, PLATFORM_TVOSSIMULATOR)
          .Cases({"watchos-simulator", "9"}, PLATFORM_WATCHOSSIMULATOR)
          .Cases({"driverkit", "10"}, PLATFORM_DRIVERKIT)
          .Cases({"xros", "11"}, PLATFORM_XROS)
          .Cases({"xros-simulator", "12"}, PLATFORM_XROS_SIMULATOR)
          .Default(PLATFORM_UNKNOWN);
  if (platformVersion.platform == PLATFORM_UNKNOWN)
    ctx.e.error(Twine("malformed platform: ") + platformStr);
  // The underlying load command only supports 3 components.
  if (platformVersion.minimum.tryParse(minVersionStr) ||
      platformVersion.minimum.getBuild())
    ctx.e.error(Twine("malformed minimum version: ") + minVersionStr);
  if (platformVersion.sdk.tryParse(sdkVersionStr) ||
      platformVersion.sdk.getBuild())
    ctx.e.error(Twine("malformed sdk version: ") + sdkVersionStr);
  return platformVersion;
}

// Has the side-effect of setting Config::platformInfo and
// potentially Config::secondaryPlatformInfo.
static void setPlatformVersions(Ctx &ctx, StringRef archName,
                                const ArgList &args) {
  std::map<PlatformType, PlatformVersion> platformVersions;
  const PlatformVersion *lastVersionInfo = nullptr;
  for (const Arg *arg : args.filtered(OPT_platform_version)) {
    PlatformVersion version = parsePlatformVersion(ctx, arg);

    // For each platform, the last flag wins:
    // `-platform_version macos 2 3 -platform_version macos 4 5` has the same
    // effect as just passing `-platform_version macos 4 5`.
    // FIXME: ld64 warns on multiple flags for one platform. Should we?
    platformVersions[version.platform] = version;
    lastVersionInfo = &platformVersions[version.platform];
  }

  if (platformVersions.empty()) {
    ctx.e.error("must specify -platform_version");
    return;
  }
  if (platformVersions.size() > 2) {
    ctx.e.error("must specify -platform_version at most twice");
    return;
  }
  if (platformVersions.size() == 2) {
    bool isZipperedCatalyst = platformVersions.count(PLATFORM_MACOS) &&
                              platformVersions.count(PLATFORM_MACCATALYST);

    if (!isZipperedCatalyst) {
      ctx.e.error("lld supports writing zippered outputs only for "
                  "macos and mac-catalyst");
    } else if (ctx.arg.outputType != MH_DYLIB &&
               ctx.arg.outputType != MH_BUNDLE) {
      ctx.e.error("writing zippered outputs only valid for -dylib and -bundle");
    }

    ctx.arg.platformInfo = {
        MachO::Target(getArchitectureFromName(archName), PLATFORM_MACOS,
                      platformVersions[PLATFORM_MACOS].minimum),
        platformVersions[PLATFORM_MACOS].sdk};
    ctx.arg.secondaryPlatformInfo = {
        MachO::Target(getArchitectureFromName(archName), PLATFORM_MACCATALYST,
                      platformVersions[PLATFORM_MACCATALYST].minimum),
        platformVersions[PLATFORM_MACCATALYST].sdk};
    return;
  }

  ctx.arg.platformInfo = {MachO::Target(getArchitectureFromName(archName),
                                        lastVersionInfo->platform,
                                        lastVersionInfo->minimum),
                          lastVersionInfo->sdk};
}

// Has the side-effect of setting Config::target.
static std::unique_ptr<TargetInfo> createTargetInfo(Ctx &ctx,
                                                    InputArgList &args) {
  StringRef archName = args.getLastArgValue(OPT_arch);
  if (archName.empty()) {
    ctx.e.error("must specify -arch");
    return nullptr;
  }

  setPlatformVersions(ctx, archName, args);
  auto [cpuType, cpuSubtype] = getCPUTypeFromArchitecture(ctx.arg.arch());
  switch (cpuType) {
  case CPU_TYPE_X86_64:
    return createX86_64TargetInfo(ctx);
  case CPU_TYPE_ARM64:
    return createARM64TargetInfo(ctx);
  case CPU_TYPE_ARM64_32:
    return createARM64_32TargetInfo(ctx);
  default:
    ctx.e.error("missing or unsupported -arch " + archName);
    return nullptr;
  }
}

static UndefinedSymbolTreatment
getUndefinedSymbolTreatment(Ctx &ctx, const ArgList &args) {
  StringRef treatmentStr = args.getLastArgValue(OPT_undefined);
  auto treatment =
      StringSwitch<UndefinedSymbolTreatment>(treatmentStr)
          .Cases({"error", ""}, UndefinedSymbolTreatment::error)
          .Case("warning", UndefinedSymbolTreatment::warning)
          .Case("suppress", UndefinedSymbolTreatment::suppress)
          .Case("dynamic_lookup", UndefinedSymbolTreatment::dynamic_lookup)
          .Default(UndefinedSymbolTreatment::unknown);
  if (treatment == UndefinedSymbolTreatment::unknown) {
    ctx.e.warn(Twine("unknown -undefined TREATMENT '") + treatmentStr +
               "', defaulting to 'error'");
    treatment = UndefinedSymbolTreatment::error;
  } else if (ctx.arg.namespaceKind == NamespaceKind::twolevel &&
             (treatment == UndefinedSymbolTreatment::warning ||
              treatment == UndefinedSymbolTreatment::suppress)) {
    if (treatment == UndefinedSymbolTreatment::warning)
      ctx.e.fatal("'-undefined warning' only valid with '-flat_namespace'");
    else
      ctx.e.fatal("'-undefined suppress' only valid with '-flat_namespace'");
    treatment = UndefinedSymbolTreatment::error;
  }
  return treatment;
}

static ICFLevel getICFLevel(Ctx &ctx, const ArgList &args) {
  StringRef icfLevelStr = args.getLastArgValue(OPT_icf_eq);
  auto icfLevel = StringSwitch<ICFLevel>(icfLevelStr)
                      .Cases({"none", ""}, ICFLevel::none)
                      .Case("safe", ICFLevel::safe)
                      .Case("safe_thunks", ICFLevel::safe_thunks)
                      .Case("all", ICFLevel::all)
                      .Default(ICFLevel::unknown);

  if ((icfLevel == ICFLevel::safe_thunks) && (ctx.arg.arch() != AK_arm64)) {
    ctx.e.error("--icf=safe_thunks is only supported on arm64 targets");
  }

  if (icfLevel == ICFLevel::unknown) {
    ctx.e.warn(Twine("unknown --icf=OPTION `") + icfLevelStr +
               "', defaulting to `none'");
    icfLevel = ICFLevel::none;
  }
  return icfLevel;
}

static ObjCStubsMode getObjCStubsMode(Ctx &ctx, const ArgList &args) {
  const Arg *arg = args.getLastArg(OPT_objc_stubs_fast, OPT_objc_stubs_small);
  if (!arg)
    return ObjCStubsMode::fast;

  if (arg->getOption().getID() == OPT_objc_stubs_small) {
    if (is_contained({AK_arm64e, AK_arm64}, ctx.arg.arch()))
      return ObjCStubsMode::small;
    else
      ctx.e.warn("-objc_stubs_small is not yet implemented, defaulting to "
                 "-objc_stubs_fast");
  }
  return ObjCStubsMode::fast;
}

static void warnIfDeprecatedOption(Ctx &ctx, const Option &opt) {
  if (!opt.getGroup().isValid())
    return;
  if (opt.getGroup().getID() == OPT_grp_deprecated) {
    ctx.e.warn("Option `" + opt.getPrefixedName() + "' is deprecated in ld64:");
    ctx.e.warn(opt.getHelpText());
  }
}

static void warnIfUnimplementedOption(Ctx &ctx, const Option &opt) {
  if (!opt.getGroup().isValid() || !opt.hasFlag(DriverFlag::HelpHidden))
    return;
  switch (opt.getGroup().getID()) {
  case OPT_grp_deprecated:
    // warn about deprecated options elsewhere
    break;
  case OPT_grp_undocumented:
    ctx.e.warn("Option `" + opt.getPrefixedName() +
               "' is undocumented. Should lld implement it?");
    break;
  case OPT_grp_obsolete:
    ctx.e.warn("Option `" + opt.getPrefixedName() +
               "' is obsolete. Please modernize your usage.");
    break;
  case OPT_grp_ignored:
    ctx.e.warn("Option `" + opt.getPrefixedName() + "' is ignored.");
    break;
  case OPT_grp_ignored_silently:
    break;
  default:
    ctx.e.warn("Option `" + opt.getPrefixedName() +
               "' is not yet implemented. Stay tuned...");
    break;
  }
}

static const char *getReproduceOption(InputArgList &args) {
  if (const Arg *arg = args.getLastArg(OPT_reproduce))
    return arg->getValue();
  return getenv("LLD_REPRODUCE");
}

// Parse options of the form "old;new".
static std::pair<StringRef, StringRef>
getOldNewOptions(Ctx &ctx, opt::InputArgList &args, unsigned id) {
  auto *arg = args.getLastArg(id);
  if (!arg)
    return {"", ""};

  StringRef s = arg->getValue();
  std::pair<StringRef, StringRef> ret = s.split(';');
  if (ret.second.empty())
    ctx.e.error(arg->getSpelling() + " expects 'old;new' format, but got " + s);
  return ret;
}

// Parse options of the form "old;new[;extra]".
static std::tuple<StringRef, StringRef, StringRef>
getOldNewOptionsExtra(Ctx &ctx, opt::InputArgList &args, unsigned id) {
  auto [oldDir, second] = getOldNewOptions(ctx, args, id);
  auto [newDir, extraDir] = second.split(';');
  return {oldDir, newDir, extraDir};
}

static void parseClangOption(Ctx &ctx, StringRef opt, const Twine &msg) {
  std::string err;
  raw_string_ostream os(err);

  const char *argv[] = {"lld", opt.data()};
  if (cl::ParseCommandLineOptions(2, argv, "", &os))
    return;
  ctx.e.error(msg + ": " + StringRef(err).trim());
}

static uint32_t parseDylibVersion(Ctx &ctx, const ArgList &args, unsigned id) {
  const Arg *arg = args.getLastArg(id);
  if (!arg)
    return 0;

  if (ctx.arg.outputType != MH_DYLIB) {
    ctx.e.error(arg->getAsString(args) + ": only valid with -dylib");
    return 0;
  }

  PackedVersion version;
  if (!version.parse32(arg->getValue())) {
    ctx.e.error(arg->getAsString(args) + ": malformed version");
    return 0;
  }

  return version.rawValue();
}

static uint32_t parseProtection(Ctx &ctx, StringRef protStr) {
  uint32_t prot = 0;
  for (char c : protStr) {
    switch (c) {
    case 'r':
      prot |= VM_PROT_READ;
      break;
    case 'w':
      prot |= VM_PROT_WRITE;
      break;
    case 'x':
      prot |= VM_PROT_EXECUTE;
      break;
    case '-':
      break;
    default:
      ctx.e.error("unknown -segprot letter '" + Twine(c) + "' in " + protStr);
      return 0;
    }
  }
  return prot;
}

static std::vector<SectionAlign> parseSectAlign(Ctx &ctx,
                                                const opt::InputArgList &args) {
  std::vector<SectionAlign> sectAligns;
  for (const Arg *arg : args.filtered(OPT_sectalign)) {
    StringRef segName = arg->getValue(0);
    StringRef sectName = arg->getValue(1);
    StringRef alignStr = arg->getValue(2);
    alignStr.consume_front_insensitive("0x");
    uint32_t align;
    if (alignStr.getAsInteger(16, align)) {
      ctx.e.error("-sectalign: failed to parse '" +
                  StringRef(arg->getValue(2)) + "' as number");
      continue;
    }
    if (!isPowerOf2_32(align)) {
      ctx.e.error("-sectalign: '" + StringRef(arg->getValue(2)) +
                  "' (in base 16) not a power of two");
      continue;
    }
    sectAligns.push_back({segName, sectName, align});
  }
  return sectAligns;
}

PlatformType macho::removeSimulator(PlatformType platform) {
  switch (platform) {
  case PLATFORM_IOSSIMULATOR:
    return PLATFORM_IOS;
  case PLATFORM_TVOSSIMULATOR:
    return PLATFORM_TVOS;
  case PLATFORM_WATCHOSSIMULATOR:
    return PLATFORM_WATCHOS;
  case PLATFORM_XROS_SIMULATOR:
    return PLATFORM_XROS;
  default:
    return platform;
  }
}

static bool supportsNoPie(Ctx &ctx) {
  return !(ctx.arg.arch() == AK_arm64 || ctx.arg.arch() == AK_arm64e ||
           ctx.arg.arch() == AK_arm64_32);
}

static bool shouldAdhocSignByDefault(Architecture arch, PlatformType platform) {
  if (arch != AK_arm64 && arch != AK_arm64e)
    return false;

  return platform == PLATFORM_MACOS || platform == PLATFORM_IOSSIMULATOR ||
         platform == PLATFORM_TVOSSIMULATOR ||
         platform == PLATFORM_WATCHOSSIMULATOR ||
         platform == PLATFORM_XROS_SIMULATOR;
}

template <std::size_t N>
using MinVersions = std::array<std::pair<PlatformType, VersionTuple>, N>;

/// Returns true if the platform is greater than the min version.
/// Returns false if the platform does not exist.
template <std::size_t N>
static bool greaterEqMinVersion(Ctx &ctx, const MinVersions<N> &minVersions,
                                bool ignoreSimulator) {
  PlatformType platform = ctx.arg.platformInfo.target.Platform;
  if (ignoreSimulator)
    platform = removeSimulator(platform);
  auto it = llvm::find_if(minVersions,
                          [&](const auto &p) { return p.first == platform; });
  if (it != minVersions.end())
    if (ctx.arg.platformInfo.target.MinDeployment >= it->second)
      return true;
  return false;
}

static bool dataConstDefault(Ctx &ctx, const InputArgList &args) {
  static const MinVersions<6> minVersion = {{
      {PLATFORM_MACOS, VersionTuple(10, 15)},
      {PLATFORM_IOS, VersionTuple(13, 0)},
      {PLATFORM_TVOS, VersionTuple(13, 0)},
      {PLATFORM_WATCHOS, VersionTuple(6, 0)},
      {PLATFORM_XROS, VersionTuple(1, 0)},
      {PLATFORM_BRIDGEOS, VersionTuple(4, 0)},
  }};
  if (!greaterEqMinVersion(ctx, minVersion, true))
    return false;

  switch (ctx.arg.outputType) {
  case MH_EXECUTE:
    return !(args.hasArg(OPT_no_pie) && supportsNoPie(ctx));
  case MH_BUNDLE:
    // FIXME: return false when -final_name ...
    // has prefix "/System/Library/UserEventPlugins/"
    // or matches "/usr/libexec/locationd" "/usr/libexec/terminusd"
    return true;
  case MH_DYLIB:
    return true;
  case MH_OBJECT:
    return false;
  default:
    llvm_unreachable(
        "unsupported output type for determining data-const default");
  }
  return false;
}

static bool shouldEmitChainedFixups(Ctx &ctx, const InputArgList &args) {
  const Arg *arg = args.getLastArg(OPT_fixup_chains, OPT_no_fixup_chains);
  if (arg && arg->getOption().matches(OPT_no_fixup_chains))
    return false;

  bool requested = arg && arg->getOption().matches(OPT_fixup_chains);
  if (!ctx.arg.isPic) {
    if (requested)
      ctx.e.error("-fixup_chains is incompatible with -no_pie");

    return false;
  }

  if (!is_contained({AK_x86_64, AK_x86_64h, AK_arm64}, ctx.arg.arch())) {
    if (requested)
      ctx.e.error(
          "-fixup_chains is only supported on x86_64 and arm64 targets");

    return false;
  }

  if (args.hasArg(OPT_preload)) {
    if (requested)
      ctx.e.error("-fixup_chains is incompatible with -preload");

    return false;
  }

  if (requested)
    return true;

  static const MinVersions<9> minVersion = {{
      {PLATFORM_IOS, VersionTuple(13, 4)},
      {PLATFORM_IOSSIMULATOR, VersionTuple(16, 0)},
      {PLATFORM_MACOS, VersionTuple(13, 0)},
      {PLATFORM_TVOS, VersionTuple(14, 0)},
      {PLATFORM_TVOSSIMULATOR, VersionTuple(15, 0)},
      {PLATFORM_WATCHOS, VersionTuple(7, 0)},
      {PLATFORM_WATCHOSSIMULATOR, VersionTuple(8, 0)},
      {PLATFORM_XROS, VersionTuple(1, 0)},
      {PLATFORM_XROS_SIMULATOR, VersionTuple(1, 0)},
  }};
  return greaterEqMinVersion(ctx, minVersion, false);
}

static bool shouldEmitRelativeMethodLists(Ctx &ctx, const InputArgList &args) {
  const Arg *arg = args.getLastArg(OPT_objc_relative_method_lists,
                                   OPT_no_objc_relative_method_lists);
  if (arg && arg->getOption().getID() == OPT_objc_relative_method_lists)
    return true;
  if (arg && arg->getOption().getID() == OPT_no_objc_relative_method_lists)
    return false;

  // If no flag is specified, enable this on newer versions by default.
  // The min versions is taken from
  // ld64(https://github.com/apple-oss-distributions/ld64/blob/47f477cb721755419018f7530038b272e9d0cdea/src/ld/ld.hpp#L310)
  // to mimic to operation of ld64
  // [here](https://github.com/apple-oss-distributions/ld64/blob/47f477cb721755419018f7530038b272e9d0cdea/src/ld/Options.cpp#L6085-L6101)
  static const MinVersions<6> minVersion = {{
      {PLATFORM_MACOS, VersionTuple(10, 16)},
      {PLATFORM_IOS, VersionTuple(14, 0)},
      {PLATFORM_WATCHOS, VersionTuple(7, 0)},
      {PLATFORM_TVOS, VersionTuple(14, 0)},
      {PLATFORM_BRIDGEOS, VersionTuple(5, 0)},
      {PLATFORM_XROS, VersionTuple(1, 0)},
  }};
  return greaterEqMinVersion(ctx, minVersion, true);
}

void SymbolPatterns::clear() {
  literals.clear();
  globs.clear();
}

void SymbolPatterns::insert(Ctx &ctx, StringRef symbolName) {
  Expected<GlobPattern> pattern = GlobPattern::create(symbolName);
  if (!pattern) {
    ctx.e.error("invalid symbol-name pattern: " + symbolName + ": " +
                toString(pattern.takeError()));
    return;
  }
  // A pattern that denotes a single string is kept as a literal: literals are
  // matched by hash lookup, and only literals seed the force-load of lazy
  // archive members below.
  if (std::optional<std::string> literal = pattern->asLiteral()) {
    literals.insert(CachedHashStringRef(ctx.saver.save(*literal)));
    return;
  }
  globs.emplace_back(std::move(*pattern));
}

bool SymbolPatterns::matchLiteral(StringRef symbolName) const {
  return literals.contains(CachedHashStringRef(symbolName));
}

bool SymbolPatterns::matchGlob(StringRef symbolName) const {
  for (const GlobPattern &glob : globs)
    if (glob.match(symbolName))
      return true;
  return false;
}

bool SymbolPatterns::match(StringRef symbolName) const {
  return matchLiteral(symbolName) || matchGlob(symbolName);
}

static void parseSymbolPatternsFile(Ctx &ctx, const Arg *arg,
                                    SymbolPatterns &symbolPatterns) {
  StringRef path = arg->getValue();
  std::optional<MemoryBufferRef> buffer = readFile(ctx, path);
  if (!buffer) {
    ctx.e.error("Could not read symbol file: " + path);
    return;
  }
  MemoryBufferRef mbref = *buffer;
  for (StringRef line : args::getLines(mbref)) {
    line = line.take_until([](char c) { return c == '#'; }).trim();
    if (!line.empty())
      symbolPatterns.insert(ctx, line);
  }
}

static void handleSymbolPatterns(Ctx &ctx, InputArgList &args,
                                 SymbolPatterns &symbolPatterns,
                                 unsigned singleOptionCode,
                                 unsigned listFileOptionCode) {
  for (const Arg *arg : args.filtered(singleOptionCode))
    symbolPatterns.insert(ctx, arg->getValue());
  for (const Arg *arg : args.filtered(listFileOptionCode))
    parseSymbolPatternsFile(ctx, arg, symbolPatterns);
}

static void createFiles(Ctx &ctx, const InputArgList &args) {
  TimeTraceScope timeScope("Load input files");
  // This loop should be reserved for options whose exact ordering matters.
  // Other options should be handled via filtered() and/or getLastArg().
  bool isLazy = false;
  // If we've processed an opening --start-lib, without a matching --end-lib
  bool inLib = false;
  DeferredFiles deferredFiles;

  for (const Arg *arg : args) {
    const Option &opt = arg->getOption();
    warnIfDeprecatedOption(ctx, opt);
    warnIfUnimplementedOption(ctx, opt);

    switch (opt.getID()) {
    case OPT_INPUT:
      deferFile(ctx, rerootPath(ctx, arg->getValue()), isLazy, deferredFiles);
      break;
    case OPT_needed_library:
      deferFile(ctx, rerootPath(ctx, arg->getValue()), /*isLazy=*/false,
                deferredFiles, LoadType::CommandLine, /*isNeeded=*/true);
      break;
    case OPT_reexport_library:
      deferFile(ctx, rerootPath(ctx, arg->getValue()), /*isLazy=*/false,
                deferredFiles, LoadType::CommandLine, /*isNeeded=*/false,
                /*isWeak=*/false,
                /*isReexport=*/true);
      break;
    case OPT_weak_library:
      deferFile(ctx, rerootPath(ctx, arg->getValue()), /*isLazy=*/false,
                deferredFiles, LoadType::CommandLine, /*isNeeded=*/false,
                /*isWeak=*/true);
      break;
    case OPT_filelist:
      addFileList(ctx, arg->getValue(), isLazy, deferredFiles);
      break;
    case OPT_force_load:
      deferFile(ctx, rerootPath(ctx, arg->getValue()), /*isLazy=*/false,
                deferredFiles, LoadType::CommandLineForce);
      break;
    case OPT_load_hidden:
      deferFile(ctx, rerootPath(ctx, arg->getValue()), /*isLazy=*/false,
                deferredFiles, LoadType::CommandLine, /*isNeeded=*/false,
                /*isWeak=*/false,
                /*isReexport=*/false, /*isHidden=*/true);
      break;
    case OPT_l:
    case OPT_needed_l:
    case OPT_reexport_l:
    case OPT_weak_l:
    case OPT_hidden_l:
      addLibrary(ctx, arg->getValue(), opt.getID() == OPT_needed_l,
                 opt.getID() == OPT_weak_l, opt.getID() == OPT_reexport_l,
                 opt.getID() == OPT_hidden_l,
                 /*isExplicit=*/true, LoadType::CommandLine, deferredFiles);
      break;
    case OPT_framework:
    case OPT_needed_framework:
    case OPT_reexport_framework:
    case OPT_weak_framework:
      addFramework(ctx, arg->getValue(), opt.getID() == OPT_needed_framework,
                   opt.getID() == OPT_weak_framework,
                   opt.getID() == OPT_reexport_framework, /*isExplicit=*/true,
                   LoadType::CommandLine, deferredFiles);
      break;
    case OPT_start_lib:
      if (inLib)
        ctx.e.error("nested --start-lib");
      inLib = true;
      if (!ctx.arg.allLoad)
        isLazy = true;
      break;
    case OPT_end_lib:
      if (!inLib)
        ctx.e.error("stray --end-lib");
      inLib = false;
      isLazy = false;
      break;
    default:
      break;
    }
  }

#if LLVM_ENABLE_THREADS
  if (ctx.arg.readWorkers) {
    multiThreadedPageIn(ctx, deferredFiles);

    DeferredFiles archiveContents;
    for (auto &file : deferredFiles) {
      if (ctx.loadedObjectFrameworks.contains(file.path))
        continue;

      auto inputFile =
          processFile(ctx, file.buffer, &archiveContents, file.path,
                      file.loadType, file.isLazy, file.isExplicit,
                      /*isBundleLoader=*/false, file.isHidden);
      applyDylibMetadata(ctx, inputFile, file.isNeeded, file.isWeak,
                         file.isReexport);
      checkAndCacheFramework(ctx, inputFile, file.path);

      if (ArchiveFile *archive = dyn_cast<ArchiveFile>(inputFile))
        archive->addLazySymbols();
    }

    if (!archiveContents.empty())
      multiThreadedPageIn(ctx, archiveContents);

    ctx.pageInQueue->stopAllWork = true;
  }
#endif
}

static void gatherInputSections(Ctx &ctx) {
  TimeTraceScope timeScope("Gathering input sections");
  for (const InputFile *file : ctx.inputFiles) {
    for (const Section *section : file->sections) {
      // Compact unwind entries require special handling elsewhere. (In
      // contrast, EH frames are handled like regular ConcatInputSections.)
      if (section->name == section_names::compactUnwind)
        continue;
      // Addrsig sections contain metadata only needed at link time.
      if (section->name == section_names::addrSig)
        continue;
      for (const Subsection &subsection : section->subsections)
        addInputSection(ctx, subsection.isec);
    }
    if (!file->objCImageInfo.empty())
      ctx.in.objCImageInfo->addFile(file);
  }
}

static void codegenDataGenerate(Ctx &ctx) {
  TimeTraceScope timeScope("Generating codegen data");

  OutlinedHashTreeRecord globalOutlineRecord;
  StableFunctionMapRecord globalMergeRecord;
  for (ConcatInputSection *isec : ctx.inputSections) {
    if (isec->getSegName() != segment_names::data)
      continue;
    if (isec->getName() == section_names::outlinedHashTree) {
      // Read outlined hash tree from each section.
      OutlinedHashTreeRecord localOutlineRecord;
      // Use a pointer to allow modification by the function.
      auto *data = isec->data.data();
      localOutlineRecord.deserialize(data);

      // Merge it to the global hash tree.
      globalOutlineRecord.merge(localOutlineRecord);
    }
    if (isec->getName() == section_names::functionMap) {
      // Read stable functions from each section.
      StableFunctionMapRecord localMergeRecord;
      // Use a pointer to allow modification by the function.
      auto *data = isec->data.data();
      localMergeRecord.deserialize(data);

      // Merge it to the global function map.
      globalMergeRecord.merge(localMergeRecord);
    }
  }

  globalMergeRecord.finalize();

  CodeGenDataWriter Writer;
  if (!globalOutlineRecord.empty())
    Writer.addRecord(globalOutlineRecord);
  if (!globalMergeRecord.empty())
    Writer.addRecord(globalMergeRecord);

  std::error_code EC;
  auto fileName = ctx.arg.codegenDataGeneratePath;
  assert(!fileName.empty());
  raw_fd_ostream Output(fileName, EC, sys::fs::OF_None);
  if (EC)
    ctx.e.error("fail to create " + fileName + ": " + EC.message());

  if (auto E = Writer.write(Output))
    ctx.e.error("fail to write CGData: " + toString(std::move(E)));
}

static void foldIdenticalLiterals(Ctx &ctx) {
  TimeTraceScope timeScope("Fold identical literals");
  // We always create a cStringSection, regardless of whether dedupLiterals is
  // true. If it isn't, we simply create a non-deduplicating CStringSection.
  // Either way, we must unconditionally finalize it here.
  for (auto *sec : ctx.in.cStringSections)
    sec->finalizeContents();
  ctx.in.wordLiteralSection->finalizeContents();
}

static void addSynthenticMethnames(Ctx &ctx) {
  std::string &data = *ctx.make<std::string>();
  llvm::raw_string_ostream os(data);
  for (Symbol *sym : ctx.symtab->getSymbols())
    if (isa<Undefined>(sym))
      if (ObjCStubsSection::isObjCStubSymbol(sym))
        os << ObjCStubsSection::getMethname(sym) << '\0';

  if (data.empty())
    return;

  const auto *buf = reinterpret_cast<const uint8_t *>(data.c_str());
  Section &section = *ctx.make<Section>(
      ctx, /*file=*/nullptr, segment_names::text, section_names::objcMethname,
      S_CSTRING_LITERALS, /*addr=*/0);

  auto *isec = ctx.make<CStringInputSection>(
      section, ArrayRef<uint8_t>{buf, data.size()},
      /*align=*/1, /*dedupLiterals=*/true);
  isec->splitIntoPieces();
  for (auto &piece : isec->pieces)
    piece.live = true;
  section.subsections.push_back({0, isec});
  ctx.in.objcMethnameSection->addInput(isec);
  ctx.in.objcMethnameSection->isec->markLive(0);
}

static void referenceStubBinder(Ctx &ctx) {
  bool needsStubHelper = ctx.arg.outputType == MH_DYLIB ||
                         ctx.arg.outputType == MH_EXECUTE ||
                         ctx.arg.outputType == MH_BUNDLE;
  if (!needsStubHelper || !ctx.symtab->find("dyld_stub_binder"))
    return;

  // dyld_stub_binder is used by dyld to resolve lazy bindings. This code here
  // adds a opportunistic reference to dyld_stub_binder if it happens to exist.
  // dyld_stub_binder is in libSystem.dylib, which is usually linked in. This
  // isn't needed for correctness, but the presence of that symbol suppresses
  // "no symbols" diagnostics from `nm`.
  // StubHelperSection::setUp() adds a reference and errors out if
  // dyld_stub_binder doesn't exist in case it is actually needed.
  ctx.symtab->addUndefined("dyld_stub_binder", /*file=*/nullptr,
                           /*isWeak=*/false);
}

static void createAliases(Ctx &ctx) {
  for (const auto &pair : ctx.arg.aliasedSymbols) {
    if (const auto &sym = ctx.symtab->find(pair.first)) {
      if (const auto &defined = dyn_cast<Defined>(sym)) {
        ctx.symtab->aliasDefined(defined, pair.second, defined->getFile())
            ->noDeadStrip = true;
      } else {
        ctx.e.error("TODO: support aliasing to symbols of kind " +
                    Twine(sym->kind()));
      }
    } else {
      ctx.e.warn("undefined base symbol '" + pair.first + "' for alias '" +
                 pair.second + "'\n");
    }
  }

  for (const InputFile *file : ctx.inputFiles) {
    if (auto *objFile = dyn_cast<ObjFile>(file)) {
      for (const AliasSymbol *alias : objFile->aliases) {
        if (const auto &aliased = ctx.symtab->find(alias->getAliasedName())) {
          if (const auto &defined = dyn_cast<Defined>(aliased)) {
            ctx.symtab->aliasDefined(defined, alias->getName(),
                                     alias->getFile(), alias->privateExtern);
          } else {
            // Common, dylib, and undefined symbols are all valid alias
            // referents (undefineds can become valid Defined symbols later on
            // in the link.)
            ctx.e.error("TODO: support aliasing to symbols of kind " +
                        Twine(aliased->kind()));
          }
        } else {
          // This shouldn't happen since MC generates undefined symbols to
          // represent the alias referents. Thus we fatal() instead of just
          // warning here.
          ctx.e.fatal("unable to find alias referent " +
                      alias->getAliasedName() + " for " + alias->getName());
        }
      }
    }
  }
}

static void handleExplicitExports(Ctx &ctx) {
  static constexpr int kMaxWarnings = 3;
  if (ctx.arg.hasExplicitExports) {
    std::atomic<uint64_t> warningsCount{0};
    parallelForEach(ctx.symtab->getSymbols(), [&ctx,
                                               &warningsCount](Symbol *sym) {
      if (auto *defined = dyn_cast<Defined>(sym)) {
        if (ctx.arg.exportedSymbols.match(sym->getName())) {
          if (defined->privateExtern) {
            if (defined->weakDefCanBeHidden) {
              // weak_def_can_be_hidden symbols behave similarly to
              // private_extern symbols in most cases, except for when
              // it is explicitly exported.
              // The former can be exported but the latter cannot.
              defined->privateExtern = false;
            } else {
              // Only print the first 3 warnings verbosely, and
              // shorten the rest to avoid crowding logs.
              if (warningsCount.fetch_add(1, std::memory_order_relaxed) <
                  kMaxWarnings)
                ctx.e.warn("cannot export hidden symbol " +
                           toString(ctx, *defined) + "\n>>> defined in " +
                           toString(defined->getFile()));
            }
          }
        } else {
          defined->privateExtern = true;
        }
      } else if (auto *dysym = dyn_cast<DylibSymbol>(sym)) {
        dysym->shouldReexport = ctx.arg.exportedSymbols.match(sym->getName());
      }
    });
    if (warningsCount > kMaxWarnings)
      ctx.e.warn("<... " + Twine(warningsCount - kMaxWarnings) +
                 " more similar warnings...>");
  } else if (!ctx.arg.unexportedSymbols.empty()) {
    parallelForEach(ctx.symtab->getSymbols(), [&ctx](Symbol *sym) {
      if (auto *defined = dyn_cast<Defined>(sym))
        if (ctx.arg.unexportedSymbols.match(defined->getName()))
          defined->privateExtern = true;
    });
  }
}

static void eraseInitializerSymbols(Ctx &ctx) {
  for (ConcatInputSection *isec : ctx.in.initOffsets->inputs())
    for (Defined *sym : isec->symbols)
      sym->used = false;
}

static SmallVector<StringRef, 0> getRuntimePaths(Ctx &ctx,
                                                 opt::InputArgList &args) {
  SmallVector<StringRef, 0> vals;
  DenseSet<StringRef> seen;
  for (const Arg *arg : args.filtered(OPT_rpath)) {
    StringRef val = arg->getValue();
    if (seen.insert(val).second)
      vals.push_back(val);
    else if (ctx.arg.warnDuplicateRpath)
      ctx.e.warn("duplicate -rpath '" + val +
                 "' ignored [--warn-duplicate-rpath]");
  }
  return vals;
}

static SmallVector<StringRef, 0> getAllowableClients(opt::InputArgList &args) {
  SmallVector<StringRef, 0> vals;
  DenseSet<StringRef> seen;
  for (const Arg *arg : args.filtered(OPT_allowable_client)) {
    StringRef val = arg->getValue();
    if (seen.insert(val).second)
      vals.push_back(val);
  }
  return vals;
}

static void computeColdness(Ctx &ctx) {
  TimeTraceScope timeScope("Compute coldness");
  for (InputSection *isec : ctx.inputSections) {
    if (!isCodeSection(isec))
      continue;
    isec->isCold =
        llvm::any_of(isec->symbols, [](Defined *sym) { return sym->isCold(); });
  }
}

namespace lld {
namespace macho {
Ctx::Ctx() {
  priorityBuilder = std::make_unique<PriorityBuilder>(*this);
  thunkMap = std::make_unique<ThunkMap>();
#if LLVM_ENABLE_THREADS
  pageInQueue = std::make_unique<SerialBackgroundWorkQueue>();
#endif
}

Ctx::~Ctx() = default;

static bool linkImpl(Ctx &ctx, ArrayRef<const char *> argsArr) {
  ctx.e.logName = args::getFilenameWithoutExe(argsArr[0]);

  MachOOptTable parser;
  InputArgList args = parser.parse(ctx, argsArr.slice(1));

  ctx.e.errorLimitExceededMsg = "too many errors emitted, stopping now "
                                "(use --error-limit=0 to see all errors)";
  ctx.e.errorLimit = args::getInteger(ctx.e, args, OPT_error_limit_eq, 20);
  ctx.e.verbose = args.hasArg(OPT_verbose);

  if (args.hasArg(OPT_help_hidden)) {
    parser.printHelp(ctx, argsArr[0], /*showHidden=*/true);
    return true;
  }
  if (args.hasArg(OPT_help)) {
    parser.printHelp(ctx, argsArr[0], /*showHidden=*/false);
    return true;
  }
  if (args.hasArg(OPT_version)) {
    ctx.e.message(getLLDVersion(), ctx.e.outs());
    return true;
  }

  ctx.symtab = std::make_unique<SymbolTable>(ctx);
  ctx.arg.outputType = getOutputType(args);
  ctx.target = createTargetInfo(ctx, args);
  ctx.depTracker = std::make_unique<DependencyTracker>(
      ctx, args.getLastArgValue(OPT_dependency_info));

  ctx.arg.ltoo = args::getInteger(ctx.e, args, OPT_lto_O, 2);
  if (ctx.arg.ltoo > 3)
    ctx.e.error("--lto-O: invalid optimization level: " + Twine(ctx.arg.ltoo));
  unsigned ltoCgo = args::getInteger(ctx.e, args, OPT_lto_CGO,
                                     args::getCGOptLevel(ctx.arg.ltoo));
  if (auto level = CodeGenOpt::getLevel(ltoCgo))
    ctx.arg.ltoCgo = *level;
  else
    ctx.e.error("--lto-CGO: invalid codegen optimization level: " +
                Twine(ltoCgo));

  if (ctx.e.errorCount)
    return false;

  if (args.hasArg(OPT_pagezero_size)) {
    uint64_t pagezeroSize = args::getHex(ctx.e, args, OPT_pagezero_size, 0);

    // ld64 does something really weird. It attempts to realign the value to the
    // page size, but assumes the page size is 4K. This doesn't work with most
    // of Apple's ARM64 devices, which use a page size of 16K. This means that
    // it will first 4K align it by rounding down, then round up to 16K.  This
    // probably only happened because no one using this arg with anything other
    // then 0, so no one checked if it did what is what it says it does.

    // So we are not copying this weird behavior and doing the it in a logical
    // way, by always rounding down to page size.
    if (!isAligned(Align(ctx.target->getPageSize()), pagezeroSize)) {
      pagezeroSize -= pagezeroSize % ctx.target->getPageSize();
      ctx.e.warn("__PAGEZERO size is not page aligned, rounding down to 0x" +
                 Twine::utohexstr(pagezeroSize));
    }

    ctx.target->pageZeroSize = pagezeroSize;
  }

  ctx.arg.osoPrefix = args.getLastArgValue(OPT_oso_prefix);
  if (!ctx.arg.osoPrefix.empty()) {
    // The max path length is 4096, in theory. However that seems quite long
    // and seems unlikely that any one would want to strip everything from the
    // path. Hence we've picked a reasonably large number here.
    SmallString<1024> expanded;
    // Expand "." into the current working directory.
    if (ctx.arg.osoPrefix == "." && !fs::current_path(expanded)) {
      // Note: LD64 expands "." to be `<current_dir>/
      // (ie., it has a slash suffix) whereas current_path() doesn't.
      // So we have to append '/' to be consistent because this is
      // meaningful for our text based stripping.
      expanded += sys::path::get_separator();
    } else {
      expanded = ctx.arg.osoPrefix;
    }
    ctx.arg.osoPrefix = ctx.saver.save(expanded.str());
  }

  bool pie = args.hasFlag(OPT_pie, OPT_no_pie, true);
  if (!supportsNoPie(ctx) && !pie) {
    ctx.e.warn("-no_pie ignored for arm64");
    pie = true;
  }

  ctx.arg.isPic = ctx.arg.outputType == MH_DYLIB ||
                  ctx.arg.outputType == MH_BUNDLE ||
                  (ctx.arg.outputType == MH_EXECUTE && pie);

  // Must be set before any InputSections and Symbols are created.
  ctx.arg.deadStrip = args.hasArg(OPT_dead_strip);
  ctx.arg.interposable = args.hasArg(OPT_interposable);

  ctx.arg.systemLibraryRoots = getSystemLibraryRoots(args);
  if (const char *path = getReproduceOption(args)) {
    // Note that --reproduce is a debug option so you can ignore it
    // if you are trying to understand the whole picture of the code.
    Expected<std::unique_ptr<TarWriter>> errOrWriter =
        TarWriter::create(path, path::stem(path));
    if (errOrWriter) {
      ctx.tar = std::move(*errOrWriter);
      ctx.tar->append("response.txt", createResponseFile(ctx, args));
      ctx.tar->append("version.txt", getLLDVersion() + "\n");
    } else {
      ctx.e.error("--reproduce: " + toString(errOrWriter.takeError()));
    }
  }

  if (auto *arg = args.getLastArg(OPT_read_workers)) {
#if LLVM_ENABLE_THREADS
    StringRef v(arg->getValue());
    unsigned workers = 0;
    if (!llvm::to_integer(v, workers, 0))
      ctx.e.error(arg->getSpelling() +
                  ": expected a non-negative integer, but got '" +
                  arg->getValue() + "'");
    ctx.arg.readWorkers = workers;
#else
    ctx.e.warn(
        arg->getSpelling() +
        ": option unavailable because lld was not built with thread support");
#endif
  }
  if (auto *arg = args.getLastArg(OPT_threads_eq)) {
    StringRef v(arg->getValue());
    unsigned threads = 0;
    if (!llvm::to_integer(v, threads, 0) || threads == 0)
      ctx.e.error(arg->getSpelling() +
                  ": expected a positive integer, but got '" + arg->getValue() +
                  "'");
    parallel::strategy = hardware_concurrency(threads);
    ctx.arg.thinLTOJobs = v;
  }
  if (auto *arg = args.getLastArg(OPT_thinlto_jobs_eq))
    ctx.arg.thinLTOJobs = arg->getValue();
  if (!get_threadpool_strategy(ctx.arg.thinLTOJobs))
    ctx.e.error("--thinlto-jobs: invalid job count: " + ctx.arg.thinLTOJobs);

  for (const Arg *arg : args.filtered(OPT_u)) {
    ctx.arg.explicitUndefineds.push_back(ctx.symtab->addUndefined(
        arg->getValue(), /*file=*/nullptr, /*isWeakRef=*/false));
  }

  for (const Arg *arg : args.filtered(OPT_U))
    ctx.arg.explicitDynamicLookups.insert(arg->getValue());

  ctx.arg.mapFile = args.getLastArgValue(OPT_map);
  ctx.arg.optimize = args::getInteger(ctx.e, args, OPT_O, 1);
  ctx.arg.outputFile = args.getLastArgValue(OPT_o, "a.out");
  ctx.arg.finalOutput =
      args.getLastArgValue(OPT_final_output, ctx.arg.outputFile);
  ctx.arg.astPaths = args.getAllArgValues(OPT_add_ast_path);
  ctx.arg.headerPad = args::getHex(ctx.e, args, OPT_headerpad, /*Default=*/32);
  ctx.arg.headerPadMaxInstallNames =
      args.hasArg(OPT_headerpad_max_install_names);
  ctx.arg.printDylibSearch =
      args.hasArg(OPT_print_dylib_search) || getenv("RC_TRACE_DYLIB_SEARCHING");
  ctx.arg.printEachFile = args.hasArg(OPT_t);
  ctx.arg.printWhyLoad = args.hasArg(OPT_why_load);
  ctx.arg.omitDebugInfo = args.hasArg(OPT_S);
  ctx.arg.errorForArchMismatch = args.hasArg(OPT_arch_errors_fatal);
  if (const Arg *arg = args.getLastArg(OPT_bundle_loader)) {
    if (ctx.arg.outputType != MH_BUNDLE)
      ctx.e.error("-bundle_loader can only be used with MachO bundle output");
    addFile(ctx, arg->getValue(), LoadType::CommandLine, /*isLazy=*/false,
            /*isExplicit=*/false, /*isBundleLoader=*/true);
  }
  for (auto *arg : args.filtered(OPT_dyld_env)) {
    StringRef envPair(arg->getValue());
    if (!envPair.contains('='))
      ctx.e.error("-dyld_env's argument is  malformed. Expected "
                  "-dyld_env <ENV_VAR>=<VALUE>, got `" +
                  envPair + "`");
    ctx.arg.dyldEnvs.push_back(envPair);
  }
  if (!ctx.arg.dyldEnvs.empty() && ctx.arg.outputType != MH_EXECUTE)
    ctx.e.error("-dyld_env can only be used when creating executable output");

  if (const Arg *arg = args.getLastArg(OPT_umbrella)) {
    if (ctx.arg.outputType != MH_DYLIB)
      ctx.e.warn("-umbrella used, but not creating dylib");
    ctx.arg.umbrella = arg->getValue();
  }
  ctx.arg.ltoObjPath = args.getLastArgValue(OPT_object_path_lto);
  ctx.arg.ltoNewPmPasses = args.getLastArgValue(OPT_lto_newpm_passes);
  ctx.arg.thinLTOCacheDir = args.getLastArgValue(OPT_cache_path_lto);
  ctx.arg.thinLTOCachePolicy = getLTOCachePolicy(ctx, args);
  ctx.arg.thinLTOEmitImportsFiles = args.hasArg(OPT_thinlto_emit_imports_files);
  ctx.arg.thinLTOEmitIndexFiles = args.hasArg(OPT_thinlto_emit_index_files) ||
                                  args.hasArg(OPT_thinlto_index_only) ||
                                  args.hasArg(OPT_thinlto_index_only_eq);
  ctx.arg.thinLTOIndexOnly = args.hasArg(OPT_thinlto_index_only) ||
                             args.hasArg(OPT_thinlto_index_only_eq);
  ctx.arg.thinLTOIndexOnlyArg = args.getLastArgValue(OPT_thinlto_index_only_eq);
  ctx.arg.thinLTOObjectSuffixReplace =
      getOldNewOptions(ctx, args, OPT_thinlto_object_suffix_replace_eq);
  std::tie(ctx.arg.thinLTOPrefixReplaceOld, ctx.arg.thinLTOPrefixReplaceNew,
           ctx.arg.thinLTOPrefixReplaceNativeObject) =
      getOldNewOptionsExtra(ctx, args, OPT_thinlto_prefix_replace_eq);
  if (ctx.arg.thinLTOEmitIndexFiles && !ctx.arg.thinLTOIndexOnly) {
    if (args.hasArg(OPT_thinlto_object_suffix_replace_eq))
      ctx.e.error("--thinlto-object-suffix-replace is not supported with "
                  "--thinlto-emit-index-files");
    else if (args.hasArg(OPT_thinlto_prefix_replace_eq))
      ctx.e.error("--thinlto-prefix-replace is not supported with "
                  "--thinlto-emit-index-files");
  }
  if (!ctx.arg.thinLTOPrefixReplaceNativeObject.empty() &&
      ctx.arg.thinLTOIndexOnlyArg.empty()) {
    ctx.e.error(
        "--thinlto-prefix-replace=old_dir;new_dir;obj_dir must be used with "
        "--thinlto-index-only=");
  }
  ctx.arg.warnDuplicateRpath =
      args.hasFlag(OPT_warn_duplicate_rpath, OPT_no_warn_duplicate_rpath, true);
  ctx.arg.runtimePaths = getRuntimePaths(ctx, args);
  ctx.arg.allowableClients = getAllowableClients(args);
  ctx.arg.allLoad = args.hasFlag(OPT_all_load, OPT_noall_load, false);
  ctx.arg.archMultiple = args.hasArg(OPT_arch_multiple);
  ctx.arg.applicationExtension = args.hasFlag(
      OPT_application_extension, OPT_no_application_extension, false);
  ctx.arg.exportDynamic = args.hasArg(OPT_export_dynamic);
  ctx.arg.forceLoadObjC = args.hasArg(OPT_ObjC);
  ctx.arg.forceLoadSwift = args.hasArg(OPT_force_load_swift_libs);
  ctx.arg.deadStripDylibs = args.hasArg(OPT_dead_strip_dylibs);
  ctx.arg.demangle = args.hasArg(OPT_demangle);
  ctx.arg.implicitDylibs = !args.hasArg(OPT_no_implicit_dylibs);
  ctx.arg.emitFunctionStarts =
      args.hasFlag(OPT_function_starts, OPT_no_function_starts, true);
  ctx.arg.emitDataInCodeInfo =
      args.hasFlag(OPT_data_in_code_info, OPT_no_data_in_code_info, true);
  ctx.arg.emitChainedFixups = shouldEmitChainedFixups(ctx, args);
  ctx.arg.emitInitOffsets =
      ctx.arg.emitChainedFixups || args.hasArg(OPT_init_offsets);
  ctx.arg.emitRelativeMethodLists = shouldEmitRelativeMethodLists(ctx, args);
  ctx.arg.icfLevel = getICFLevel(ctx, args);
  ctx.arg.keepICFStabs = args.hasArg(OPT_keep_icf_stabs);
  ctx.arg.dedupStrings =
      args.hasFlag(OPT_deduplicate_strings, OPT_no_deduplicate_strings, true);
  ctx.arg.dedupSymbolStrings = !args.hasArg(OPT_no_deduplicate_symbol_strings);
  ctx.arg.deadStripDuplicates = args.hasArg(OPT_dead_strip_duplicates);
  ctx.arg.stripSwiftForceLoad =
      args.hasFlag(OPT_strip_swift_force_load, OPT_no_strip_swift_force_load,
                   /*Default=*/false);
  ctx.arg.warnDylibInstallName = args.hasFlag(
      OPT_warn_dylib_install_name, OPT_no_warn_dylib_install_name, false);
  ctx.arg.ignoreOptimizationHints = args.hasArg(OPT_ignore_optimization_hints);
  ctx.arg.callGraphProfileSort = args.hasFlag(
      OPT_call_graph_profile_sort, OPT_no_call_graph_profile_sort, true);
  ctx.arg.printSymbolOrder = args.getLastArgValue(OPT_print_symbol_order_eq);
  ctx.arg.forceExactCpuSubtypeMatch =
      getenv("LD_DYLIB_CPU_SUBTYPES_MUST_MATCH");
  ctx.arg.objcStubsMode = getObjCStubsMode(ctx, args);
  ctx.arg.ignoreAutoLink = args.hasArg(OPT_ignore_auto_link);
  for (const Arg *arg : args.filtered(OPT_ignore_auto_link_option))
    ctx.arg.ignoreAutoLinkOptions.insert(arg->getValue());
  ctx.arg.strictAutoLink = args.hasArg(OPT_strict_auto_link);
  ctx.arg.ltoDebugPassManager = args.hasArg(OPT_lto_debug_pass_manager);
  ctx.arg.emitLLVM = args.hasArg(OPT_lto_emit_llvm);
  ctx.arg.codegenDataGeneratePath =
      args.getLastArgValue(OPT_codegen_data_generate_path);
  ctx.arg.csProfileGenerate = args.hasArg(OPT_cs_profile_generate);
  ctx.arg.csProfilePath = args.getLastArgValue(OPT_cs_profile_path);
  ctx.arg.pgoWarnMismatch =
      args.hasFlag(OPT_pgo_warn_mismatch, OPT_no_pgo_warn_mismatch, true);
  ctx.arg.warnThinArchiveMissingMembers =
      args.hasFlag(OPT_warn_thin_archive_missing_members,
                   OPT_no_warn_thin_archive_missing_members, true);
  ctx.arg.warnMissingSubsectionsViaSymbols =
      args.hasFlag(OPT_warn_missing_subsections_via_symbols,
                   OPT_no_warn_missing_subsections_via_symbols, false);
  ctx.arg.generateUuid = !args.hasArg(OPT_no_uuid);
  ctx.arg.disableVerify = args.hasArg(OPT_disable_verify);
  ctx.arg.separateCstringLiteralSections =
      args.hasFlag(OPT_separate_cstring_literal_sections,
                   OPT_no_separate_cstring_literal_sections, false);
  ctx.arg.tailMergeStrings =
      args.hasFlag(OPT_tail_merge_strings, OPT_no_tail_merge_strings, false);
  if (auto *arg = args.getLastArg(OPT_slop_scale_eq)) {
    StringRef v(arg->getValue());
    unsigned slop = 0;
    if (!llvm::to_integer(v, slop))
      ctx.e.error(arg->getSpelling() +
                  ": expected a non-negative integer, but got '" + v + "'");
    ctx.arg.slopScale = slop;
  }

  auto IncompatWithCGSort = [&](StringRef firstArgStr) {
    // Throw an error only if --call-graph-profile-sort is explicitly specified
    if (ctx.arg.callGraphProfileSort)
      if (const Arg *arg = args.getLastArgNoClaim(OPT_call_graph_profile_sort))
        ctx.e.error(firstArgStr + " is incompatible with " +
                    arg->getSpelling());
  };
  if (args.hasArg(OPT_irpgo_profile_sort) ||
      args.hasArg(OPT_irpgo_profile_sort_eq))
    ctx.e.warn("--irpgo-profile-sort is deprecated. Please use "
               "--bp-startup-sort=function");
  if (const Arg *arg = args.getLastArg(OPT_irpgo_profile))
    ctx.arg.irpgoProfilePath = arg->getValue();

  if (const Arg *arg = args.getLastArg(OPT_irpgo_profile_sort)) {
    ctx.arg.irpgoProfilePath = arg->getValue();
    ctx.arg.bpStartupFunctionSort = true;
    IncompatWithCGSort(arg->getSpelling());
  }
  ctx.arg.bpCompressionSortStartupFunctions =
      args.hasFlag(OPT_bp_compression_sort_startup_functions,
                   OPT_no_bp_compression_sort_startup_functions, false);
  if (const Arg *arg = args.getLastArg(OPT_bp_startup_sort)) {
    StringRef startupSortStr = arg->getValue();
    if (startupSortStr == "function") {
      ctx.arg.bpStartupFunctionSort = true;
    } else if (startupSortStr != "none") {
      ctx.e.error("unknown value `" + startupSortStr + "` for " +
                  arg->getSpelling());
    }
    if (startupSortStr != "none")
      IncompatWithCGSort(arg->getSpelling());
  }
  if (!ctx.arg.bpStartupFunctionSort &&
      ctx.arg.bpCompressionSortStartupFunctions)
    ctx.e.error("--bp-compression-sort-startup-functions must be used with "
                "--bp-startup-sort=function");
  if (ctx.arg.irpgoProfilePath.empty() && ctx.arg.bpStartupFunctionSort)
    ctx.e.error("--bp-startup-sort=function must be used with "
                "--irpgo-profile");
  auto addCompressionSortSpec = [&](StringRef value) {
    SmallVector<StringRef, 3> parts;
    value.split(parts, '=');

    StringRef globString = parts[0];
    unsigned layoutPriority = 0;
    std::optional<unsigned> matchPriority;

    if (parts.size() > 1 && !parts[1].empty()) {
      if (!to_integer(parts[1], layoutPriority)) {
        ctx.e.error("--bp-compression-sort-section: expected integer "
                    "for layout_priority, got '" +
                    parts[1] + "'");
        return;
      }
    }
    if (parts.size() > 2 && !parts[2].empty()) {
      unsigned mp;
      if (!to_integer(parts[2], mp)) {
        ctx.e.error("--bp-compression-sort-section: expected integer "
                    "for match_priority, got '" +
                    parts[2] + "'");
        return;
      }
      matchPriority = mp;
    }
    if (parts.size() > 3) {
      ctx.e.error("--bp-compression-sort-section: too many '=' in '" + value +
                  "'");
      return;
    }

    auto spec = BPCompressionSortSpec::create(globString, layoutPriority,
                                              matchPriority);
    if (!spec) {
      ctx.e.error("--bp-compression-sort-section: " +
                  toString(spec.takeError()));
      return;
    }
    ctx.arg.bpCompressionSortSpecs.emplace_back(std::move(*spec));
  };

  for (const Arg *arg : args.filtered(OPT_bp_compression_sort_section))
    addCompressionSortSpec(arg->getValue());
  if (!ctx.arg.bpCompressionSortSpecs.empty())
    IncompatWithCGSort("--bp-compression-sort-section");
  if (const Arg *arg = args.getLastArg(OPT_bp_compression_sort)) {
    StringRef compressionSortStr = arg->getValue();
    if (compressionSortStr == "function") {
      ctx.arg.bpFunctionOrderForCompression = true;
    } else if (compressionSortStr == "data") {
      ctx.arg.bpDataOrderForCompression = true;
    } else if (compressionSortStr == "both") {
      ctx.arg.bpFunctionOrderForCompression = true;
      ctx.arg.bpDataOrderForCompression = true;
    } else if (compressionSortStr != "none") {
      ctx.e.error("unknown value `" + compressionSortStr + "` for " +
                  arg->getSpelling());
    }
    if (compressionSortStr != "none")
      IncompatWithCGSort(arg->getSpelling());
  }
  ctx.arg.bpVerboseSectionOrderer = args.hasArg(OPT_verbose_bp_section_orderer);

  for (const Arg *arg : args.filtered(OPT_alias)) {
    ctx.arg.aliasedSymbols.push_back(
        std::make_pair(arg->getValue(0), arg->getValue(1)));
  }

  if (const char *zero = getenv("ZERO_AR_DATE"))
    ctx.arg.zeroModTime = strcmp(zero, "0") != 0;
  if (args.getLastArg(OPT_reproducible))
    ctx.arg.zeroModTime = true;

  std::array<PlatformType, 4> encryptablePlatforms{
      PLATFORM_IOS, PLATFORM_WATCHOS, PLATFORM_TVOS, PLATFORM_XROS};
  ctx.arg.emitEncryptionInfo =
      args.hasFlag(OPT_encryptable, OPT_no_encryption,
                   is_contained(encryptablePlatforms, ctx.arg.platform()));

  if (const Arg *arg = args.getLastArg(OPT_install_name)) {
    if (ctx.arg.warnDylibInstallName && ctx.arg.outputType != MH_DYLIB)
      ctx.e.warn(
          arg->getAsString(args) +
          ": ignored, only has effect with -dylib [--warn-dylib-install-name]");
    else
      ctx.arg.installName = arg->getValue();
  } else if (ctx.arg.outputType == MH_DYLIB) {
    ctx.arg.installName = ctx.arg.finalOutput;
  }

  auto getClientName = [&]() {
    StringRef cn = path::filename(ctx.arg.finalOutput);
    cn.consume_front("lib");
    auto firstDotOrUnderscore = cn.find_first_of("._");
    cn = cn.take_front(firstDotOrUnderscore);
    return cn;
  };
  ctx.arg.clientName = args.getLastArgValue(OPT_client_name, getClientName());

  if (args.hasArg(OPT_mark_dead_strippable_dylib)) {
    if (ctx.arg.outputType != MH_DYLIB)
      ctx.e.warn(
          "-mark_dead_strippable_dylib: ignored, only has effect with -dylib");
    else
      ctx.arg.markDeadStrippableDylib = true;
  }

  if (const Arg *arg = args.getLastArg(OPT_static, OPT_dynamic))
    ctx.arg.staticLink = (arg->getOption().getID() == OPT_static);

  if (const Arg *arg =
          args.getLastArg(OPT_flat_namespace, OPT_twolevel_namespace))
    ctx.arg.namespaceKind = arg->getOption().getID() == OPT_twolevel_namespace
                                ? NamespaceKind::twolevel
                                : NamespaceKind::flat;

  ctx.arg.undefinedSymbolTreatment = getUndefinedSymbolTreatment(ctx, args);

  if (ctx.arg.outputType == MH_EXECUTE)
    ctx.arg.entry =
        ctx.symtab->addUndefined(args.getLastArgValue(OPT_e, "_main"),
                                 /*file=*/nullptr,
                                 /*isWeakRef=*/false);

  ctx.arg.librarySearchPaths =
      getLibrarySearchPaths(ctx, args, ctx.arg.systemLibraryRoots);
  ctx.arg.frameworkSearchPaths =
      getFrameworkSearchPaths(ctx, args, ctx.arg.systemLibraryRoots);
  if (const Arg *arg =
          args.getLastArg(OPT_search_paths_first, OPT_search_dylibs_first))
    ctx.arg.searchDylibsFirst =
        arg->getOption().getID() == OPT_search_dylibs_first;

  ctx.arg.dylibCompatibilityVersion =
      parseDylibVersion(ctx, args, OPT_compatibility_version);
  ctx.arg.dylibCurrentVersion =
      parseDylibVersion(ctx, args, OPT_current_version);

  ctx.arg.dataConst = args.hasFlag(OPT_data_const, OPT_no_data_const,
                                   dataConstDefault(ctx, args));
  // Populate ctx.arg.sectionRenameMap with builtin default renames.
  // Options -rename_section and -rename_segment are able to override.
  initializeSectionRenameMap(ctx);
  // Reject every special character except '.' and '$'
  // TODO(gkm): verify that this is the proper set of invalid chars
  StringRef invalidNameChars("!\"#%&'()*+,-/:;<=>?@[\\]^`{|}~");
  auto validName = [&ctx, invalidNameChars](StringRef s) {
    if (s.find_first_of(invalidNameChars) != StringRef::npos)
      ctx.e.error("invalid name for segment or section: " + s);
    return s;
  };
  for (const Arg *arg : args.filtered(OPT_rename_section)) {
    ctx.arg.sectionRenameMap[{validName(arg->getValue(0)),
                              validName(arg->getValue(1))}] = {
        validName(arg->getValue(2)), validName(arg->getValue(3))};
  }
  for (const Arg *arg : args.filtered(OPT_rename_segment)) {
    ctx.arg.segmentRenameMap[validName(arg->getValue(0))] =
        validName(arg->getValue(1));
  }

  ctx.arg.sectionAlignments = parseSectAlign(ctx, args);

  for (const Arg *arg : args.filtered(OPT_segprot)) {
    StringRef segName = arg->getValue(0);
    uint32_t maxProt = parseProtection(ctx, arg->getValue(1));
    uint32_t initProt = parseProtection(ctx, arg->getValue(2));

    // FIXME: Check if this works on more platforms.
    bool allowsDifferentInitAndMaxProt =
        ctx.arg.platform() == PLATFORM_MACOS ||
        ctx.arg.platform() == PLATFORM_MACCATALYST;
    if (allowsDifferentInitAndMaxProt) {
      if (initProt > maxProt)
        ctx.e.error("invalid argument '" + arg->getAsString(args) +
                    "': init must not be more permissive than max");
    } else {
      if (maxProt != initProt && ctx.arg.arch() != AK_i386)
        ctx.e.error(
            "invalid argument '" + arg->getAsString(args) +
            "': max and init must be the same for non-macOS non-i386 archs");
    }

    if (segName == segment_names::linkEdit)
      ctx.e.error("-segprot cannot be used to change __LINKEDIT's protections");
    ctx.arg.segmentProtections.push_back({segName, maxProt, initProt});
  }

  ctx.arg.hasExplicitExports =
      args.hasArg(OPT_no_exported_symbols) ||
      args.hasArgNoClaim(OPT_exported_symbol, OPT_exported_symbols_list);
  handleSymbolPatterns(ctx, args, ctx.arg.exportedSymbols, OPT_exported_symbol,
                       OPT_exported_symbols_list);
  handleSymbolPatterns(ctx, args, ctx.arg.unexportedSymbols,
                       OPT_unexported_symbol, OPT_unexported_symbols_list);
  if (ctx.arg.hasExplicitExports && !ctx.arg.unexportedSymbols.empty())
    ctx.e.error(
        "cannot use both -exported_symbol* and -unexported_symbol* options");

  if (args.hasArg(OPT_no_exported_symbols) && !ctx.arg.exportedSymbols.empty())
    ctx.e.error(
        "cannot use both -exported_symbol* and -no_exported_symbols options");

  // Imitating LD64's:
  // -non_global_symbols_no_strip_list and -non_global_symbols_strip_list can't
  // both be present.
  // But -x can be used with either of these two, in which case, the last arg
  // takes effect.
  // (TODO: This is kind of confusing - considering disallowing using them
  // together for a more straightforward behaviour)
  {
    bool includeLocal = false;
    bool excludeLocal = false;
    for (const Arg *arg :
         args.filtered(OPT_x, OPT_non_global_symbols_no_strip_list,
                       OPT_non_global_symbols_strip_list)) {
      switch (arg->getOption().getID()) {
      case OPT_x:
        ctx.arg.localSymbolsPresence = SymtabPresence::None;
        break;
      case OPT_non_global_symbols_no_strip_list:
        if (excludeLocal) {
          ctx.e.error("cannot use both -non_global_symbols_no_strip_list and "
                      "-non_global_symbols_strip_list");
        } else {
          includeLocal = true;
          ctx.arg.localSymbolsPresence = SymtabPresence::SelectivelyIncluded;
          parseSymbolPatternsFile(ctx, arg, ctx.arg.localSymbolPatterns);
        }
        break;
      case OPT_non_global_symbols_strip_list:
        if (includeLocal) {
          ctx.e.error("cannot use both -non_global_symbols_no_strip_list and "
                      "-non_global_symbols_strip_list");
        } else {
          excludeLocal = true;
          ctx.arg.localSymbolsPresence = SymtabPresence::SelectivelyExcluded;
          parseSymbolPatternsFile(ctx, arg, ctx.arg.localSymbolPatterns);
        }
        break;
      default:
        llvm_unreachable("unexpected option");
      }
    }
  }
  // Explicitly-exported literal symbols must be defined, but might
  // languish in an archive if unreferenced elsewhere or if they are in the
  // non-global strip list. Light a fire under those lazy symbols!
  for (const CachedHashStringRef &cachedName : ctx.arg.exportedSymbols.literals)
    ctx.symtab->addUndefined(cachedName.val(), /*file=*/nullptr,
                             /*isWeakRef=*/false);

  for (const Arg *arg : args.filtered(OPT_why_live))
    ctx.arg.whyLive.insert(ctx, arg->getValue());
  if (!ctx.arg.whyLive.empty() && !ctx.arg.deadStrip)
    ctx.e.warn("-why_live has no effect without -dead_strip, ignoring");

  ctx.arg.saveTemps = args.hasArg(OPT_save_temps);

  ctx.arg.adhocCodesign = args.hasFlag(
      OPT_adhoc_codesign, OPT_no_adhoc_codesign,
      shouldAdhocSignByDefault(ctx.arg.arch(), ctx.arg.platform()));

  if (args.hasArg(OPT_v)) {
    ctx.e.message(getLLDVersion(), ctx.e.errs());
    ctx.e.message(StringRef("Library search paths:") +
                      (ctx.arg.librarySearchPaths.empty()
                           ? ""
                           : "\n\t" + join(ctx.arg.librarySearchPaths, "\n\t")),
                  ctx.e.errs());
    ctx.e.message(
        StringRef("Framework search paths:") +
            (ctx.arg.frameworkSearchPaths.empty()
                 ? ""
                 : "\n\t" + join(ctx.arg.frameworkSearchPaths, "\n\t")),
        ctx.e.errs());
  }

  ctx.arg.progName = argsArr[0];

  ctx.arg.timeTraceEnabled = args.hasArg(OPT_time_trace_eq);
  ctx.arg.timeTraceGranularity =
      args::getInteger(ctx.e, args, OPT_time_trace_granularity_eq, 500);

  // Initialize time trace profiler.
  if (ctx.arg.timeTraceEnabled)
    timeTraceProfilerInitialize(ctx.arg.timeTraceGranularity, ctx.arg.progName);

  {
    TimeTraceScope timeScope("ExecuteLinker");

    initLLVM(); // must be run before any call to addFile()
    createFiles(ctx, args);

    // Now that all dylibs have been loaded, search for those that should be
    // re-exported.
    {
      auto reexportHandler = [&ctx](const Arg *arg,
                                    const std::vector<StringRef> &extensions) {
        ctx.arg.hasReexports = true;
        StringRef searchName = arg->getValue();
        if (!markReexport(ctx, searchName, extensions))
          ctx.e.error(arg->getSpelling() + " " + searchName +
                      " does not match a supplied dylib");
      };
      std::vector<StringRef> extensions = {".tbd"};
      for (const Arg *arg : args.filtered(OPT_sub_umbrella))
        reexportHandler(arg, extensions);

      extensions.push_back(".dylib");
      for (const Arg *arg : args.filtered(OPT_sub_library))
        reexportHandler(arg, extensions);
    }

    cl::ResetAllOptionOccurrences();

    // Parse LTO options.
    if (const Arg *arg = args.getLastArg(OPT_mcpu))
      parseClangOption(ctx,
                       ctx.saver.save("-mcpu=" + StringRef(arg->getValue())),
                       arg->getSpelling());

    for (const Arg *arg : args.filtered(OPT_mllvm)) {
      parseClangOption(ctx, arg->getValue(), arg->getSpelling());
      ctx.arg.mllvmOpts.emplace_back(arg->getValue());
    }

    ctx.arg.passPlugins = args::getStrings(args, OPT_load_pass_plugins);

    createSyntheticSections(ctx);
    createSyntheticSymbols(ctx);
    addSynthenticMethnames(ctx);

    createAliases(ctx);
    // If we are in "explicit exports" mode, hide everything that isn't
    // explicitly exported. Do this before running LTO so that LTO can better
    // optimize.
    handleExplicitExports(ctx);

    bool didCompileBitcodeFiles = compileBitcodeFiles(ctx);

    resolveLCLinkerOptions(ctx);

    // If either --thinlto-index-only or --lto-emit-llvm is given, we should
    // not create object files. Index file creation is already done in
    // compileBitcodeFiles, so we are done if that's the case.
    if (ctx.arg.thinLTOIndexOnly || ctx.arg.emitLLVM)
      return ctx.e.errorCount == 0;

    // LTO may emit a non-hidden (extern) object file symbol even if the
    // corresponding bitcode symbol is hidden. In particular, this happens for
    // cross-module references to hidden symbols under ThinLTO. Thus, if we
    // compiled any bitcode files, we must redo the symbol hiding.
    if (didCompileBitcodeFiles)
      handleExplicitExports(ctx);
    replaceCommonSymbols(ctx);

    StringRef orderFile = args.getLastArgValue(OPT_order_file);
    if (!orderFile.empty())
      ctx.priorityBuilder->parseOrderFile(orderFile);

    referenceStubBinder(ctx);

    // FIXME: should terminate the link early based on errors encountered so
    // far?

    for (const Arg *arg : args.filtered(OPT_sectcreate)) {
      StringRef segName = arg->getValue(0);
      StringRef sectName = arg->getValue(1);
      StringRef fileName = arg->getValue(2);
      std::optional<MemoryBufferRef> buffer = readFile(ctx, fileName);
      if (buffer)
        ctx.inputFiles.insert(
            ctx.make<OpaqueFile>(ctx, *buffer, segName, sectName));
    }

    for (const Arg *arg : args.filtered(OPT_add_empty_section)) {
      StringRef segName = arg->getValue(0);
      StringRef sectName = arg->getValue(1);
      ctx.inputFiles.insert(
          ctx.make<OpaqueFile>(ctx, MemoryBufferRef(), segName, sectName));
    }

    gatherInputSections(ctx);

    if (!ctx.arg.codegenDataGeneratePath.empty())
      codegenDataGenerate(ctx);

    if (ctx.arg.callGraphProfileSort)
      ctx.priorityBuilder->extractCallGraphProfile();

    if (ctx.arg.deadStrip)
      markLive(ctx);

    // Ensure that no symbols point inside __mod_init_func sections if they are
    // removed due to -init_offsets. This must run after dead stripping.
    if (ctx.arg.emitInitOffsets)
      eraseInitializerSymbols(ctx);

    // Categories are not subject to dead-strip. The __objc_catlist section is
    // marked as NO_DEAD_STRIP and that propagates into all category data.
    if (args.hasArg(OPT_check_category_conflicts))
      objc::checkCategories(ctx);

    // Category merging uses "->live = false" to erase old category data, so
    // it has to run after dead-stripping (markLive).
    if (args.hasFlag(OPT_objc_category_merging, OPT_no_objc_category_merging,
                     false))
      objc::mergeCategories(ctx);

    computeColdness(ctx);

    // ICF assumes that all literals have been folded already, so we must run
    // foldIdenticalLiterals before foldIdenticalSections.
    foldIdenticalLiterals(ctx);
    if (ctx.arg.icfLevel != ICFLevel::none) {
      if (ctx.arg.icfLevel == ICFLevel::safe ||
          ctx.arg.icfLevel == ICFLevel::safe_thunks)
        markAddrSigSymbols(ctx);
      foldIdenticalSections(ctx, /*onlyCfStrings=*/false);
    } else if (ctx.arg.dedupStrings) {
      foldIdenticalSections(ctx, /*onlyCfStrings=*/true);
    }

    stripSwiftForceLoadFixups(ctx);

    // Write to an output file.
    if (ctx.target->wordSize == 8)
      writeResult<LP64>(ctx);
    else
      writeResult<ILP32>(ctx);

    ctx.depTracker->write(getLLDVersion(), ctx.inputFiles, ctx.arg.outputFile);
  }

  if (ctx.arg.timeTraceEnabled) {
    checkError(ctx.e, timeTraceProfilerWrite(
                          args.getLastArgValue(OPT_time_trace_eq).str(),
                          ctx.arg.outputFile));

    timeTraceProfilerCleanup();
  }

  if (ctx.e.errorCount != 0 || ctx.arg.strictAutoLink)
    for (const auto &warning : ctx.missingAutolinkWarnings)
      ctx.e.warn(warning);

  return ctx.e.errorCount == 0;
}

bool link(ArrayRef<const char *> argsArr, llvm::raw_ostream &stdoutOS,
          llvm::raw_ostream &stderrOS, bool exitEarly, bool disableOutput) {
  // If fatal() unwinds the link, the context is leaked on purpose: tasks that
  // the link spawned on the parallel executor may still be using it. lldMain()
  // then reports that lld cannot run again.
  auto context = std::make_unique<Ctx>();
  Ctx &ctx = *context;
  ctx.e.initialize(stdoutOS, stderrOS, exitEarly, disableOutput);

  bool ret = linkImpl(ctx, argsArr);
  // Exit before the context is destroyed, so that its destructors are skipped.
  if (exitEarly)
    exitLld(ctx.e, !ret);
  return ret;
}
} // namespace macho
} // namespace lld
