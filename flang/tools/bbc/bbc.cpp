//===- bbc.cpp - Burnside Bridge Compiler -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Coding style: https://mlir.llvm.org/getting_started/DeveloperGuide/
//
//===----------------------------------------------------------------------===//
///
/// This is a tool for translating Fortran sources to the FIR dialect of MLIR.
///
//===----------------------------------------------------------------------===//

#include "flang/Common/FlangOptionsOptInfos.h"
#include "flang/Frontend/CodeGenOptions.h"
#include "flang/Frontend/TargetOptions.h"
#include "flang/Lower/Bridge.h"
#include "flang/Lower/LoweringOptions.h"
#include "flang/Lower/PFTBuilder.h"
#include "flang/Lower/Support/Verifier.h"
#include "flang/Optimizer/Dialect/Support/FIRContext.h"
#include "flang/Optimizer/Dialect/Support/KindMapping.h"
#include "flang/Optimizer/Support/InitFIR.h"
#include "flang/Optimizer/Support/InternalNames.h"
#include "flang/Optimizer/Support/Utils.h"
#include "flang/Optimizer/Transforms/Passes.h"
#include "flang/Parser/characters.h"
#include "flang/Parser/dump-parse-tree.h"
#include "flang/Parser/message.h"
#include "flang/Parser/parse-tree-visitor.h"
#include "flang/Parser/parse-tree.h"
#include "flang/Parser/parsing.h"
#include "flang/Parser/provenance.h"
#include "flang/Parser/unparse.h"
#include "flang/Semantics/expression.h"
#include "flang/Semantics/runtime-type-info.h"
#include "flang/Semantics/semantics.h"
#include "flang/Semantics/unparse-with-symbols.h"
#include "flang/Support/FPMaxminBehavior.h"
#include "flang/Support/Fortran-features.h"
#include "flang/Support/LangOptions.h"
#include "flang/Support/OpenMP-features.h"
#include "flang/Support/Version.h"
#include "flang/Support/default-kinds.h"
#include "flang/Tools/CrossToolHelpers.h"
#include "flang/Tools/TargetSetup.h"
#include "flang/Version.inc"
#include "mlir/Dialect/OpenMP/OpenMPDialect.h"
#include "mlir/IR/AsmState.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/MLIROptionsOptInfos.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "mlir/Transforms/Passes.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include <memory>

using namespace llvm;
using namespace llvm::clv2;

//===----------------------------------------------------------------------===//
// bbc-local option declarations
//===----------------------------------------------------------------------===//

inline constexpr OptionInfo<std::string> BBC_InputFilename{
    "", "<input file>", Positional{}, Required};

inline constexpr OptionInfo<std::string> BBC_OutputFilename{
    "o", "Specify the output filename", value_desc("filename")};

inline constexpr ListOptionInfo<std::string> BBC_IncludeDirs{
    "I", "include module search paths"};

inline constexpr AliasInfo BBC_IncludeAlias{"module-directory", "I",
                                            "module search directory"};

inline constexpr ListOptionInfo<std::string> BBC_IntrinsicIncludeDirs{
    "J", "intrinsic module search paths"};

inline constexpr ListOptionInfo<std::string> BBC_ImplicitUseModules{
    "implicit-use-module", "implicitly USE the named module for testing"};

inline constexpr AliasInfo BBC_IntrinsicIncludeAlias{
    "intrinsic-module-directory", "J", "intrinsic module directory"};

inline constexpr AliasInfo BBC_IntrinsicModulePath{
    "fintrinsic-modules-path", "J", "intrinsic module search paths"};

inline constexpr OptionInfo<std::string> BBC_ModuleDir{
    "module", "module output directory (default .)", Init{"."}};

inline constexpr OptionInfo<std::string> BBC_ModuleSuffix{
    "module-suffix", "module file suffix override", Init{".mod"}};

inline constexpr OptionInfo<bool> BBC_EmitFIR{
    "emit-fir", "Dump the FIR created by lowering and exit", Init{false}};

inline constexpr OptionInfo<bool> BBC_EmitHLFIR{
    "emit-hlfir", "Dump the HLFIR created by lowering and exit", Init{false}};

inline constexpr OptionInfo<bool> BBC_WarnStdViolation{
    "Mstandard", "emit warnings", Init{false}};

inline constexpr OptionInfo<bool> BBC_WarnIsError{
    "Werror", "warnings are errors", Init{false}};

inline constexpr OptionInfo<bool> BBC_DumpSymbols{
    "dump-symbols", "dump the symbol table", Init{false}};

inline constexpr OptionInfo<bool> BBC_PftDumpTest{
    "pft-test", "parse the input, create a PFT, dump it, and exit",
    Init{false}};

inline constexpr OptionInfo<bool> BBC_EnableOpenMP{"fopenmp", "enable openmp",
                                                   Init{false}};

inline constexpr OptionInfo<bool> BBC_EnableOpenMPDevice{
    "fopenmp-is-target-device", "enable openmp device compilation",
    Init{false}};

inline constexpr OptionInfo<std::string>
    BBC_EnableDoConcurrentToOpenMPConversion{
        "fdo-concurrent-to-openmp",
        "Try to map `do concurrent` loops to OpenMP [none|host|device]",
        Init{"none"}};

