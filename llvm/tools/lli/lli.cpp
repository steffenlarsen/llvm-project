//===- lli.cpp - LLVM Interpreter / Dynamic compiler ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This utility provides a simple wrapper around the LLVM Execution Engines,
// which allow the direct execution of LLVM programs through a Just-In-Time
// compiler, or through an interpreter if no JIT is available for this platform.
//
//===----------------------------------------------------------------------===//

#include "ForwardingMemoryManager.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/CodeGen/CommandFlagsOptInfos.h"
#include "llvm/CodeGen/LinkAllCodegenComponents.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ExecutionEngine/Interpreter.h"
#include "llvm/ExecutionEngine/JITEventListener.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/ExecutionEngine/ObjectCache.h"
#include "llvm/ExecutionEngine/Orc/AbsoluteSymbols.h"
#include "llvm/ExecutionEngine/Orc/DebugUtils.h"
#include "llvm/ExecutionEngine/Orc/Debugging/DebuggerSupport.h"
#include "llvm/ExecutionEngine/Orc/EPCDynamicLibrarySearchGenerator.h"
#include "llvm/ExecutionEngine/Orc/EPCGenericRTDyldMemoryManager.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/IRPartitionLayer.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ObjectTransformLayer.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/Orc/SelfExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/SimpleRemoteEPC.h"
#include "llvm/ExecutionEngine/Orc/SymbolStringPool.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/JITLoaderGDB.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/RegisterEHFrames.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/TargetExecutionUtils.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/MC/MCTargetOptionsCommandFlags.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Memory.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <cerrno>
#include <optional>

#if !defined(_MSC_VER) && !defined(__MINGW32__)
#include <unistd.h>
#else
#include <io.h>
#endif

#ifdef __CYGWIN__
#include <cygwin/version.h>
#if defined(CYGWIN_VERSION_DLL_MAJOR) && CYGWIN_VERSION_DLL_MAJOR < 1007
#define DO_NOTHING_ATEXIT 1
#endif
#endif

using namespace llvm;
using namespace llvm::clv2;

#define DEBUG_TYPE "lli"

namespace {
enum class JITKind { MCJIT, Orc, OrcLazy };
enum class JITLinkerKind { Default, RuntimeDyld, JITLink };
enum class LLJITPlatform { Inactive, Auto, ExecutorNative, GenericIR };
enum class DumpKind {
  NoDump,
  DumpFuncsToStdOut,
  DumpModsToStdOut,
  DumpModsToDisk,
  DumpDebugDescriptor,
  DumpDebugObjects,
};
} // namespace

//===----------------------------------------------------------------------===//
// lli option declarations (clv2)
//===----------------------------------------------------------------------===//

inline constexpr OptionInfo<std::string> LLI_InputFile{"", "<input bitcode>",
                                                       Positional{}, Init{"-"}};

inline constexpr ListOptionInfo<std::string> LLI_InputArgv{
    "", "<program arguments>...", Positional{}, ConsumeAfter};

inline constexpr OptionInfo<bool> LLI_ForceInterpreter{
    "force-interpreter", "Force interpretation: disable JIT", Init{false}};

inline constexpr EnumVal<JITKind> LLI_JITKindVals[] = {
    {"mcjit", JITKind::MCJIT, "MCJIT"},
    {"orc", JITKind::Orc, "Orc JIT"},
    {"orc-lazy", JITKind::OrcLazy, "Orc-based lazy JIT."},
};
inline constexpr auto LLI_UseJITKind =
    makeEnumOption<JITKind>("jit-kind", "Choose underlying JIT kind.",
                            LLI_JITKindVals, Init{JITKind::Orc});

inline constexpr EnumVal<JITLinkerKind> LLI_JITLinkerVals[] = {
    {"default", JITLinkerKind::Default, "Default for platform and JIT-kind"},
    {"rtdyld", JITLinkerKind::RuntimeDyld, "RuntimeDyld"},
    {"jitlink", JITLinkerKind::JITLink, "Orc-specific linker"},
};
inline constexpr auto LLI_JITLinker = makeEnumOption<JITLinkerKind>(
    "jit-linker", "Choose the dynamic linker/loader.", LLI_JITLinkerVals,
    Init{JITLinkerKind::Default});

inline constexpr OptionInfo<std::string> LLI_OrcRuntime{
    "orc-runtime", "Use ORC runtime from given path", Init{""}};

inline constexpr OptionInfo<unsigned> LLI_LazyJITCompileThreads{
    "compile-threads",
    "Choose the number of compile threads (jit-kind=orc-lazy only)", Init{0u}};

inline constexpr ListOptionInfo<std::string> LLI_ThreadEntryPoints{
    "thread-entry",
    "calls the given entry-point on a new thread (jit-kind=orc-lazy only)"};

inline constexpr OptionInfo<bool> LLI_PerModuleLazy{
    "per-module-lazy",
    "Performs lazy compilation on whole module boundaries "
    "rather than individual functions",
    Init{false}};

inline constexpr ListOptionInfo<std::string> LLI_JITDylibs{
    "jd", "Specifies the JITDylib to be used for any subsequent "
          "-extra-module arguments."};

inline constexpr ListOptionInfo<std::string> LLI_Dylibs{
    "dlopen", "Dynamic libraries to load before linking"};

inline constexpr OptionInfo<bool> LLI_RemoteMCJIT{
    "remote-mcjit", "Execute MCJIT'ed code in a separate process.",
    Init{false}};

inline constexpr OptionInfo<std::string> LLI_ChildExecPath{
    "mcjit-remote-process",
    "Specify the filename of the process to launch "
    "for remote MCJIT execution.  If none is specified,"
    "\n\tremote execution will be simulated in-process.",
    value_desc("filename"), Init{""}};

inline constexpr OptionInfo<std::string> LLI_OptLevel{
    "O", "Optimization level. [-O0, -O1, -O2, or -O3] (default = '-O2')",
    PrefixFormat, Init{"2"}, value_desc("char")};

inline constexpr OptionInfo<std::string> LLI_TargetTriple{
    "mtriple", "Override target triple for module"};

inline constexpr OptionInfo<std::string> LLI_EntryFunc{
    "entry-function",
    "Specify the entry function (default = 'main') of the executable",
    value_desc("function"), Init{"main"}};

inline constexpr ListOptionInfo<std::string> LLI_ExtraModules{
    "extra-module", "Extra modules to be loaded", value_desc("input bitcode")};

inline constexpr ListOptionInfo<std::string> LLI_ExtraObjects{
    "extra-object", "Extra object files to be loaded",
    value_desc("input object")};

inline constexpr ListOptionInfo<std::string> LLI_ExtraArchives{
    "extra-archive", "Extra archive files to be loaded",
    value_desc("input archive")};

inline constexpr OptionInfo<bool> LLI_EnableCacheManager{
    "enable-cache-manager", "Use cache manager to save/load modules",
    Init{false}};

inline constexpr OptionInfo<std::string> LLI_ObjectCacheDir{
    "object-cache-dir",
    "Directory to store cached object files (must be user writable)", Init{""}};

inline constexpr OptionInfo<std::string> LLI_FakeArgv0{
    "fake-argv0",
    "Override the 'argv[0]' value passed into the executing program",
    value_desc("executable")};

inline constexpr OptionInfo<bool> LLI_DisableCoreFiles{
    "disable-core-files", "Disable emission of core files if possible", Hidden};

inline constexpr OptionInfo<bool> LLI_NoLazyCompilation{
    "disable-lazy-compilation", "Disable JIT lazy compilation", Init{false}};

inline constexpr OptionInfo<bool> LLI_GenerateSoftFloatCalls{
    "soft-float", "Generate software floating point library calls",
    Init{false}};

inline constexpr OptionInfo<bool> LLI_NoProcessSymbols{
    "no-process-syms", "Do not resolve lli process symbols in JIT'd code"};

inline constexpr EnumVal<LLJITPlatform> LLI_PlatformVals[] = {
    {"Auto", LLJITPlatform::Auto,
     "Like 'ExecutorNative' if ORC runtime provided, "
     "otherwise like 'GenericIR'"},
    {"ExecutorNative", LLJITPlatform::ExecutorNative,
     "Use the native platform for the executor. Requires -orc-runtime"},
    {"GenericIR", LLJITPlatform::GenericIR, "Use LLJITGenericIRPlatform"},
    {"Inactive", LLJITPlatform::Inactive,
     "Disable platform support explicitly"},
};
inline constexpr auto LLI_Platform = makeEnumOption<LLJITPlatform>(
    "lljit-platform", "Platform to use with LLJIT", LLI_PlatformVals, Hidden,
    Init{LLJITPlatform::Auto});

inline constexpr EnumVal<DumpKind> LLI_OrcDumpKindVals[] = {
    {"no-dump", DumpKind::NoDump, "Don't dump anything."},
    {"funcs-to-stdout", DumpKind::DumpFuncsToStdOut,
     "Dump function names to stdout."},
    {"mods-to-stdout", DumpKind::DumpModsToStdOut, "Dump modules to stdout."},
    {"mods-to-disk", DumpKind::DumpModsToDisk,
     "Dump modules to the current working directory. (WARNING: will overwrite "
     "existing files)."},
    {"jit-debug-descriptor", DumpKind::DumpDebugDescriptor,
     "Dump __jit_debug_descriptor contents to stdout"},
    {"jit-debug-objects", DumpKind::DumpDebugObjects,
     "Dump __jit_debug_descriptor in-memory debug objects as tool output"},
};
inline constexpr auto LLI_OrcDumpKind = makeEnumOption<DumpKind>(
    "orc-lazy-debug", "Debug dumping for the orc-lazy JIT.",
    LLI_OrcDumpKindVals, Hidden, Init{DumpKind::NoDump});

//===----------------------------------------------------------------------===//
// Registry
//===----------------------------------------------------------------------===//

inline constexpr OptionsRegistry<
    &LLI_InputFile, &LLI_InputArgv, &LLI_ForceInterpreter, &LLI_UseJITKind,
    &LLI_JITLinker, &LLI_OrcRuntime, &LLI_LazyJITCompileThreads,
    &LLI_ThreadEntryPoints, &LLI_PerModuleLazy, &LLI_JITDylibs, &LLI_Dylibs,
    &LLI_RemoteMCJIT, &LLI_ChildExecPath, &LLI_OptLevel, &LLI_TargetTriple,
    &LLI_EntryFunc, &LLI_ExtraModules, &LLI_ExtraObjects, &LLI_ExtraArchives,
    &LLI_EnableCacheManager, &LLI_ObjectCacheDir, &LLI_FakeArgv0,
    &LLI_DisableCoreFiles, &LLI_NoLazyCompilation, &LLI_GenerateSoftFloatCalls,
    &LLI_NoProcessSymbols, &LLI_Platform, &LLI_OrcDumpKind,
    // CodeGen flags (forwarded to CGParsedValues bridge)
    &CG_MArch, &CG_MCPU, &CG_MTune, &CG_MAttrs, &CG_RelocModel, &CG_ThreadModel,
    &CG_CodeModel, &CG_LargeDataThreshold, &CG_ExceptionModel, &CG_FileType,
    &CG_FramePointer, &CG_EnableNoTrappingFPMath,
    &CG_EnableAIXExtendedAltivecABI, &CG_DenormalFPMath, &CG_DenormalFP32Math,
    &CG_EnableHonorSignDependentRoundingFPMath, &CG_FloatABIForCalls,
    &CG_FuseFPOps, &CG_SwiftAsyncFramePointer, &CG_DontPlaceZerosInBSS,
    &CG_EnableGuaranteedTailCallOpt, &CG_DisableTailCalls,
    &CG_StackSymbolOrdering, &CG_StackRealign, &CG_TrapFuncName, &CG_UseCtors,
    &CG_DisableIntegratedAS, &CG_DataSections, &CG_FunctionSections,
    &CG_IgnoreXCOFFVisibility, &CG_XCOFFTracebackTable, &CG_EnableBBAddrMap,
    &CG_BBSections, &CG_TLSSize, &CG_EmulatedTLS, &CG_EnableTLSDESC,
    &CG_UniqueSectionNames, &CG_UniqueBasicBlockSectionNames,
    &CG_SeparateNamedSections, &CG_EABIVersion, &CG_DebuggerTuning,
    &CG_VectorLibrary, &CG_EnableStackSizeSection, &CG_EnableAddrsig,
    &CG_EnableCallGraphSection, &CG_EmitCallSiteInfo,
    &CG_EnableMachineFunctionSplitter, &CG_EnableStaticDataPartitioning,
    &CG_EnableDebugEntryValues, &CG_ForceDwarfFrameSection,
    &CG_XRayFunctionIndex, &CG_DebugStrictDwarf, &CG_AlignLoops,
    &CG_JMCInstrument, &CG_XCOFFReadOnlyPointers, &CG_SaveStats>
    LLIToolReg;

using LLIOpts = decltype(LLIToolReg)::ParsedOptionsT;

static ExitOnError ExitOnErr;

LLVM_ATTRIBUTE_USED static void linkComponents() {
  errs() << (void *)&llvm_orc_registerEHFrameSectionAllocAction
         << (void *)&llvm_orc_deregisterEHFrameSectionAllocAction
         << (void *)&llvm_orc_registerJITLoaderGDBAllocAction;
}

namespace {
//===----------------------------------------------------------------------===//
// Object cache
//
// This object cache implementation writes cached objects to disk to the
// directory specified by CacheDir, using a filename provided in the module
// descriptor. The cache tries to load a saved object using that path if the
// file exists. CacheDir defaults to "", in which case objects are cached
// alongside their originating bitcodes.
//
class LLIObjectCache : public ObjectCache {
public:
  LLIObjectCache(const std::string &CacheDir) : CacheDir(CacheDir) {
    // Add trailing '/' to cache dir if necessary.
    if (!this->CacheDir.empty() &&
        this->CacheDir[this->CacheDir.size() - 1] != '/')
      this->CacheDir += '/';
  }
  ~LLIObjectCache() override = default;

  void notifyObjectCompiled(const Module *M, MemoryBufferRef Obj) override {
    const std::string &ModuleID = M->getModuleIdentifier();
    std::string CacheName;
    if (!getCacheFilename(ModuleID, CacheName))
      return;
    if (!CacheDir.empty()) { // Create user-defined cache dir.
      SmallString<128> dir(sys::path::parent_path(CacheName));
      sys::fs::create_directories(Twine(dir));
    }

    std::error_code EC;
    raw_fd_ostream outfile(CacheName, EC, sys::fs::OF_None);
    outfile.write(Obj.getBufferStart(), Obj.getBufferSize());
    outfile.close();
  }

  std::unique_ptr<MemoryBuffer> getObject(const Module *M) override {
    const std::string &ModuleID = M->getModuleIdentifier();
    std::string CacheName;
    if (!getCacheFilename(ModuleID, CacheName))
      return nullptr;
    // Load the object from the cache filename
    ErrorOr<std::unique_ptr<MemoryBuffer>> IRObjectBuffer =
        MemoryBuffer::getFile(CacheName, /*IsText=*/false,
                              /*RequiresNullTerminator=*/false);
    // If the file isn't there, that's OK.
    if (!IRObjectBuffer)
      return nullptr;
    // MCJIT will want to write into this buffer, and we don't want that
    // because the file has probably just been mmapped.  Instead we make
    // a copy.  The filed-based buffer will be released when it goes
    // out of scope.
    return MemoryBuffer::getMemBufferCopy(IRObjectBuffer.get()->getBuffer());
  }

private:
  std::string CacheDir;