inline constexpr OptionInfo<bool> BBC_EnableOpenMPGPU{
    "fopenmp-is-gpu", "enable openmp GPU target codegen", Init{false}};

inline constexpr OptionInfo<bool> BBC_EnableOpenMPForceUSM{
    "fopenmp-force-usm", "force openmp unified shared memory mode",
    Init{false}};

inline constexpr ListOptionInfo<std::string> BBC_TargetTriplesOpenMP{
    "fopenmp-targets", "comma-separated list of OpenMP offloading triples",
    CommaSeparated};

inline constexpr OptionInfo<uint32_t> BBC_SetOpenMPVersion{
    "fopenmp-version", "OpenMP standard version", Init{31u}};

inline constexpr OptionInfo<uint32_t> BBC_SetOpenMPTargetDebug{
    "fopenmp-target-debug",
    "Enable debugging in the OpenMP offloading device RTL", Init{0u}};

inline constexpr OptionInfo<bool> BBC_SetOpenMPThreadSubscription{
    "fopenmp-assume-threads-oversubscription",
    "Assume work-shared loops do not have more "
    "iterations than participating threads.",
    Init{false}};

inline constexpr OptionInfo<bool> BBC_SetOpenMPTeamSubscription{
    "fopenmp-assume-teams-oversubscription",
    "Assume distributed loops do not have more iterations than "
    "participating teams.",
    Init{false}};

inline constexpr OptionInfo<bool> BBC_SetOpenMPNoThreadState{
    "fopenmp-assume-no-thread-state",
    "Assume that no thread in a parallel region will modify an ICV.",
    Init{false}};

inline constexpr OptionInfo<bool> BBC_SetOpenMPNoNestedParallelism{
    "fopenmp-assume-no-nested-parallelism",
    "Assume that no thread in a parallel region will encounter "
    "a parallel region.",
    Init{false}};

inline constexpr OptionInfo<bool> BBC_SetNoGPULib{
    "nogpulib", "Do not link device library for CUDA/HIP device compilation",
    Init{false}};

inline constexpr OptionInfo<bool> BBC_EnableOpenACC{
    "fopenacc", "enable openacc", Init{false}};

inline constexpr OptionInfo<bool> BBC_EnableNoPPCNativeVecElemOrder{
    "fno-ppc-native-vector-element-order",
    "no PowerPC native vector element order.", Init{false}};

inline constexpr OptionInfo<bool> BBC_EnableCUDA{"fcuda", "enable CUDA Fortran",
                                                 Init{false}};

inline constexpr OptionInfo<bool> BBC_EnableCUDAInit{
    "fcuda-init", "enable CUDA Init", Init{false}};

inline constexpr OptionInfo<bool> BBC_WarnOnAllExtensions{
    "pedantic", "warn on all extensions", Init{false}};

inline constexpr OptionInfo<bool> BBC_EnableDoConcurrentOffload{
    "fdoconcurrent-offload", "enable do concurrent offload", Init{false}};

inline constexpr OptionInfo<bool> BBC_DisableCUDAWarpFunction{
    "fcuda-disable-warp-function", "Disable CUDA Warp Function", Init{false}};

inline constexpr OptionInfo<std::string> BBC_EnableGPUMode{
    "gpu", "Enable GPU Mode managed|unified|pinned", Init{""}};

inline constexpr OptionInfo<std::string> BBC_CompilerDirectiveSentinel{
    "sentinel-test", "Test additional sentinel", Init{"dir$"}};

inline constexpr OptionInfo<bool> BBC_FixedForm{
    "ffixed-form", "enable fixed form", Init{false}};

inline constexpr OptionInfo<std::string> BBC_TargetTripleOverride{
    "target", "Override host target triple", Init{""}};

inline constexpr OptionInfo<bool> BBC_IntegerWrapAround{
    "fwrapv", "Treat signed integer overflow as two's complement", Init{false}};

inline constexpr OptionInfo<bool> BBC_InitGlobalZero{
    "finit-global-zero",
    "Zero initialize globals without default initialization", Init{true}};

inline constexpr OptionInfo<bool> BBC_ReallocateLHS{
    "frealloc-lhs",
    "Follow Fortran 2003 rules for (re)allocating "
    "the LHS of the intrinsic assignment",
    Init{true}};

inline constexpr OptionInfo<bool> BBC_StackRepackArrays{
    "fstack-repack-arrays",
    "Allocate temporary arrays for -frepack-arrays "
    "in stack memory",
    Init{false}};

inline constexpr OptionInfo<bool> BBC_RepackArrays{
    "frepack-arrays",
    "Pack non-contiguous assummed shape arrays "
    "into contiguous memory",
    Init{false}};

inline constexpr OptionInfo<bool> BBC_RepackArraysWhole{
    "frepack-arrays-continuity-whole",
    "Repack arrays that are non-contiguous "
    "in any dimension. If set to false, "
    "only the arrays non-contiguous in the "
    "leading dimension will be repacked",
    Init{true}};