  bool getCacheFilename(StringRef ModID, std::string &CacheName) {
    if (!ModID.consume_front("file:"))
      return false;

    std::string CacheSubdir = std::string(ModID);
    // Transform "X:\foo" => "/X\foo" for convenience on Windows.
    if (is_style_windows(llvm::sys::path::Style::native) &&
        isalpha(CacheSubdir[0]) && CacheSubdir[1] == ':') {
      CacheSubdir[1] = CacheSubdir[0];
      CacheSubdir[0] = '/';
    }

    CacheName = CacheDir + CacheSubdir;
    size_t pos = CacheName.rfind('.');
    CacheName.replace(pos, CacheName.length() - pos, ".o");
    return true;
  }
};
} // namespace

// On Mingw and Cygwin, an external symbol named '__main' is called from the
// generated 'main' function to allow static initialization.  To avoid linking
// problems with remote targets (because lli's remote target support does not
// currently handle external linking) we add a secondary module which defines
// an empty '__main' function.
static void addCygMingExtraModule(ExecutionEngine &EE, LLVMContext &Context,
                                  const Triple &TargetTriple) {
  IRBuilder<> Builder(Context);

  // Create a new module.
  std::unique_ptr<Module> M =
      std::make_unique<Module>("CygMingHelper", Context);
  M->setTargetTriple(TargetTriple);

  // Create an empty function named "__main".
  Type *ReturnTy;
  if (TargetTriple.isArch64Bit())
    ReturnTy = Type::getInt64Ty(Context);
  else
    ReturnTy = Type::getInt32Ty(Context);
  Function *Result =
      Function::Create(FunctionType::get(ReturnTy, {}, false),
                       GlobalValue::ExternalLinkage, "__main", M.get());

  BasicBlock *BB = BasicBlock::Create(Context, "__main", Result);
  Builder.SetInsertPoint(BB);
  Value *ReturnVal = ConstantInt::get(ReturnTy, 0);
  Builder.CreateRet(ReturnVal);

  // Add this new module to the ExecutionEngine.
  EE.addModule(std::move(M));
}

static CodeGenOptLevel getOptLevel(const LLIOpts &Opts) {
  std::string OStr = Opts.get<&LLI_OptLevel>();
  char Level = OStr.empty() ? '2' : OStr[0];
  if (auto L = CodeGenOpt::parseLevel(Level))
    return *L;
  WithColor::error(errs(), "lli") << "invalid optimization level.\n";
  exit(1);
}

[[noreturn]] static void reportError(SMDiagnostic Err, const char *ProgName) {
  Err.print(ProgName, errs());
  exit(1);
}

static CodeGenOptLevel getOptLevel(const LLIOpts &Opts);
static Error loadDylibs(const LLIOpts &Opts);
static int runOrcJIT(const char *ProgName, const LLIOpts &Opts,
                     const clv2::OptionsContext &OptsCtx);
static void disallowOrcOptions(const LLIOpts &Opts);
static Expected<std::unique_ptr<orc::ExecutorProcessControl>>
launchRemote(const LLIOpts &Opts);

//===----------------------------------------------------------------------===//
// main Driver function
//
int main(int argc, char **argv, char *const *envp) {
  InitLLVM X(argc, argv);

  if (argc > 1)
    ExitOnErr.setBanner(std::string(argv[0]) + ": ");

  // If we have a native target, initialize it to ensure it is linked in and
  // usable by the JIT.
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();

  clv2::OptionParser P;
  P.add<&LLIToolReg>();
  RegisterCommonLLVMOptionsHidden(P);
  // lli showOptions (147 options)
  P.showOptions({
      "O",
      "abort-on-max-devirt-iterations-reached",
      "addrsig",
      "align-loops",
      "allow-ginsert-as-artifact",
      "arc-contract-use-objc-claim-rv",
      "asm-show-inst",
      "basic-block-address-map",
      "basic-block-section-match-infer",
      "basic-block-sections",
      "call-graph-section",
      "cfg-hide-cold-paths",
      "cfg-hide-deoptimize-paths",
      "cfg-hide-unreachable-paths",
      "code-model",
      "compile-threads",
      "crel",
      "ctx-profile-force-is-specialized",
      "data-sections",
      "debug-entry-values",
      "debugger-tune",
      "debugify-atoms",
      "debugify-func-limit",
      "debugify-level",
      "debugify-quiet",
      "denormal-fp-math",
      "denormal-fp-math-f32",
      "disable-auto-upgrade-debug-info",
      "disable-i2p-p2i-opt",
      "disable-lazy-compilation",
      "disable-tail-calls",
      "dlopen",
      "dot-cfg-mssa",
      "dwarf64",
      "dwarf-version",
      "elide-all-zero-branch-weights",
      "emit-bb-hash",
      "emit-call-site-info",
      "emit-compact-unwind-non-canonical",
      "emit-dwarf-unwind",
      "emulated-tls",
      "enable-cache-manager",
      "enable-cse-in-irtranslator",
      "enable-cse-in-legalizer",
      "enable-jmc-instrument",
      "enable-name-compression",
      "enable-no-signed-zeros-fp-math",
      "enable-no-trapping-fp-math",
      "enable-split-loopiv-heuristic",
      "enable-tlsdesc",
      "enable-vtable-profile-use",
      "enable-vtable-value-profiling",
      "entry-function",
      "exception-model",
      "experimental-debug-variable-locations",
      "extra-archive",
      "extra-module",
      "extra-object",
      "fake-argv0",
      "fatal-warnings",
      "fdpic",
      "filetype",
      "float-abi",
      "force-dwarf-frame-section",
      "force-interpreter",
      "fp-contract",
      "frame-pointer",
      "fs-profile-debug-bw-threshold",
      "fs-profile-debug-prob-diff-threshold",
      "function-sections",
      "generate-merged-base-profiles",
      "gsframe",
      "ignore-xcoff-visibility",
      "implicit-mapsyms",
      "incremental-linker-compatible",
      "ir2vec-arg-weight",
      "ir2vec-kind",
      "ir2vec-opc-weight",
      "ir2vec-type-weight",
      "ir2vec-vocab-path",
      "jd",
      "jit-kind",
      "jit-linker",
      "large-data-threshold",
      "march",
      "mattr",
      "mcjit-remote-process",
      "mcpu",
      "mc-relax-all",
      "meabi",
      "mir2vec-common-operand-weight",
      "mir2vec-kind",
      "mir2vec-opc-weight",
      "mir2vec-print-all-vocab-entries",
      "mir2vec-reg-operand-weight",
      "mir2vec-vocab-path",
      "mir-strip-debugify-only",
      "ms-secure-hotpatch-functions-file",
      "ms-secure-hotpatch-functions-list",
      "mtriple",
      "mxcoff-roptr",
      "no-deprecated-warn",
      "no-integrated-as",
      "no-process-syms",
      "no-type-check",
      "no-warn",
      "nozero-initialized-in-bss",
      "object-cache-dir",
      "object-size-offset-visitor-max-visit-instructions",
      "orc-runtime",
      "partition-static-data-sections",
      "per-module-lazy",
      "propeller-infer-threshold",
      "relocation-model",
      "reloc-section-sym",
      "remote-mcjit",
      "sample-profile-check-record-coverage",
      "sample-profile-check-sample-coverage",
      "sample-profile-max-propagate-iterations",
      "save-temp-labels",
      "separate-named-sections",
      "soft-float",
      "split-machine-functions",
      "stackrealign",
      "stack-size-section",
      "stack-symbol-ordering",
      "strict-dwarf",
      "swift-async-fp",
      "tailcallopt",
      "target-abi",
      "thread-entry",
      "thread-model",
      "tls-size",
      "unique-basic-block-section-names",
      "unique-section-names",
      "use-ctors",
      "vec-extabi",
      "verify-legalizer-debug-locs",
      "x86-align-branch",
      "x86-align-branch-boundary",
      "x86-branches-within-32B-boundaries",
      "x86-enable-apx-for-relocation",
      "x86-pad-max-prefix-size",
      "x86-relax-relocations",
      "x86-sse2avx",
      "xcoff-traceback-table",
      "xray-function-index",
  });

  auto OptsCtx = P.parse(argc, argv, "llvm interpreter & dynamic compiler\n");
  auto *Opts = OptsCtx->getViewPtr<&LLIToolReg>();

  setTPCValues(CGPassBuilderOption{});

  // If the user doesn't want core files, disable them.
  if (Opts->get<&LLI_DisableCoreFiles>())
    sys::Process::PreventCoreFiles();

  ExitOnErr(loadDylibs(*Opts));

  std::string EntryFunc = Opts->get<&LLI_EntryFunc>();
  if (EntryFunc.empty()) {
    WithColor::error(errs(), argv[0])
        << "--entry-function name cannot be empty\n";
    exit(1);
  }

  JITKind UseJITKind = Opts->get<&LLI_UseJITKind>();
  bool ForceInterpreter = Opts->get<&LLI_ForceInterpreter>();
  if (UseJITKind == JITKind::MCJIT || ForceInterpreter)
    disallowOrcOptions(*Opts);
  else
    return runOrcJIT(argv[0], *Opts, *OptsCtx);

  // Old lli implementation based on ExecutionEngine and MCJIT.
  LLVMContext Context(*OptsCtx);

  // Load the bitcode...
  SMDiagnostic Err;
  std::string InputFile = Opts->get<&LLI_InputFile>();
  std::unique_ptr<Module> Owner = parseIRFile(InputFile, Err, Context);
  Module *Mod = Owner.get();
  if (!Mod)
    reportError(Err, argv[0]);

  bool EnableCacheManager = Opts->get<&LLI_EnableCacheManager>();
  if (EnableCacheManager) {
    std::string CacheName("file:");
    CacheName.append(InputFile);
    Mod->setModuleIdentifier(CacheName);
  }

  // If not jitting lazily, load the whole bitcode file eagerly too.
  bool NoLazyCompilation = Opts->get<&LLI_NoLazyCompilation>();
  if (NoLazyCompilation) {
    // Use *argv instead of argv[0] to work around a wrong GCC warning.
    ExitOnError ExitOnErr(std::string(*argv) +
                          ": bitcode didn't read correctly: ");
    ExitOnErr(Mod->materializeAll());
  }

  std::string TargetTriple = Opts->get<&LLI_TargetTriple>();
  std::string ErrorMsg;
  EngineBuilder builder(std::move(Owner));
  builder.setMArch(codegen::getMArch(*OptsCtx));
  builder.setMCPU(codegen::getCPUStr(*OptsCtx));
  builder.setMAttrs(codegen::getFeatureList(*OptsCtx));
  if (auto RM = codegen::getExplicitRelocModel(*OptsCtx))
    builder.setRelocationModel(*RM);
  if (auto CM = codegen::getExplicitCodeModel(*OptsCtx))
    builder.setCodeModel(*CM);
  builder.setErrorStr(&ErrorMsg);
  builder.setEngineKind(ForceInterpreter ? EngineKind::Interpreter
                                         : EngineKind::JIT);

  // If we are supposed to override the target triple, do so now.
  if (!TargetTriple.empty())
    Mod->setTargetTriple(Triple(Triple::normalize(TargetTriple)));

  bool RemoteMCJIT = Opts->get<&LLI_RemoteMCJIT>();
  std::string ChildExecPath = Opts->get<&LLI_ChildExecPath>();
  std::string ObjectCacheDir = Opts->get<&LLI_ObjectCacheDir>();
  std::vector<std::string> ExtraModules = Opts->get<&LLI_ExtraModules>();
  std::vector<std::string> ExtraObjects = Opts->get<&LLI_ExtraObjects>();
  std::vector<std::string> ExtraArchives = Opts->get<&LLI_ExtraArchives>();
  std::string FakeArgv0 = Opts->get<&LLI_FakeArgv0>();
  std::vector<std::string> InputArgvVec = Opts->get<&LLI_InputArgv>();

  // Enable MCJIT if desired.
  RTDyldMemoryManager *RTDyldMM = nullptr;
  if (!ForceInterpreter) {
    if (RemoteMCJIT)
      RTDyldMM = new ForwardingMemoryManager();
    else
      RTDyldMM = new SectionMemoryManager();

    // Deliberately construct a temp std::unique_ptr to pass in. Do not null out
    // RTDyldMM: We still use it below, even though we don't own it.
    builder.setMCJITMemoryManager(
        std::unique_ptr<RTDyldMemoryManager>(RTDyldMM));
  } else if (RemoteMCJIT) {
    WithColor::error(errs(), argv[0])
        << "remote process execution does not work with the interpreter.\n";
    exit(1);
  }

  builder.setOptLevel(getOptLevel(*Opts));

  TargetOptions Options = codegen::InitTargetOptionsFromCodeGenFlags(
      Triple(TargetTriple), *OptsCtx);

  if (FloatABI::ABIType ABI = codegen::getFloatABIForCalls(*OptsCtx);
      ABI != FloatABI::Default && !Mod->getModuleFlag("float-abi")) {
    Mod->addModuleFlag(Module::Error, "float-abi",
                       MDString::get(Context, FloatABI::getABITypeName(ABI)));
  }

  builder.setTargetOptions(Options);

  // Resolve the target the JIT will compile for and record it in the module
  TargetMachine *TM = builder.selectTarget();
  if (TM && Mod->getTargetTriple().empty())
    Mod->setTargetTriple(TM->getTargetTriple());

  std::unique_ptr<ExecutionEngine> EE(builder.create(TM));
  if (!EE) {
    if (!ErrorMsg.empty())
      WithColor::error(errs(), argv[0])
          << "error creating EE: " << ErrorMsg << "\n";
    else
      WithColor::error(errs(), argv[0]) << "unknown error creating EE!\n";
    exit(1);
  }

  std::unique_ptr<LLIObjectCache> CacheManager;
  if (EnableCacheManager) {
    CacheManager.reset(new LLIObjectCache(ObjectCacheDir));
    EE->setObjectCache(CacheManager.get());
  }

  // Load any additional modules specified on the command line.
  for (unsigned i = 0, e = ExtraModules.size(); i != e; ++i) {
    std::unique_ptr<Module> XMod = parseIRFile(ExtraModules[i], Err, Context);
    if (!XMod)
      reportError(Err, argv[0]);
    if (EnableCacheManager) {
      std::string CacheName("file:");
      CacheName.append(ExtraModules[i]);
      XMod->setModuleIdentifier(CacheName);
    }
    EE->addModule(std::move(XMod));
  }

  for (unsigned i = 0, e = ExtraObjects.size(); i != e; ++i) {
    Expected<object::OwningBinary<object::ObjectFile>> Obj =
        object::ObjectFile::createObjectFile(ExtraObjects[i]);
    if (!Obj) {
      // TODO: Actually report errors helpfully.
      consumeError(Obj.takeError());
      reportError(Err, argv[0]);
    }
    object::OwningBinary<object::ObjectFile> &O = Obj.get();
    EE->addObjectFile(std::move(O));
  }

  for (unsigned i = 0, e = ExtraArchives.size(); i != e; ++i) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> ArBufOrErr =
        MemoryBuffer::getFileOrSTDIN(ExtraArchives[i]);
    if (!ArBufOrErr)
      reportError(Err, argv[0]);
    std::unique_ptr<MemoryBuffer> &ArBuf = ArBufOrErr.get();

    Expected<std::unique_ptr<object::Archive>> ArOrErr =
        object::Archive::create(ArBuf->getMemBufferRef());
    if (!ArOrErr) {
      std::string Buf;
      raw_string_ostream OS(Buf);
      logAllUnhandledErrors(ArOrErr.takeError(), OS);
      errs() << Buf;
      exit(1);
    }
    std::unique_ptr<object::Archive> &Ar = ArOrErr.get();

    object::OwningBinary<object::Archive> OB(std::move(Ar), std::move(ArBuf));

    EE->addArchive(std::move(OB));
  }

  // If the target is Cygwin/MingW and we are generating remote code, we
  // need an extra module to help out with linking.
  if (RemoteMCJIT && Mod->getTargetTriple().isOSCygMing()) {
    addCygMingExtraModule(*EE, Context, Mod->getTargetTriple());
  }

  // The following functions have no effect if their respective profiling
  // support wasn't enabled in the build configuration.
  EE->RegisterJITEventListener(
      JITEventListener::createOProfileJITEventListener());
  EE->RegisterJITEventListener(JITEventListener::createIntelJITEventListener());
  if (!RemoteMCJIT)
    EE->RegisterJITEventListener(
        JITEventListener::createPerfJITEventListener());

  if (!NoLazyCompilation && RemoteMCJIT) {
    WithColor::warning(errs(), argv[0])
        << "remote mcjit does not support lazy compilation\n";
    NoLazyCompilation = true;
  }
  EE->DisableLazyCompilation(NoLazyCompilation);

  // If the user specifically requested an argv[0] to pass into the program,
  // do it now.
  if (!FakeArgv0.empty()) {
    InputFile = static_cast<std::string>(FakeArgv0);
  } else {
    // Otherwise, if there is a .bc suffix on the executable strip it off, it
    // might confuse the program.
    if (StringRef(InputFile).ends_with(".bc"))
      InputFile.erase(InputFile.length() - 3);
  }

  // Add the module's name to the start of the vector of arguments to main().
  InputArgvVec.insert(InputArgvVec.begin(), InputFile);

  // Call the main function from M as if its signature were:
  //   int main (int argc, char **argv, const char **envp)
  // using the contents of Args to determine argc & argv, and the contents of
  // EnvVars to determine envp.
  //
  Function *EntryFn = Mod->getFunction(EntryFunc);
  if (!EntryFn) {
    WithColor::error(errs(), argv[0])
        << '\'' << EntryFunc << "\' function not found in module.\n";
    return -1;
  }

  // Reset errno to zero on entry to main.
  errno = 0;

  int Result = -1;

  // Sanity check use of remote-jit: LLI currently only supports use of the
  // remote JIT on Unix platforms.
  if (RemoteMCJIT) {
#ifndef LLVM_ON_UNIX
    WithColor::warning(errs(), argv[0])
        << "host does not support external remote targets.\n";
    WithColor::note() << "defaulting to local execution\n";
    return -1;
#else
    if (ChildExecPath.empty()) {
      WithColor::error(errs(), argv[0])
          << "-remote-mcjit requires -mcjit-remote-process.\n";
      exit(1);
    } else if (!sys::fs::can_execute(ChildExecPath)) {
      WithColor::error(errs(), argv[0])
          << "unable to find usable child executable: '" << ChildExecPath
          << "'\n";
      return -1;
    }
#endif
  }

  if (!RemoteMCJIT) {
    // If the program doesn't explicitly call exit, we will need the Exit
    // function later on to make an explicit call, so get the function now.
    FunctionCallee Exit = Mod->getOrInsertFunction(
        "exit", Type::getVoidTy(Context), Type::getInt32Ty(Context));

    // Run static constructors.
    if (!ForceInterpreter) {
      // Give MCJIT a chance to apply relocations and set page permissions.
      EE->finalizeObject();
    }
    EE->runStaticConstructorsDestructors(false);

    // Trigger compilation separately so code regions that need to be
    // invalidated will be known.
    (void)EE->getPointerToFunction(EntryFn);
    // Clear instruction cache before code will be executed.
    if (RTDyldMM)
      static_cast<SectionMemoryManager *>(RTDyldMM)
          ->invalidateInstructionCache();

    // Run main.
    Result = EE->runFunctionAsMain(EntryFn, InputArgvVec, envp);

    // Run static destructors.
    EE->runStaticConstructorsDestructors(true);

    // If the program didn't call exit explicitly, we should call it now.
    // This ensures that any atexit handlers get called correctly.
    if (Function *ExitF =
            dyn_cast<Function>(Exit.getCallee()->stripPointerCasts())) {
      if (ExitF->getFunctionType() == Exit.getFunctionType()) {
        std::vector<GenericValue> Args;
        GenericValue ResultGV;
        ResultGV.IntVal = APInt(32, Result);
        Args.push_back(ResultGV);
        EE->runFunction(ExitF, Args);
        WithColor::error(errs(), argv[0])
            << "exit(" << Result << ") returned!\n";
        abort();
      }
    }
    WithColor::error(errs(), argv[0]) << "exit defined with wrong prototype!\n";
    abort();
  } else {
    // else == "if (RemoteMCJIT)"
    orc::ExecutionSession ES(ExitOnErr(launchRemote(*Opts)));

    // Remote target MCJIT doesn't (yet) support static constructors. No reason
    // it couldn't. This is a limitation of the LLI implementation, not the
    // MCJIT itself. FIXME.

    // Create a remote memory manager.
    auto RemoteMM = ExitOnErr(
        orc::EPCGenericRTDyldMemoryManager::CreateWithDefaultBootstrapSymbols(
            ES.getExecutorProcessControl()));

    // Forward MCJIT's memory manager calls to the remote memory manager.
    static_cast<ForwardingMemoryManager *>(RTDyldMM)->setMemMgr(
        std::move(RemoteMM));

    // Forward MCJIT's symbol resolution calls to the remote.
    static_cast<ForwardingMemoryManager *>(RTDyldMM)->setResolver(
        ExitOnErr(RemoteResolver::Create(ES)));
    // Grab the target address of the JIT'd main function on the remote and call
    // it.
    // FIXME: argv and envp handling.
    auto Entry =
        orc::ExecutorAddr(EE->getFunctionAddress(EntryFn->getName().str()));
    EE->finalizeObject();
    LLVM_DEBUG(dbgs() << "Executing '" << EntryFn->getName() << "' at 0x"
                      << format("%llx", Entry.getValue()) << "\n");
    Result = ExitOnErr(ES.getExecutorProcessControl().runAsMain(Entry, {}));

    // Like static constructors, the remote target MCJIT support doesn't handle
    // this yet. It could. FIXME.

    // Delete the EE - we need to tear it down *before* we terminate the session
    // with the remote, otherwise it'll crash when it tries to release resources
    // on a remote that has already been disconnected.
    EE.reset();

    // Signal the remote target that we're done JITing.
    ExitOnErr(ES.endSession());
  }

  return Result;
}