inline constexpr OptionInfo<std::string> BBC_ComplexRange{
    "complex-range",
    "Controls the various implementations for complex "
    "multiplication and division [full|improved|basic]",
    Init{""}};

// FPMaxminBehavior enum option
inline constexpr EnumVal<Fortran::common::FPMaxminBehavior>
    FPMaxminBehaviorVals[] = {
        {"legacy", Fortran::common::FPMaxminBehavior::Legacy, "cmp+select"},
        {"portable", Fortran::common::FPMaxminBehavior::Portable,
         "cmp+select and arith.max/minnumf when nnan and nsz fast math flags "
         "are enabled"},
        {"extremum", Fortran::common::FPMaxminBehavior::Extremum,
         "arith.max/minimum"},
        {"extremenum", Fortran::common::FPMaxminBehavior::ExtremeNum,
         "arith.max/minnum"},
};

inline constexpr auto BBC_FPMaxminBehavior =
    makeEnumOption<Fortran::common::FPMaxminBehavior>(
        "ffp-maxmin-behavior",
        "Control max/min and [max|min][loc|val] lowering "
        "[legacy|portable|extremum|extremenum]",
        FPMaxminBehaviorVals, Init{Fortran::common::FPMaxminBehavior::Legacy});

//===----------------------------------------------------------------------===//
// Tool registry
//===----------------------------------------------------------------------===//

inline constexpr OptionsRegistry<
    &BBC_InputFilename, &BBC_OutputFilename, &BBC_IncludeDirs,
    &BBC_IncludeAlias, &BBC_IntrinsicIncludeDirs, &BBC_ImplicitUseModules,
    &BBC_IntrinsicIncludeAlias, &BBC_IntrinsicModulePath, &BBC_ModuleDir,
    &BBC_ModuleSuffix, &BBC_EmitFIR, &BBC_EmitHLFIR, &BBC_WarnStdViolation,
    &BBC_WarnIsError, &BBC_DumpSymbols, &BBC_PftDumpTest, &BBC_EnableOpenMP,
    &BBC_EnableOpenMPDevice, &BBC_EnableDoConcurrentToOpenMPConversion,
    &BBC_EnableOpenMPGPU, &BBC_EnableOpenMPForceUSM, &BBC_TargetTriplesOpenMP,
    &BBC_SetOpenMPVersion, &BBC_SetOpenMPTargetDebug,
    &BBC_SetOpenMPThreadSubscription, &BBC_SetOpenMPTeamSubscription,
    &BBC_SetOpenMPNoThreadState, &BBC_SetOpenMPNoNestedParallelism,
    &BBC_SetNoGPULib, &BBC_EnableOpenACC, &BBC_EnableNoPPCNativeVecElemOrder,
    &BBC_EnableCUDA, &BBC_EnableCUDAInit, &BBC_EnableDoConcurrentOffload,
    &BBC_WarnOnAllExtensions, &BBC_DisableCUDAWarpFunction, &BBC_EnableGPUMode,
    &BBC_CompilerDirectiveSentinel, &BBC_FixedForm, &BBC_TargetTripleOverride,
    &BBC_IntegerWrapAround, &BBC_InitGlobalZero, &BBC_ReallocateLHS,
    &BBC_StackRepackArrays, &BBC_RepackArrays, &BBC_RepackArraysWhole,
    &BBC_ComplexRange, &BBC_FPMaxminBehavior>
    BBCToolReg;

using BBCToolOpts = decltype(BBCToolReg)::ParsedOptionsT;

#define FLANG_EXCLUDE_CODEGEN
#include "flang/Optimizer/Passes/Pipelines.h"

//===----------------------------------------------------------------------===//

using ProgramName = std::string;

// Print the module with the "module { ... }" wrapper, preventing
// information loss from attribute information appended to the module
static void printModule(mlir::ModuleOp mlirModule, llvm::raw_ostream &out) {
  out << mlirModule << '\n';
}

static void registerAllPasses() {
  fir::support::registerMLIRPassesForFortranTools();
  fir::registerOptTransformPasses();
}

/// Create a target machine that is at least sufficient to get data-layout
/// information required by flang semantics and lowering. Note that it may not
/// contain all the CPU feature information to get optimized assembly generation
/// from LLVM IR. Drivers that needs to generate assembly from LLVM IR should
/// create a target machine according to their specific options.
static std::unique_ptr<llvm::TargetMachine>
createTargetMachine(llvm::StringRef targetTriple, std::string &error,
                    const llvm::clv2::OptionsContext &optsCtx) {
  std::string triple{targetTriple};
  if (triple.empty())
    triple = llvm::sys::getDefaultTargetTriple();
  llvm::Triple parsedTriple(triple);

  const llvm::Target *theTarget =
      llvm::TargetRegistry::lookupTarget(parsedTriple, error);
  if (!theTarget)
    return nullptr;
  // Carry the parsed options: TargetMachine's ctor reads through these, and a
  // default-constructed TargetOptions would silently use an empty context.
  llvm::TargetOptions targetOptions;
  targetOptions.OptsCtx = &optsCtx;
  // MCAsmInfo reads through the nested MC options, so set both (as llc does).
  targetOptions.MCOptions.OptsCtx = &optsCtx;
  return std::unique_ptr<llvm::TargetMachine>{
      theTarget->createTargetMachine(parsedTriple, /*CPU=*/"",
                                     /*Features=*/"", targetOptions,
                                     /*Reloc::Model=*/std::nullopt)};
}

/// Build and execute the OpenMPFIRPassPipeline with its own instance
/// of the pass manager, allowing it to be invoked as soon as it's
/// required without impacting the main pass pipeline that may be invoked
/// more than once for verification.
static llvm::LogicalResult runOpenMPPasses(mlir::ModuleOp mlirModule,
                                           const OptionsContext &OptsCtx) {
  const auto *Opts = OptsCtx.getViewPtr<&BBCToolReg>();
  mlir::PassManager pm(mlirModule->getName(),
                       mlir::OpPassManager::Nesting::Implicit);
  using DoConcurrentMappingKind =
      Fortran::frontend::CodeGenOptions::DoConcurrentMappingKind;

  fir::OpenMPFIRPassPipelineOpts opts;
  opts.isTargetDevice = Opts->get<&BBC_EnableOpenMPDevice>();
  opts.doConcurrentMappingKind =
      llvm::StringSwitch<DoConcurrentMappingKind>(
          Opts->get<&BBC_EnableDoConcurrentToOpenMPConversion>())
          .Case("host", DoConcurrentMappingKind::DCMK_Host)
          .Case("device", DoConcurrentMappingKind::DCMK_Device)
          .Default(DoConcurrentMappingKind::DCMK_None);

  fir::createOpenMPFIRPassPipeline(pm, opts);
  (void)mlir::applyPassManagerCLOptions(pm);
  if (mlir::failed(pm.run(mlirModule))) {
    llvm::errs() << "FATAL: failed to correctly apply OpenMP pass pipeline";
    return mlir::failure();
  }
  return mlir::success();
}

//===----------------------------------------------------------------------===//
// Translate Fortran input to FIR, a dialect of MLIR.
//===----------------------------------------------------------------------===//