// JITLink debug support plugins put information about JITed code in this GDB
// JIT Interface global from OrcTargetProcess.
extern "C" LLVM_ABI struct jit_descriptor __jit_debug_descriptor;

static struct jit_code_entry *
findNextDebugDescriptorEntry(struct jit_code_entry *Latest) {
  if (Latest == nullptr)
    return __jit_debug_descriptor.first_entry;
  if (Latest->next_entry)
    return Latest->next_entry;
  return nullptr;
}

static ToolOutputFile &claimToolOutput() {
  static std::unique_ptr<ToolOutputFile> ToolOutput = nullptr;
  if (ToolOutput) {
    WithColor::error(errs(), "lli")
        << "Can not claim stdout for tool output twice\n";
    exit(1);
  }
  std::error_code EC;
  ToolOutput = std::make_unique<ToolOutputFile>("-", EC, sys::fs::OF_None);
  if (EC) {
    WithColor::error(errs(), "lli")
        << "Failed to create tool output file: " << EC.message() << "\n";
    exit(1);
  }
  return *ToolOutput;
}

static std::function<void(Module &)> createIRDebugDumper(const LLIOpts &Opts) {
  DumpKind OrcDumpKind = Opts.get<&LLI_OrcDumpKind>();
  switch (OrcDumpKind) {
  case DumpKind::NoDump:
  case DumpKind::DumpDebugDescriptor:
  case DumpKind::DumpDebugObjects:
    return [](Module &M) {};

  case DumpKind::DumpFuncsToStdOut:
    return [](Module &M) {
      printf("[ ");

      for (const auto &F : M) {
        if (F.isDeclaration())
          continue;

        if (F.hasName()) {
          std::string Name(std::string(F.getName()));
          printf("%s ", Name.c_str());
        } else
          printf("<anon> ");
      }

      printf("]\n");
    };

  case DumpKind::DumpModsToStdOut:
    return [](Module &M) {
      outs() << "----- Module Start -----\n" << M << "----- Module End -----\n";
    };

  case DumpKind::DumpModsToDisk:
    return [](Module &M) {
      std::error_code EC;
      raw_fd_ostream Out(M.getModuleIdentifier() + ".ll", EC,
                         sys::fs::OF_TextWithCRLF);
      if (EC) {
        errs() << "Couldn't open " << M.getModuleIdentifier()
               << " for dumping.\nError:" << EC.message() << "\n";
        exit(1);
      }
      Out << M;
    };
  }
  llvm_unreachable("Unknown DumpKind");
}