static llvm::LogicalResult convertFortranSourceToMLIR(
    std::string path, Fortran::parser::Options options,
    const ProgramName &programPrefix,
    Fortran::semantics::SemanticsContext &semanticsContext,
    const mlir::PassPipelineCLParser &passPipeline,
    const llvm::TargetMachine &targetMachine, const OptionsContext &OptsCtx) {

  const auto *Opts = OptsCtx.getViewPtr<&BBCToolReg>();

  // prep for prescan and parse
  Fortran::parser::Parsing parsing{semanticsContext.allCookedSources()};
  const std::string &compilerDirectiveSentinel =
      Opts->get<&BBC_CompilerDirectiveSentinel>();
  if (!compilerDirectiveSentinel.empty()) {
    options.compilerDirectiveSentinels.push_back(compilerDirectiveSentinel);
  }
  parsing.Prescan(path, options);
  if (!parsing.messages().empty() && (parsing.messages().AnyFatalError())) {
    llvm::errs() << programPrefix << "could not scan " << path << '\n';
    parsing.messages().Emit(llvm::errs(), parsing.allCooked());
    return mlir::failure();
  }

  // parse the input Fortran
  parsing.Parse(llvm::outs(), semanticsContext.langOptions());
  if (!parsing.consumedWholeFile()) {
    parsing.messages().Emit(llvm::errs(), parsing.allCooked());
    parsing.EmitMessage(llvm::errs(), parsing.finalRestingPlace(),
                        "parser FAIL (final position)",
                        "error: ", llvm::raw_ostream::RED);
    return mlir::failure();
  } else if ((!parsing.messages().empty() &&
              (parsing.messages().AnyFatalError())) ||
             !parsing.parseTree().has_value()) {
    parsing.messages().Emit(llvm::errs(), parsing.allCooked());
    llvm::errs() << programPrefix << "could not parse " << path << '\n';
    return mlir::failure();
  } else {
    semanticsContext.messages().Annex(std::move(parsing.messages()));
  }

  // run semantics
  auto &parseTree = *parsing.parseTree();
  const auto &implicitUseModules = Opts->get<&BBC_ImplicitUseModules>();
  std::vector<std::string> implicitUseModuleNames;
  for (const std::string &module : implicitUseModules) {
    bool moduleIsDefinedInInput{false};
    for (const Fortran::parser::ProgramUnit &unit : parseTree.v) {
      if (const auto *indirectModule{std::get_if<
              Fortran::common::Indirection<Fortran::parser::Module>>(
              &unit.u)}) {
        const auto &moduleStmt{
            std::get<Fortran::parser::Statement<Fortran::parser::ModuleStmt>>(
                indirectModule->value().t)};
        if (moduleStmt.statement.v.source.ToString() == module) {
          moduleIsDefinedInInput = true;
          break;
        }
      }
    }
    if (!moduleIsDefinedInInput) {
      implicitUseModuleNames.push_back(module);
    }
  }
  semanticsContext.set_implicitUseModules(implicitUseModuleNames);
  Fortran::semantics::Semantics semantics(semanticsContext, parseTree);
  semantics.Perform();
  semantics.EmitMessages(llvm::errs());
  if (semantics.AnyFatalError()) {
    llvm::errs() << programPrefix << "semantic errors in " << path << '\n';
    return mlir::failure();
  }
  Fortran::semantics::RuntimeDerivedTypeTables tables;
  if (!semantics.AnyFatalError()) {
    tables =
        Fortran::semantics::BuildRuntimeDerivedTypeTables(semanticsContext);
    if (!tables.schemata)
      llvm::errs() << programPrefix
                   << "could not find module file for __fortran_type_info\n";
  }

  bool dumpSymbols = Opts->get<&BBC_DumpSymbols>();
  if (dumpSymbols) {
    semantics.DumpSymbols(llvm::outs());
    return mlir::success();
  }

  bool pftDumpTest = Opts->get<&BBC_PftDumpTest>();
  if (pftDumpTest) {
    // Use default lowering options for PFT dump test
    Fortran::lower::LoweringOptions loweringOptions{};
    if (auto ast = Fortran::lower::createPFT(parseTree, semanticsContext,
                                             loweringOptions, OptsCtx)) {
      Fortran::lower::dumpPFT(llvm::outs(), *ast);
      return mlir::success();
    }
    llvm::errs() << "Pre FIR Tree is NULL.\n";
    return mlir::failure();
  }

  // translate to FIR dialect of MLIR
  mlir::DialectRegistry registry;
  fir::support::registerNonCodegenDialects(registry);
  fir::support::addFIRExtensions(registry);
  mlir::MLIRContext ctx(OptsCtx, registry);
  fir::support::loadNonCodegenDialects(ctx);
  auto &defKinds = semanticsContext.defaultKinds();
  fir::KindMapping kindMap(
      &ctx, llvm::ArrayRef<fir::KindTy>{fir::fromDefaultKinds(defKinds)});
  std::string targetTriple = targetMachine.getTargetTriple().normalize();

  bool enableNoPPCNativeVecElemOrder =
      Opts->get<&BBC_EnableNoPPCNativeVecElemOrder>();
  bool integerWrapAround = Opts->get<&BBC_IntegerWrapAround>();
  bool initGlobalZero = Opts->get<&BBC_InitGlobalZero>();
  bool reallocateLHS = Opts->get<&BBC_ReallocateLHS>();
  bool stackRepackArrays = Opts->get<&BBC_StackRepackArrays>();
  bool repackArrays = Opts->get<&BBC_RepackArrays>();
  bool repackArraysWhole = Opts->get<&BBC_RepackArraysWhole>();
  bool enableCUDA = Opts->get<&BBC_EnableCUDA>();
  const std::string &complexRange = Opts->get<&BBC_ComplexRange>();
  auto fpMaxminBehavior = Opts->get<&BBC_FPMaxminBehavior>();

  // Use default lowering options for bbc.
  Fortran::lower::LoweringOptions loweringOptions{};
  loweringOptions.setNoPPCNativeVecElemOrder(enableNoPPCNativeVecElemOrder);
  loweringOptions.setIntegerWrapAround(integerWrapAround);
  loweringOptions.setInitGlobalZero(initGlobalZero);
  loweringOptions.setReallocateLHS(reallocateLHS);
  loweringOptions.setStackRepackArrays(stackRepackArrays);
  loweringOptions.setRepackArrays(repackArrays);
  loweringOptions.setRepackArraysWhole(repackArraysWhole);
  loweringOptions.setSkipExternalRttiDefinition(
      llvm::flang_opts::getSkipExternalRttiDefinition(ctx.getOptionsContext()));
  if (enableCUDA)
    loweringOptions.setCUDARuntimeCheck(true);
  if (complexRange == "improved" || complexRange == "basic")
    loweringOptions.setComplexDivisionToRuntime(false);
  loweringOptions.setFPMaxminBehavior(fpMaxminBehavior);
  std::vector<Fortran::lower::EnvironmentDefault> envDefaults = {};
  Fortran::frontend::TargetOptions targetOpts;
  Fortran::frontend::CodeGenOptions cgOpts;
  auto burnside = Fortran::lower::LoweringBridge::create(
      ctx, semanticsContext, defKinds, semanticsContext.intrinsics(),
      semanticsContext.targetCharacteristics(), parsing.allCooked(),
      targetTriple, kindMap, loweringOptions, envDefaults,
      semanticsContext.languageFeatures(), targetMachine, targetOpts, cgOpts);
  mlir::ModuleOp mlirModule = burnside.getModule();

  bool enableOpenMP = Opts->get<&BBC_EnableOpenMP>();
  bool enableOpenMPDevice = Opts->get<&BBC_EnableOpenMPDevice>();
  bool enableOpenMPGPU = Opts->get<&BBC_EnableOpenMPGPU>();
  bool enableOpenMPForceUSM = Opts->get<&BBC_EnableOpenMPForceUSM>();
  uint32_t setOpenMPVersion = Opts->get<&BBC_SetOpenMPVersion>();
  uint32_t setOpenMPTargetDebug = Opts->get<&BBC_SetOpenMPTargetDebug>();
  bool setOpenMPThreadSubscription =
      Opts->get<&BBC_SetOpenMPThreadSubscription>();
  bool setOpenMPTeamSubscription = Opts->get<&BBC_SetOpenMPTeamSubscription>();
  bool setOpenMPNoThreadState = Opts->get<&BBC_SetOpenMPNoThreadState>();
  bool setOpenMPNoNestedParallelism =
      Opts->get<&BBC_SetOpenMPNoNestedParallelism>();
  bool setNoGPULib = Opts->get<&BBC_SetNoGPULib>();
  const auto &targetTriplesOpenMP = Opts->get<&BBC_TargetTriplesOpenMP>();

  if (enableOpenMP) {
    if (enableOpenMPGPU && !enableOpenMPDevice) {
      llvm::errs() << "FATAL: -fopenmp-is-gpu can only be set if "
                      "-fopenmp-is-target-device is also set";
      return mlir::failure();
    }
    // Construct offloading target triples vector.
    std::vector<llvm::Triple> targetTriples;
    targetTriples.reserve(targetTriplesOpenMP.size());
    for (llvm::StringRef s : targetTriplesOpenMP)
      targetTriples.emplace_back(s);

    auto offloadModuleOpts = mlir::omp::OffloadModuleOpts(
        setOpenMPTargetDebug, setOpenMPTeamSubscription,
        setOpenMPThreadSubscription, setOpenMPNoThreadState,
        setOpenMPNoNestedParallelism, enableOpenMPDevice, enableOpenMPGPU,
        enableOpenMPForceUSM, setOpenMPVersion, /*hostIRFile=*/"",
        targetTriples, setNoGPULib);
    mlir::omp::setOffloadModuleInterfaceAttributes(mlirModule,
                                                   offloadModuleOpts);
    mlir::omp::setOpenMPVersionAttribute(mlirModule, setOpenMPVersion);
    if (!integerWrapAround)
      mlir::omp::setOpenMPIntegerWrapAround(mlirModule, false);
  }
  burnside.lower(parseTree, semanticsContext);
  std::error_code ec;
  const std::string &inputFilename = Opts->get<&BBC_InputFilename>();
  const std::string &outputFilename = Opts->get<&BBC_OutputFilename>();
  std::string outputName = outputFilename;
  if (!outputName.size())
    outputName = llvm::sys::path::stem(inputFilename).str().append(".mlir");
  llvm::raw_fd_ostream out(outputName, ec);
  if (ec)
    return mlir::emitError(mlir::UnknownLoc::get(&ctx),
                           "could not open output file ")
           << outputName;

  // WARNING: This pipeline must be run immediately after the lowering to
  // ensure that the FIR is correct with respect to OpenMP operations/
  // attributes.
  if (enableOpenMP)
    if (mlir::failed(runOpenMPPasses(mlirModule, OptsCtx)))
      return mlir::failure();

  bool emitFIR = Opts->get<&BBC_EmitFIR>();
  bool emitHLFIR = Opts->get<&BBC_EmitHLFIR>();

  // Otherwise run the default passes.
  mlir::PassManager pm(mlirModule->getName(),
                       mlir::OpPassManager::Nesting::Implicit);
  pm.enableVerifier(/*verifyPasses=*/true);
  (void)mlir::applyPassManagerCLOptions(pm);
  if (passPipeline.hasAnyOccurrences()) {
    // run the command-line specified pipeline
    hlfir::registerHLFIRPasses();
    (void)passPipeline.addToPipeline(pm, [&](const llvm::Twine &msg) {
      mlir::emitError(mlir::UnknownLoc::get(&ctx)) << msg;
      return mlir::failure();
    });
  } else if (emitFIR || emitHLFIR) {
    // --emit-fir: Build the IR, verify it, and dump the IR if the IR passes
    // verification. Use --dump-module-on-failure to dump invalid IR.
    pm.addPass(std::make_unique<Fortran::lower::VerifierPass>());
    if (mlir::failed(pm.run(mlirModule))) {
      llvm::errs() << "FATAL: verification of lowering to FIR failed";
      return mlir::failure();
    }

    if (emitFIR) {
      // lower HLFIR to FIR
      fir::EnableOpenMP enableOmp =
          enableOpenMP ? fir::EnableOpenMP::Full : fir::EnableOpenMP::None;
      MLIRToLLVMPassPipelineConfig config(llvm::OptimizationLevel::O2);
      config.fpMaxminBehavior = loweringOptions.getFPMaxminBehavior();
      fir::createHLFIRToFIRPassPipeline(pm, enableOmp, config);
      if (mlir::failed(pm.run(mlirModule))) {
        llvm::errs() << "FATAL: lowering from HLFIR to FIR failed";
        return mlir::failure();
      }
    }

    printModule(mlirModule, out);
    return mlir::success();
  } else {
    // run the default canned pipeline
    pm.addPass(std::make_unique<Fortran::lower::VerifierPass>());

    // Add O2 optimizer pass pipeline.
    MLIRToLLVMPassPipelineConfig config(llvm::OptimizationLevel::O2);
    config.fpMaxminBehavior = loweringOptions.getFPMaxminBehavior();
    config.SkipConvertComplexPow = targetMachine.getTargetTriple().isAMDGCN();
    if (enableOpenMP)
      config.EnableOpenMP = true;
    if (enableOpenMPDevice)
      config.EnableOpenMPIsTargetDevice = true;
    config.NSWOnLoopVarInc = !integerWrapAround;
    fir::registerDefaultInlinerPass(config);
    fir::createDefaultFIROptimizerPassPipeline(pm, config);
  }

  if (mlir::succeeded(pm.run(mlirModule))) {
    // Emit MLIR and do not lower to LLVM IR.
    printModule(mlirModule, out);
    return mlir::success();
  }
  // Something went wrong. Try to dump the MLIR module.
  llvm::errs() << "oops, pass manager reported failure\n";
  return mlir::failure();
}