static std::function<void(MemoryBuffer &)>
createObjDebugDumper(const LLIOpts &Opts) {
  DumpKind OrcDumpKind = Opts.get<&LLI_OrcDumpKind>();
  switch (OrcDumpKind) {
  case DumpKind::NoDump:
  case DumpKind::DumpFuncsToStdOut:
  case DumpKind::DumpModsToStdOut:
  case DumpKind::DumpModsToDisk:
    return [](MemoryBuffer &) {};

  case DumpKind::DumpDebugDescriptor: {
    // Dump the empty descriptor at startup once
    fprintf(stderr, "jit_debug_descriptor 0x%016" PRIx64 "\n",
            pointerToJITTargetAddress(__jit_debug_descriptor.first_entry));
    return [](MemoryBuffer &) {
      // Dump new entries as they appear
      static struct jit_code_entry *Latest = nullptr;
      while (auto *NewEntry = findNextDebugDescriptorEntry(Latest)) {
        fprintf(stderr, "jit_debug_descriptor 0x%016" PRIx64 "\n",
                pointerToJITTargetAddress(NewEntry));
        Latest = NewEntry;
      }
    };
  }

  case DumpKind::DumpDebugObjects: {
    return [](MemoryBuffer &Obj) {
      static struct jit_code_entry *Latest = nullptr;
      static ToolOutputFile &ToolOutput = claimToolOutput();
      while (auto *NewEntry = findNextDebugDescriptorEntry(Latest)) {
        ToolOutput.os().write(NewEntry->symfile_addr, NewEntry->symfile_size);
        Latest = NewEntry;
      }
    };
  }
  }
  llvm_unreachable("Unknown DumpKind");
}

static Error loadDylibs(const LLIOpts &Opts) {
  for (const auto &Dylib : Opts.get<&LLI_Dylibs>()) {
    std::string ErrMsg;
    if (sys::DynamicLibrary::LoadLibraryPermanently(Dylib.c_str(), &ErrMsg))
      return make_error<StringError>(ErrMsg, inconvertibleErrorCode());
  }

  return Error::success();
}

static void exitOnLazyCallThroughFailure() { exit(1); }

static Expected<orc::ThreadSafeModule>
loadModule(StringRef Path, orc::ThreadSafeContext TSCtx, const LLIOpts &Opts) {
  SMDiagnostic Err;
  auto M = TSCtx.withContextDo(
      [&](LLVMContext *Ctx) { return parseIRFile(Path, Err, *Ctx); });
  if (!M) {
    std::string ErrMsg;
    {
      raw_string_ostream ErrMsgStream(ErrMsg);
      Err.print("lli", ErrMsgStream);
    }
    return make_error<StringError>(std::move(ErrMsg), inconvertibleErrorCode());
  }

  if (Opts.get<&LLI_EnableCacheManager>())
    M->setModuleIdentifier("file:" + M->getModuleIdentifier());

  return orc::ThreadSafeModule(std::move(M), std::move(TSCtx));
}

static int mingw_noop_main(void) {
  // Cygwin and MinGW insert calls from the main function to the runtime
  // function __main. The __main function is responsible for setting up main's
  // environment (e.g. running static constructors), however this is not needed
  // when running under lli: the executor process will have run non-JIT ctors,
  // and ORC will take care of running JIT'd ctors. To avoid a missing symbol
  // error we just implement __main as a no-op.
  //
  // FIXME: Move this to ORC-RT (and the ORC-RT substitution library once it
  //        exists). That will allow it to work out-of-process, and for all
  //        ORC tools (the problem isn't lli specific).
  return 0;
}

// Try to enable debugger support for the given instance.
// This alway returns success, but prints a warning if it's not able to enable
// debugger support.
static Error tryEnableDebugSupport(orc::LLJIT &J) {
  if (auto Err = enableDebuggerSupport(J)) {
    [[maybe_unused]] std::string ErrMsg = toString(std::move(Err));
    LLVM_DEBUG(dbgs() << "lli: " << ErrMsg << "\n");
  }
  return Error::success();
}