int main(int argc, char **argv) {
  [[maybe_unused]] llvm::InitLLVM y(argc, argv);
  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  registerAllPasses();

  mlir::registerMLIRContextCLOptions();
  mlir::registerAsmPrinterCLOptions();
  mlir::PassPipelineCLParser passPipe("", "Compiler passes to run");

  // Build the OptionParser with bbc-local, flang library, MLIR, and LLVM
  // options.
  clv2::OptionParser P;
  P.add<&BBCToolReg>();
  P.add<&FlangOptsReg>();
  P.add<&clv2::MLIROptsReg>();
  RegisterCoreLLVMOptions(P);
  P.enableGlobalDynamicEntries();
  mlir::registerPassManagerCLOptions(P);
  passPipe.registerWith(P);

  auto OptsCtx = P.parse(argc, argv, "Burnside Bridge Compiler\n");
  if (!OptsCtx)
    return 1;

  const auto *Opts = OptsCtx->getViewPtr<&BBCToolReg>();

  // Disable the ExternalNameConversion pass by default until all the tests have
  // been updated to pass with it enabled. Override via mutable view.
  if (auto *FlangOpts = OptsCtx->getViewPtr<&FlangOptsReg>()) {
    if (!FlangOpts->specified<&FLANG_DisableExternalNameInterop>())
      FlangOpts->get<&FLANG_DisableExternalNameInterop>() = true;
  }

  ProgramName programPrefix;
  programPrefix = argv[0] + ": "s;

  const auto &includeDirs = Opts->get<&BBC_IncludeDirs>();
  auto &mutableIncludeDirs =
      const_cast<std::vector<std::string> &>(includeDirs);
  const auto &intrinsicIncludeDirs = Opts->get<&BBC_IntrinsicIncludeDirs>();
  auto &mutableIntrinsicIncludeDirs =
      const_cast<std::vector<std::string> &>(intrinsicIncludeDirs);

  if (includeDirs.size() == 0) {
    mutableIncludeDirs.push_back(".");
    // Default Fortran modules should be installed in include/flang (a sibling
    // to the bin) directory.
    mutableIntrinsicIncludeDirs.push_back(
        llvm::sys::path::parent_path(
            llvm::sys::path::parent_path(
                llvm::sys::fs::getMainExecutable(argv[0], nullptr)))
            .str() +
        "/include/flang");
  }

  Fortran::parser::Options options;
  options.predefinitions.emplace_back("__flang__"s, "1"s);
  options.predefinitions.emplace_back("__flang_major__"s,
                                      std::string{FLANG_VERSION_MAJOR_STRING});
  options.predefinitions.emplace_back("__flang_minor__"s,
                                      std::string{FLANG_VERSION_MINOR_STRING});
  options.predefinitions.emplace_back(
      "__flang_patchlevel__"s, std::string{FLANG_VERSION_PATCHLEVEL_STRING});

  bool enableOpenMP = Opts->get<&BBC_EnableOpenMP>();
  bool enableOpenMPDevice = Opts->get<&BBC_EnableOpenMPDevice>();
  bool enableOpenMPGPU = Opts->get<&BBC_EnableOpenMPGPU>();
  bool enableOpenMPForceUSM = Opts->get<&BBC_EnableOpenMPForceUSM>();
  uint32_t setOpenMPVersion = Opts->get<&BBC_SetOpenMPVersion>();
  uint32_t setOpenMPTargetDebug = Opts->get<&BBC_SetOpenMPTargetDebug>();
  bool setOpenMPThreadSubscription =
      Opts->get<&BBC_SetOpenMPThreadSubscription>();
  bool setOpenMPTeamSubscription = Opts->get<&BBC_SetOpenMPTeamSubscription>();
  bool setOpenMPNoThreadState = Opts->get<&BBC_SetOpenMPNoThreadState>();
  bool setOpenMPNoNestedParallelism =
      Opts->get<&BBC_SetOpenMPNoNestedParallelism>();
  bool setNoGPULib = Opts->get<&BBC_SetNoGPULib>();
  const auto &targetTriplesOpenMP = Opts->get<&BBC_TargetTriplesOpenMP>();
  bool enableOpenACC = Opts->get<&BBC_EnableOpenACC>();
  bool enableCUDA = Opts->get<&BBC_EnableCUDA>();
  bool enableCUDAInit = Opts->get<&BBC_EnableCUDAInit>();
  bool enableDoConcurrentOffload = Opts->get<&BBC_EnableDoConcurrentOffload>();
  bool disableCUDAWarpFunction = Opts->get<&BBC_DisableCUDAWarpFunction>();
  const std::string &enableGPUMode = Opts->get<&BBC_EnableGPUMode>();
  bool fixedForm = Opts->get<&BBC_FixedForm>();
  const std::string &targetTripleOverride =
      Opts->get<&BBC_TargetTripleOverride>();
  const std::string &moduleDir = Opts->get<&BBC_ModuleDir>();
  const std::string &moduleSuffix = Opts->get<&BBC_ModuleSuffix>();
  bool warnStdViolation = Opts->get<&BBC_WarnStdViolation>();
  bool warnIsError = Opts->get<&BBC_WarnIsError>();

  Fortran::common::LangOptions langOpts;
  langOpts.NoGPULib = setNoGPULib;
  langOpts.OpenMPVersion = setOpenMPVersion;
  langOpts.OpenMPIsTargetDevice = enableOpenMPDevice;
  langOpts.OpenMPIsGPU = enableOpenMPGPU;
  langOpts.OpenMPForceUSM = enableOpenMPForceUSM;
  langOpts.OpenMPTargetDebug = setOpenMPTargetDebug;
  langOpts.OpenMPThreadSubscription = setOpenMPThreadSubscription;
  langOpts.OpenMPTeamSubscription = setOpenMPTeamSubscription;
  langOpts.OpenMPNoThreadState = setOpenMPNoThreadState;
  langOpts.OpenMPNoNestedParallelism = setOpenMPNoNestedParallelism;
  std::transform(targetTriplesOpenMP.begin(), targetTriplesOpenMP.end(),
                 std::back_inserter(langOpts.OMPTargetTriples),
                 [](const std::string &str) { return llvm::Triple(str); });

  // enable parsing of OpenMP
  if (enableOpenMP) {
    options.features.Enable(Fortran::common::LanguageFeature::OpenMP);
    Fortran::common::setOpenMPMacro(setOpenMPVersion, options.predefinitions);
  }

  // enable parsing of OpenACC
  if (enableOpenACC) {
    options.features.Enable(Fortran::common::LanguageFeature::OpenACC);
    options.predefinitions.emplace_back("_OPENACC", "202211");
  }

  // enable parsing of CUDA Fortran
  if (enableCUDA) {
    options.features.Enable(Fortran::common::LanguageFeature::CUDA);
  }
  if (enableCUDAInit) {
    options.features.Enable(Fortran::common::LanguageFeature::CUDAInit);
  }
  if (Opts->get<&BBC_WarnOnAllExtensions>()) {
    options.features.WarnOnAllNonstandard();
    options.features.WarnOnAllUsage();
  }

  if (enableDoConcurrentOffload) {
    options.features.Enable(
        Fortran::common::LanguageFeature::DoConcurrentOffload);
  }

  if (disableCUDAWarpFunction) {
    options.features.Enable(
        Fortran::common::LanguageFeature::CudaWarpMatchFunction, false);
  }

  if (enableGPUMode == "managed")
    options.features.Enable(Fortran::common::LanguageFeature::CudaManaged);
  else if (enableGPUMode == "unified")
    options.features.Enable(Fortran::common::LanguageFeature::CudaUnified);
  else if (enableGPUMode == "pinned")
    options.features.Enable(Fortran::common::LanguageFeature::CudaPinned);

  if (fixedForm) {
    options.isFixedForm = fixedForm;
  }

  Fortran::common::IntrinsicTypeDefaultKinds defaultKinds;
  Fortran::parser::AllSources allSources;
  Fortran::parser::AllCookedSources allCookedSources(allSources);
  Fortran::semantics::SemanticsContext semanticsContext{
      defaultKinds, options.features, langOpts, allCookedSources};
  semanticsContext.setOptionsContext(*OptsCtx);
  semanticsContext.set_moduleDirectory(moduleDir)
      .set_moduleFileSuffix(moduleSuffix)
      .set_searchDirectories(includeDirs)
      .set_intrinsicModuleDirectories(intrinsicIncludeDirs)
      .set_warnOnNonstandardUsage(warnStdViolation)
      .set_warningsAreErrors(warnIsError);

  std::string error;
  // Create host target machine.
  std::unique_ptr<llvm::TargetMachine> targetMachine =
      createTargetMachine(targetTripleOverride, error, *OptsCtx);
  if (!targetMachine) {
    llvm::errs() << "failed to create target machine: " << error << "\n";
    return mlir::failed(mlir::failure());
  }
  std::string compilerVersion = Fortran::common::getFlangToolFullVersion("bbc");
  std::string compilerOptions = "";
  Fortran::tools::setUpTargetCharacteristics(
      semanticsContext.targetCharacteristics(), *targetMachine, {},
      compilerVersion, compilerOptions);

  return mlir::failed(convertFortranSourceToMLIR(
      Opts->get<&BBC_InputFilename>(), options, programPrefix, semanticsContext,
      passPipe, *targetMachine, *OptsCtx));
}