static int runOrcJIT(const char *ProgName, const LLIOpts &Opts,
                     const clv2::OptionsContext &OptsCtx) {
  std::string InputFile = Opts.get<&LLI_InputFile>();
  std::string EntryFunc = Opts.get<&LLI_EntryFunc>();
  JITKind UseJITKind = Opts.get<&LLI_UseJITKind>();
  JITLinkerKind JITLinker = Opts.get<&LLI_JITLinker>();
  std::string OrcRuntime = Opts.get<&LLI_OrcRuntime>();
  unsigned LazyJITCompileThreads = Opts.get<&LLI_LazyJITCompileThreads>();
  bool PerModuleLazy = Opts.get<&LLI_PerModuleLazy>();
  bool NoProcessSymbols = Opts.get<&LLI_NoProcessSymbols>();
  bool EnableCacheManager = Opts.get<&LLI_EnableCacheManager>();
  std::string ObjectCacheDir = Opts.get<&LLI_ObjectCacheDir>();
  std::vector<std::string> ThreadEntryPoints =
      Opts.get<&LLI_ThreadEntryPoints>();
  std::vector<std::string> ExtraObjects = Opts.get<&LLI_ExtraObjects>();
  std::vector<std::string> InputArgvVec = Opts.get<&LLI_InputArgv>();
  LLJITPlatform Platform = Opts.get<&LLI_Platform>();

  // Start setting up the JIT environment.

  // Parse the main module.
  orc::ThreadSafeContext TSCtx(
      std::make_unique<LLVMContext>(llvm::clv2::defaultOptionsContext()));
  auto MainModule = ExitOnErr(loadModule(InputFile, TSCtx, Opts));

  // Get TargetTriple and DataLayout from the main module if they're explicitly
  // set.
  std::optional<Triple> TT;
  std::optional<DataLayout> DL;
  MainModule.withModuleDo([&](Module &M) {
    if (!M.getTargetTriple().empty())
      TT = M.getTargetTriple();
    if (!M.getDataLayout().isDefault())
      DL = M.getDataLayout();
  });

  orc::LLLazyJITBuilder Builder;

  Builder.setJITTargetMachineBuilder(
      TT ? orc::JITTargetMachineBuilder(*TT)
         : ExitOnErr(orc::JITTargetMachineBuilder::detectHost()));

  TT = Builder.getJITTargetMachineBuilder()->getTargetTriple();
  if (DL)
    Builder.setDataLayout(DL);

  if (!codegen::getMArch(OptsCtx).empty())
    Builder.getJITTargetMachineBuilder()->getTargetTriple().setArchName(
        codegen::getMArch(OptsCtx));

  Builder.getJITTargetMachineBuilder()
      ->setCPU(codegen::getCPUStr(OptsCtx))
      .addFeatures(codegen::getFeatureList(OptsCtx))
      .setRelocationModel(codegen::getExplicitRelocModel(OptsCtx))
      .setCodeModel(codegen::getExplicitCodeModel(OptsCtx));

  // Link process symbols unless NoProcessSymbols is set.
  Builder.setLinkProcessSymbolsByDefault(!NoProcessSymbols);

  // FIXME: Setting a dummy call-through manager in non-lazy mode prevents the
  // JIT builder to instantiate a default (which would fail with an error for
  // unsupported architectures).
  if (UseJITKind != JITKind::OrcLazy) {
    auto ES = std::make_unique<orc::ExecutionSession>(
        ExitOnErr(orc::SelfExecutorProcessControl::Create()));
    Builder.setLazyCallthroughManager(
        std::make_unique<orc::LazyCallThroughManager>(*ES, orc::ExecutorAddr(),
                                                      nullptr));
    Builder.setExecutionSession(std::move(ES));
  }

  Builder.setLazyCompileFailureAddr(
      orc::ExecutorAddr::fromPtr(exitOnLazyCallThroughFailure));
  Builder.setNumCompileThreads(LazyJITCompileThreads);

  // If the object cache is enabled then set a custom compile function
  // creator to use the cache.
  std::unique_ptr<LLIObjectCache> CacheManager;
  if (EnableCacheManager) {

    CacheManager = std::make_unique<LLIObjectCache>(ObjectCacheDir);

    Builder.setCompileFunctionCreator(
        [&](orc::JITTargetMachineBuilder JTMB)
            -> Expected<std::unique_ptr<orc::IRCompileLayer::IRCompiler>> {
          if (LazyJITCompileThreads > 0)
            return std::make_unique<orc::ConcurrentIRCompiler>(
                std::move(JTMB), CacheManager.get());

          auto TM = JTMB.createTargetMachine();
          if (!TM)
            return TM.takeError();

          return std::make_unique<orc::TMOwningSimpleCompiler>(
              std::move(*TM), CacheManager.get());
        });
  }

  // Enable debugging of JIT'd code (only works on JITLink for ELF and MachO).
  Builder.setPrePlatformSetup(tryEnableDebugSupport);

  // Set up LLJIT platform.
  LLJITPlatform P = Platform;
  if (P == LLJITPlatform::Auto)
    P = OrcRuntime.empty() ? LLJITPlatform::GenericIR
                           : LLJITPlatform::ExecutorNative;

  switch (P) {
  case LLJITPlatform::ExecutorNative: {
    Builder.setPlatformSetUp(orc::ExecutorNativePlatform(OrcRuntime));
    break;
  }
  case LLJITPlatform::GenericIR:
    // Nothing to do: LLJITBuilder will use this by default.
    break;
  case LLJITPlatform::Inactive:
    Builder.setPlatformSetUp(orc::setUpInactivePlatform);
    break;
  default:
    llvm_unreachable("Unrecognized platform value");
  }

  switch (JITLinker) {
  case JITLinkerKind::JITLink:
    Builder.getJITTargetMachineBuilder()
        ->setRelocationModel(Reloc::PIC_)
        .setCodeModel(CodeModel::Small);
    Builder.setObjectLinkingLayerCreator(
        [&](orc::ExecutionSession &ES, jitlink::JITLinkMemoryManager &MemMgr) {
          return std::make_unique<orc::ObjectLinkingLayer>(ES, MemMgr);
        });
    break;
  case JITLinkerKind::RuntimeDyld:
    Builder.setObjectLinkingLayerCreator(
        [&](orc::ExecutionSession &ES, jitlink::JITLinkMemoryManager &MemMgr) {
          return std::make_unique<orc::RTDyldObjectLinkingLayer>(
              ES, [](const MemoryBuffer &) {
                return std::make_unique<SectionMemoryManager>();
              });
        });
    break;
  case JITLinkerKind::Default:
    // Let LLJITBuilder decide
    break;
  }

  auto J = ExitOnErr(Builder.create());

  auto *ObjLayer = &J->getObjLinkingLayer();
  if (auto *RTDyldObjLayer =
          dyn_cast<orc::RTDyldObjectLinkingLayer>(ObjLayer)) {
    RTDyldObjLayer->registerJITEventListener(
        *JITEventListener::createGDBRegistrationListener());
#if LLVM_USE_OPROFILE
    RTDyldObjLayer->registerJITEventListener(
        *JITEventListener::createOProfileJITEventListener());
#endif
#if LLVM_USE_INTEL_JITEVENTS
    RTDyldObjLayer->registerJITEventListener(
        *JITEventListener::createIntelJITEventListener());
#endif
#if LLVM_USE_PERF
    RTDyldObjLayer->registerJITEventListener(
        *JITEventListener::createPerfJITEventListener());
#endif
  }

  if (PerModuleLazy)
    J->setPartitionFunction(orc::IRPartitionLayer::compileWholeModule);

  auto IRDump = createIRDebugDumper(Opts);
  J->getIRTransformLayer().setTransform(
      [&](orc::ThreadSafeModule TSM,
          const orc::MaterializationResponsibility &R) {
        TSM.withModuleDo([&](Module &M) {
          if (verifyModule(M, &dbgs())) {
            dbgs() << "Bad module: " << &M << "\n";
            exit(1);
          }
          IRDump(M);
        });
        return TSM;
      });

  auto ObjDump = createObjDebugDumper(Opts);
  J->getObjTransformLayer().setTransform(
      [&](std::unique_ptr<MemoryBuffer> Obj)
          -> Expected<std::unique_ptr<MemoryBuffer>> {
        ObjDump(*Obj);
        return std::move(Obj);
      });

  // If this is a Mingw or Cygwin executor then we need to alias __main to
  // orc_rt_int_void_return_0.
  if (J->getTargetTriple().isOSCygMing()) {
    auto &WorkaroundJD = J->getProcessSymbolsJITDylib()
                             ? *J->getProcessSymbolsJITDylib()
                             : J->getMainJITDylib();
    ExitOnErr(WorkaroundJD.define(
        orc::absoluteSymbols({{J->mangleAndIntern("__main"),
                               {orc::ExecutorAddr::fromPtr(mingw_noop_main),
                                JITSymbolFlags::Exported}}})));
  }

  // Regular modules are greedy: They materialize as a whole and trigger
  // materialization for all required symbols recursively. Lazy modules go
  // through partitioning and they replace outgoing calls with reexport stubs
  // that resolve on call-through.
  auto AddModule = [&](orc::JITDylib &JD, orc::ThreadSafeModule M) {
    return UseJITKind == JITKind::OrcLazy ? J->addLazyIRModule(JD, std::move(M))
                                          : J->addIRModule(JD, std::move(M));
  };

  // Add the main module.
  ExitOnErr(AddModule(J->getMainJITDylib(), std::move(MainModule)));

  // Create JITDylibs and add any extra modules.
  {
    // Create JITDylibs, keep a map from argv index to dylib. We use
    // elementPositions<> to track where each -jd/-extra-module/-extra-archive
    // appeared on the command line so that modules are associated with the
    // last -jd that preceded them.
    std::vector<std::string> JITDylibsVec = Opts.get<&LLI_JITDylibs>();
    const auto &JDPositions = Opts.elementPositions<&LLI_JITDylibs>();
    std::vector<std::string> ExtraModulesVec = Opts.get<&LLI_ExtraModules>();
    const auto &EMPositions = Opts.elementPositions<&LLI_ExtraModules>();
    const auto &EAPositions = Opts.elementPositions<&LLI_ExtraArchives>();

    std::map<unsigned, orc::JITDylib *> IdxToDylib;
    IdxToDylib[0] = &J->getMainJITDylib();
    for (std::size_t JDI = 0; JDI < JITDylibsVec.size(); ++JDI) {
      const std::string &JDName = JITDylibsVec[JDI];
      orc::JITDylib *JD = J->getJITDylibByName(JDName);
      if (!JD) {
        JD = &ExitOnErr(J->createJITDylib(JDName));
        J->getMainJITDylib().addToLinkOrder(*JD);
        JD->addToLinkOrder(J->getMainJITDylib());
      }
      IdxToDylib[JDPositions[JDI]] = JD;
    }

    for (std::size_t EMI = 0; EMI < ExtraModulesVec.size(); ++EMI) {
      auto M = ExitOnErr(loadModule(ExtraModulesVec[EMI], TSCtx, Opts));
      auto EMIdx = EMPositions[EMI];
      assert(EMIdx != 0 && "ExtraModule should have index > 0");
      auto JDItr = std::prev(IdxToDylib.upper_bound(EMIdx));
      auto &JD = *JDItr->second;
      ExitOnErr(AddModule(JD, std::move(M)));
    }

    std::vector<std::string> ExtraArchivesVec = Opts.get<&LLI_ExtraArchives>();
    for (std::size_t EAI = 0; EAI < ExtraArchivesVec.size(); ++EAI) {
      auto EAIdx = EAPositions[EAI];
      assert(EAIdx != 0 && "ExtraArchive should have index > 0");
      auto JDItr = std::prev(IdxToDylib.upper_bound(EAIdx));
      auto &JD = *JDItr->second;
      ExitOnErr(J->linkStaticLibraryInto(JD, ExtraArchivesVec[EAI].c_str()));
    }
  }

  // Add the objects.
  for (auto &ObjPath : ExtraObjects) {
    auto Obj = ExitOnErr(errorOrToExpected(MemoryBuffer::getFile(ObjPath)));
    ExitOnErr(J->addObjectFile(std::move(Obj)));
  }

  // Run any static constructors.
  ExitOnErr(J->initialize(J->getMainJITDylib()));

  // Run any -thread-entry points.
  std::vector<std::thread> AltEntryThreads;
  for (auto &ThreadEntryPoint : ThreadEntryPoints) {
    auto EntryPointSym = ExitOnErr(J->lookup(ThreadEntryPoint));
    typedef void (*EntryPointPtr)();
    auto EntryPoint = EntryPointSym.toPtr<EntryPointPtr>();
    AltEntryThreads.push_back(std::thread([EntryPoint]() { EntryPoint(); }));
  }

  // Resolve and run the main function.
  using MainFnTy = int(int, char *[]);
  auto MainAddr = ExitOnErr(J->lookup(EntryFunc));
  auto MainFn = MainAddr.toPtr<MainFnTy *>();
  int Result = orc::runAsMain(MainFn, InputArgvVec, StringRef(InputFile));

  // Wait for -entry-point threads.
  for (auto &AltEntryThread : AltEntryThreads)
    AltEntryThread.join();

  // Run destructors.
  ExitOnErr(J->deinitialize(J->getMainJITDylib()));

  return Result;
}

static void disallowOrcOptions(const LLIOpts &Opts) {
  // Make sure nobody used an orc-lazy specific option accidentally.

  if (Opts.get<&LLI_LazyJITCompileThreads>() != 0) {
    errs() << "-compile-threads requires -jit-kind=orc-lazy\n";
    exit(1);
  }

  if (!Opts.get<&LLI_ThreadEntryPoints>().empty()) {
    errs() << "-thread-entry requires -jit-kind=orc-lazy\n";
    exit(1);
  }

  if (Opts.get<&LLI_PerModuleLazy>()) {
    errs() << "-per-module-lazy requires -jit-kind=orc-lazy\n";
    exit(1);
  }
}

static Expected<std::unique_ptr<orc::ExecutorProcessControl>>
launchRemote(const LLIOpts &Opts) {
#ifndef LLVM_ON_UNIX
  llvm_unreachable("launchRemote not supported on non-Unix platforms");
#else
  int PipeFD[2][2];
  pid_t ChildPID;

  // Create two pipes.
  if (pipe(PipeFD[0]) != 0 || pipe(PipeFD[1]) != 0)
    perror("Error creating pipe: ");

  ChildPID = fork();

  if (ChildPID == 0) {
    // In the child...

    // Close the parent ends of the pipes
    close(PipeFD[0][1]);
    close(PipeFD[1][0]);

    // Execute the child process.
    std::string CEP = Opts.get<&LLI_ChildExecPath>();
    std::unique_ptr<char[]> ChildPath, ChildIn, ChildOut;
    {
      ChildPath.reset(new char[CEP.size() + 1]);
      llvm::copy(CEP, &ChildPath[0]);
      ChildPath[CEP.size()] = '\0';
      std::string ChildInStr = utostr(PipeFD[0][0]);
      ChildIn.reset(new char[ChildInStr.size() + 1]);
      llvm::copy(ChildInStr, &ChildIn[0]);
      ChildIn[ChildInStr.size()] = '\0';
      std::string ChildOutStr = utostr(PipeFD[1][1]);
      ChildOut.reset(new char[ChildOutStr.size() + 1]);
      llvm::copy(ChildOutStr, &ChildOut[0]);
      ChildOut[ChildOutStr.size()] = '\0';
    }

    char *const args[] = {&ChildPath[0], &ChildIn[0], &ChildOut[0], nullptr};
    int rc = execv(CEP.c_str(), args);
    if (rc != 0)
      perror("Error executing child process: ");
    llvm_unreachable("Error executing child process");
  }
  // else we're the parent...

  // Close the child ends of the pipes
  close(PipeFD[0][0]);
  close(PipeFD[1][1]);

  // Return a SimpleRemoteEPC instance connected to our end of the pipes.
  return orc::SimpleRemoteEPC::Create<orc::FDSimpleRemoteEPCTransport>(
      std::make_unique<llvm::orc::InPlaceTaskDispatcher>(), PipeFD[1][0],
      PipeFD[0][1]);
#endif
}

// For MinGW environments, manually export the __chkstk function from the lli
// executable.
//
// Normally, this function is provided by compiler-rt builtins or libgcc.
// It is named "_alloca" on i386, "___chkstk_ms" on x86_64, and "__chkstk" on
// arm/aarch64. In MSVC configurations, it's named "__chkstk" in all
// configurations.
//
// When Orc tries to resolve symbols at runtime, this succeeds in MSVC
// configurations, somewhat by accident/luck; kernelbase.dll does export a
// symbol named "__chkstk" which gets found by Orc, even if regular applications
// never link against that function from that DLL (it's linked in statically
// from a compiler support library).
//
// The MinGW specific symbol names aren't available in that DLL though.
// Therefore, manually export the relevant symbol from lli, to let it be
// found at runtime during tests.
//
// For real JIT uses, the real compiler support libraries should be linked
// in, somehow; this is a workaround to let tests pass.
//
// We need to make sure that this symbol actually is linked in when we
// try to export it; if no functions allocate a large enough stack area,
// nothing would reference it. Therefore, manually declare it and add a
// reference to it. (Note, the declarations of _alloca/___chkstk_ms/__chkstk
// are somewhat bogus, these functions use a different custom calling
// convention.)
//
// TODO: Move this into libORC at some point, see
// https://github.com/llvm/llvm-project/issues/56603.
#ifdef __MINGW32__
// This is a MinGW version of #pragma comment(linker, "...") that doesn't
// require compiling with -fms-extensions.
#if defined(__i386__)
#undef _alloca
extern "C" void _alloca(void);
static __attribute__((used)) void (*const ref_func)(void) = _alloca;
static __attribute__((section(".drectve"), used)) const char export_chkstk[] =
    "-export:_alloca";
#elif defined(__x86_64__)
extern "C" void ___chkstk_ms(void);
static __attribute__((used)) void (*const ref_func)(void) = ___chkstk_ms;
static __attribute__((section(".drectve"), used)) const char export_chkstk[] =
    "-export:___chkstk_ms";
#else
extern "C" void __chkstk(void);
static __attribute__((used)) void (*const ref_func)(void) = __chkstk;
static __attribute__((section(".drectve"), used)) const char export_chkstk[] =
    "-export:__chkstk";
#endif
#endif
