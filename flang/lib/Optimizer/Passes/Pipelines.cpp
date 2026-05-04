//===-- Pipelines.cpp -- FIR pass pipelines ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

/// This file defines some utilties to setup FIR pass pipelines. These are
/// common to flang and the test tools.

#include "flang/Optimizer/Passes/Pipelines.h"
#include "flang/Common/FlangOptionsOptInfos.h"
#include "flang/Optimizer/Builder/MIFCommon.h"
#include "flang/Optimizer/Dialect/FIROps.h"
#include "flang/Optimizer/OpenACC/Passes.h"
#include "mlir/Conversion/Passes.h"
#include "mlir/Dialect/LLVMIR/Transforms/Passes.h"
#include "mlir/Dialect/OpenMP/Transforms/Passes.h"


namespace fir {

template <typename F>
void addNestedPassToAllTopLevelOperationsConditionally(mlir::PassManager &pm,
                                                       bool disabled, F ctor) {
  if (!disabled)
    addNestedPassToAllTopLevelOperations<F>(pm, ctor);
}

void addCanonicalizerPassWithoutRegionSimplification(mlir::OpPassManager &pm) {
  mlir::GreedyRewriteConfig config;
  config.setRegionSimplificationLevel(
      mlir::GreedySimplifyRegionLevel::Disabled);
  pm.addPass(mlir::createCanonicalizerPass(config));
}

void addCfgConversionPass(mlir::PassManager &pm,
                          const MLIRToLLVMPassPipelineConfig &config) {
  auto &optsCtx = pm.getContext()->getOptionsContext();
  fir::CFGConversionOptions options;
  if (!config.NSWOnLoopVarInc)
    options.setNSW = false;
  bool disabled = llvm::flang_opts::getDisableCfgConversion(optsCtx);
  addNestedPassToAllTopLevelOperationsConditionally(
      pm, disabled, [&]() { return createCFGConversion(options); });
}

void addMemoryAllocationOpt(mlir::PassManager &pm) {
  auto &optsCtx = pm.getContext()->getOptionsContext();
  bool disabled = llvm::flang_opts::getDisableMemoryAllocationOpt(optsCtx);
  bool dynHeap = llvm::flang_opts::getFdynamicHeapArray(optsCtx);
  std::size_t stackThreshold = llvm::flang_opts::getFstackArraySize(optsCtx);
  addNestedPassConditionally<mlir::func::FuncOp>(pm, disabled, [&]() {
    return fir::createMemoryAllocationOpt({dynHeap, stackThreshold});
  });
}

void addAllocationPlacement(mlir::PassManager &pm, bool stackArrays) {
  auto &optsCtx = pm.getContext()->getOptionsContext();
  fir::AllocationPlacementOptions options;
  options.stackArrays = stackArrays;
  options.smallArrayThresholdBytes =
      llvm::flang_opts::getAllocationPlacementSmallArraySize(optsCtx);
  options.totalStackLimitBytes =
      llvm::flang_opts::getAllocationPlacementStackLimit(optsCtx);
  pm.addPass(fir::createAllocationPlacement(options));
}

void addCodeGenRewritePass(mlir::PassManager &pm, bool preserveDeclare) {
  auto &optsCtx = pm.getContext()->getOptionsContext();
  fir::CodeGenRewriteOptions options;
  options.preserveDeclare = preserveDeclare;
  bool disabled = llvm::flang_opts::getDisableCodegenRewrite(optsCtx);
  addPassConditionally(pm, disabled,
                       [&]() { return fir::createCodeGenRewrite(options); });
}

void addTargetRewritePass(mlir::PassManager &pm) {
  auto &optsCtx = pm.getContext()->getOptionsContext();
  bool disabled = llvm::flang_opts::getDisableTargetRewrite(optsCtx);
  addPassConditionally(pm, disabled,
                       []() { return fir::createTargetRewritePass(); });
}

mlir::LLVM::DIEmissionKind
getEmissionKind(llvm::codegenoptions::DebugInfoKind kind) {
  switch (kind) {
  case llvm::codegenoptions::DebugInfoKind::FullDebugInfo:
    return mlir::LLVM::DIEmissionKind::Full;
  case llvm::codegenoptions::DebugInfoKind::DebugLineTablesOnly:
    return mlir::LLVM::DIEmissionKind::LineTablesOnly;
  case llvm::codegenoptions::DebugInfoKind::DebugDirectivesOnly:
    return mlir::LLVM::DIEmissionKind::DebugDirectivesOnly;
  default:
    return mlir::LLVM::DIEmissionKind::None;
  }
}

void addDebugInfoPass(mlir::PassManager &pm,
                      const MLIRToLLVMPassPipelineConfig &config,
                      llvm::StringRef inputFilename) {
  auto &optsCtx = pm.getContext()->getOptionsContext();
  bool disableFakeUse = llvm::flang_opts::getDisableArgumentFakeUse(optsCtx);
  fir::AddDebugInfoOptions options;
  options.debugLevel = getEmissionKind(config.DebugInfo);
  options.isOptimized = config.OptLevel != llvm::OptimizationLevel::O0;
  options.inputFilename = inputFilename;
  options.debugInfoForProfiling = config.DebugInfoForProfiling;
  options.dwarfVersion = config.DwarfVersion;
  options.splitDwarfFile = config.SplitDwarfFile;
  options.dwarfDebugFlags = config.DwarfDebugFlags;
  options.emitFakeUseForDebugVars =
      (config.OptLevel == llvm::OptimizationLevel::O0) && !disableFakeUse;
  bool disabled = llvm::flang_opts::getDisableDebugInfo(optsCtx);
  addPassConditionally(pm, disabled,
                       [&]() { return fir::createAddDebugInfoPass(options); });
}

fir::FIRToLLVMPassOptions
getFIRToLLVMPassOptions(const MLIRToLLVMPassPipelineConfig &config) {
  fir::FIRToLLVMPassOptions options;
  // These options are not context-dependent at this level; they are set
  // from the pipeline config and will be resolved at pipeline build time
  // via the PassManager's context.
  options.applyTBAA = config.AliasAnalysis;
  options.ComplexRange = config.ComplexRange;
  return options;
}

fir::FIRToLLVMPassOptions
getFIRToLLVMPassOptions(const MLIRToLLVMPassPipelineConfig &config,
                        const llvm::clv2::OptionsContext &optsCtx) {
  fir::FIRToLLVMPassOptions options;
  options.ignoreMissingTypeDescriptors =
      llvm::flang_opts::getIgnoreMissingTypeDesc(optsCtx);
  options.skipExternalRttiDefinition =
      llvm::flang_opts::getSkipExternalRttiDefinition(optsCtx);
  options.applyTBAA = config.AliasAnalysis;
  options.forceUnifiedTBAATree = llvm::flang_opts::getUseOldAliasTags(optsCtx);
  options.typeDescriptorsRenamedForAssembly =
      !llvm::flang_opts::getDisableCompilerGeneratedNames(optsCtx);
  options.ComplexRange = config.ComplexRange;
  return options;
}

void addFIRToLLVMPass(mlir::PassManager &pm,
                      const MLIRToLLVMPassPipelineConfig &config) {
  auto &optsCtx = pm.getContext()->getOptionsContext();
  fir::FIRToLLVMPassOptions options = getFIRToLLVMPassOptions(config, optsCtx);
  bool disabled = llvm::flang_opts::getDisableFirToLlvmir(optsCtx);
  addPassConditionally(pm, disabled,
                       [&]() { return fir::createFIRToLLVMPass(options); });
  // The dialect conversion framework may leave dead unrealized_conversion_cast
  // ops behind, so run reconcile-unrealized-casts to clean them up.
  addPassConditionally(pm, disabled, [&]() {
    return mlir::createReconcileUnrealizedCastsPass();
  });
}

void addLLVMDialectToLLVMPass(mlir::PassManager &pm,
                              llvm::raw_ostream &output) {
  auto &optsCtx = pm.getContext()->getOptionsContext();
  bool disabled = llvm::flang_opts::getDisableLlvm(optsCtx);
  addPassConditionally(
      pm, disabled, [&]() { return fir::createLLVMDialectToLLVMPass(output); });
}

void addBoxedProcedurePass(mlir::PassManager &pm,
                           bool enableSafeTrampolineFromConfig) {
  auto &optsCtx = pm.getContext()->getOptionsContext();
  bool disabled = llvm::flang_opts::getDisableBoxedProcedureRewrite(optsCtx);
  bool safeTrampolineOpt = llvm::flang_opts::getEnableSafeTrampoline(optsCtx);
  addPassConditionally(pm, disabled, [&]() {
    fir::BoxedProcedurePassOptions opts;
    // Support both the frontend -fsafe-trampoline flag (via config)
    // and the cl::opt --safe-trampoline (for fir-opt/tco tools).
    opts.useSafeTrampoline =
        enableSafeTrampolineFromConfig || safeTrampolineOpt;
    return fir::createBoxedProcedurePass(opts);
  });
}

void addExternalNameConversionPass(mlir::PassManager &pm,
                                   bool appendUnderscore) {
  auto &optsCtx = pm.getContext()->getOptionsContext();
  bool disabled = llvm::flang_opts::getDisableExternalNameInterop(optsCtx);
  addPassConditionally(pm, disabled, [&]() {
    return fir::createExternalNameConversion({appendUnderscore});
  });
}

void addCompilerGeneratedNamesConversionPass(mlir::PassManager &pm) {
  auto &optsCtx = pm.getContext()->getOptionsContext();
  bool disabled = llvm::flang_opts::getDisableCompilerGeneratedNames(optsCtx);
  addPassConditionally(pm, disabled, [&]() {
    return fir::createCompilerGeneratedNamesConversion();
  });
}

// Use inliner extension point callback to register the default inliner pass.
void registerDefaultInlinerPass(MLIRToLLVMPassPipelineConfig &config) {
  config.registerFIRInlinerCallback(
      [](mlir::PassManager &pm, llvm::OptimizationLevel level) {
        llvm::StringMap<mlir::OpPassManager> pipelines;
        // The default inliner pass adds the canonicalizer pass with the default
        // configuration.
        pm.addPass(mlir::createInlinerPass(
            pipelines, addCanonicalizerPassWithoutRegionSimplification));
      });
}

void createDefaultFIRPreCFGOptimizerPassPipeline(
    mlir::PassManager &pm, MLIRToLLVMPassPipelineConfig &pc) {
  auto &optsCtx = pm.getContext()->getOptionsContext();

  // simplify the IR
  mlir::GreedyRewriteConfig config;
  config.setRegionSimplificationLevel(
      mlir::GreedySimplifyRegionLevel::Disabled);
  pm.addPass(mlir::createCSEPass());
  addNestedPassToAllTopLevelOperations<PassConstructor>(
      pm, fir::createCharacterConversion);
  pm.addPass(mlir::createCanonicalizerPass(config));
  pm.addPass(fir::createSimplifyRegionLite());
  if (pc.OptLevel != llvm::OptimizationLevel::O0) {
    // These passes may increase code size.
    pm.addPass(fir::createSimplifyIntrinsics());
    pm.addPass(fir::createAlgebraicSimplificationPass(config));
    if (llvm::flang_opts::getEnableConstantArgumentGlobalisation(optsCtx))
      pm.addPass(fir::createConstantArgumentGlobalisationOpt());
  }

  if (pc.LoopVersioning)
    pm.addPass(fir::createLoopVersioning());

  pm.addPass(mlir::createCSEPass());

  // Unconditional and ahead of the array allocation placement below: under
  // -gpu=mem:unified|managed the unified/managed allocators are required for
  // correctness, so this must not depend on which placement pass is selected
  // or on -disable-memory-allocation-opt.
  pm.addPass(fir::createCudaHeapAllocPromotion(
      fir::CudaHeapAllocPromotionOptions{pc.StackArrays}));

  if (llvm::flang_opts::getEnableAllocationPlacement(optsCtx))
    fir::addAllocationPlacement(pm, pc.StackArrays);
  else if (pc.StackArrays)
    pm.addPass(fir::createStackArrays());
  else
    fir::addMemoryAllocationOpt(pm);

  // FIR Inliner Callback
  pc.invokeFIRInlinerCallback(pm, pc.OptLevel);

  pm.addPass(fir::createSimplifyRegionLite());
  pm.addPass(mlir::createCSEPass());

  // Run LICM after CSE, which may reduce the number of operations to hoist.
  if (llvm::flang_opts::getEnableFirLicm(optsCtx) &&
      pc.OptLevel != llvm::OptimizationLevel::O0)
    pm.addPass(fir::createLoopInvariantCodeMotion());

  // Polymorphic types
  pm.addPass(fir::createPolymorphicOpConversion());
  pm.addPass(fir::createSelectOpsConversion());
  pm.addPass(fir::createAssumedRankOpConversion());

  // Optimize redundant array repacking operations,
  // if the source is known to be contiguous.
  if (pc.OptLevel != llvm::OptimizationLevel::O0)
    pm.addPass(fir::createOptimizeArrayRepacking());
  pm.addPass(fir::createLowerRepackArraysPass());
  // Expand FIR operations that may use SCF dialect for their
  // implementation. This is a mandatory pass.
  pm.addPass(fir::createSimplifyFIROperations(
      {/*preferInlineImplementation=*/pc.OptLevel !=
       llvm::OptimizationLevel::O0}));

  addNestedPassToAllTopLevelOperations<PassConstructor>(
      pm, fir::createStackReclaim);
}

void createDefaultFIRPostCFGOptimizerPassPipeline(
    mlir::PassManager &pm, MLIRToLLVMPassPipelineConfig &pc) {
  mlir::GreedyRewriteConfig config;
  config.setRegionSimplificationLevel(
      mlir::GreedySimplifyRegionLevel::Disabled);

  pm.addPass(mlir::createSCFToControlFlowPass());

  pm.addPass(mlir::createCanonicalizerPass(config));
  pm.addPass(fir::createSimplifyRegionLite());
  if (!pc.SkipConvertComplexPow)
    pm.addPass(fir::createConvertComplexPow());
  pm.addPass(mlir::createCSEPass());

  if (pc.OptLevel != llvm::OptimizationLevel::O0)
    pm.addPass(fir::createSetRuntimeCallAttributes());
}

/// Create a pass pipeline for running default optimization passes for
/// incremental conversion of FIR.
///
/// \param pm - MLIR pass manager that will hold the pipeline definition
void createDefaultFIROptimizerPassPipeline(mlir::PassManager &pm,
                                           MLIRToLLVMPassPipelineConfig &pc) {
  pc.invokeFIROptEarlyEPCallbacks(pm, pc.OptLevel);
  createDefaultFIRPreCFGOptimizerPassPipeline(pm, pc);
  fir::addCfgConversionPass(pm, pc);
  createDefaultFIRPostCFGOptimizerPassPipeline(pm, pc);
  pc.invokeFIROptLastEPCallbacks(pm, pc.OptLevel);
}

/// Create a pass pipeline for lowering from HLFIR to FIR
///
/// \param pm - MLIR pass manager that will hold the pipeline definition
/// \param enableOpenMP - whether OpenMP lowering is enabled
/// \param config - pipeline config (OptLevel, etc.)
void createHLFIRToFIRPassPipeline(mlir::PassManager &pm,
                                  EnableOpenMP enableOpenMP,
                                  const MLIRToLLVMPassPipelineConfig &config) {
  llvm::OptimizationLevel optLevel = config.OptLevel;

  config.invokeHLFIROptEarlyEPCallbacks(pm, optLevel);

  if (optLevel != llvm::OptimizationLevel::O0) {
    addNestedPassToAllTopLevelOperations<PassConstructor>(
        pm, hlfir::createExpressionSimplification);
    addCanonicalizerPassWithoutRegionSimplification(pm);
    addNestedPassToAllTopLevelOperations(pm, [&]() {
      return hlfir::createSimplifyHLFIRIntrinsics(
          {/*allowNewSideEffects=*/false, config.fpMaxminBehavior});
    });
  }
  addNestedPassToAllTopLevelOperations<PassConstructor>(
      pm, hlfir::createInlineElementals);
  addNestedPassToAllTopLevelOperations<PassConstructor>(
      pm, hlfir::createSeparateAllocatableAssign);
  if (optLevel != llvm::OptimizationLevel::O0) {
    addCanonicalizerPassWithoutRegionSimplification(pm);
    pm.addPass(mlir::createCSEPass());
    // Run SimplifyHLFIRIntrinsics pass late after CSE,
    // and allow introducing operations with new side effects.
    addNestedPassToAllTopLevelOperations(pm, [&]() {
      return hlfir::createSimplifyHLFIRIntrinsics(
          {/*allowNewSideEffects=*/true, config.fpMaxminBehavior});
    });
    addNestedPassToAllTopLevelOperations<PassConstructor>(
        pm, hlfir::createPropagateFortranVariableAttributes);
    addNestedPassToAllTopLevelOperations<PassConstructor>(
        pm, hlfir::createOptimizedBufferization);
    addNestedPassToAllTopLevelOperations<PassConstructor>(
        pm, hlfir::createInlineHLFIRAssign);

    if (optLevel == llvm::OptimizationLevel::O3) {
      addNestedPassToAllTopLevelOperations<PassConstructor>(
          pm, hlfir::createInlineHLFIRCopy);
    }
  } else if (config.EnableOpenMPIsTargetDevice) {
    // At O0, only inline scalar-to-array broadcasts when compiling for an
    // OpenMP target device. This avoids emitting Fortran runtime calls
    // (e.g. _FortranAAssign) that use malloc/free in device code generated
    // by OpenMP target offloading. Restricting this to target-device
    // compilation preserves the runtime call on the host at -O0 so that a
    // line breakpoint on a scalar-to-array assignment hits once instead of
    // once per element.
    addNestedPassToAllTopLevelOperations(pm, [&]() {
      return hlfir::createInlineHLFIRAssign({/*onlyScalarRHS=*/true});
    });
  }
  pm.addPass(hlfir::createLowerHLFIROrderedAssignments(
      {/*tryFusingAssignments=*/optLevel != llvm::OptimizationLevel::O0}));

  config.invokeHLFIROptLastEPCallbacks(pm, optLevel);

  pm.addPass(hlfir::createLowerHLFIRIntrinsics());

  hlfir::BufferizeHLFIROptions bufferizeOptions;
  // For opt-for-speed, avoid running any of the loops resulting
  // from hlfir.elemental lowering, if the result is an empty array.
  // This helps to avoid long running loops for elementals with
  // shapes like (0, HUGE).
  if (optLevel != llvm::OptimizationLevel::O0)
    bufferizeOptions.optimizeEmptyElementals = true;
  pm.addPass(hlfir::createBufferizeHLFIR(bufferizeOptions));
  // Run hlfir.assign inlining again after BufferizeHLFIR,
  // because the latter may introduce new hlfir.assign operations,
  // e.g. for copying an array into a temporary due to
  // hlfir.associate.
  // TODO: we can remove the previous InlineHLFIRAssign, when
  // FIR AliasAnalysis is good enough to say that a temporary
  // array does not alias with any user object.
  if (optLevel != llvm::OptimizationLevel::O0)
    addNestedPassToAllTopLevelOperations<PassConstructor>(
        pm, hlfir::createInlineHLFIRAssign);
  pm.addPass(hlfir::createConvertHLFIRtoFIR());
  if (enableOpenMP != EnableOpenMP::None) {
    pm.addPass(flangomp::createLowerWorkshare());
    pm.addPass(flangomp::createLowerWorkdistribute());
  }
  if (enableOpenMP == EnableOpenMP::Simd)
    pm.addPass(flangomp::createSimdOnlyPass());
}

/// Create a pass pipeline for handling certain OpenMP transformations needed
/// prior to FIR lowering.
///
/// WARNING: These passes must be run immediately after the lowering to ensure
/// that the FIR is correct with respect to OpenMP operations/attributes.
///
/// \param pm - MLIR pass manager that will hold the pipeline definition.
/// \param isTargetDevice - Whether code is being generated for a target device
/// rather than the host device.
void createOpenMPFIRPassPipeline(mlir::PassManager &pm,
                                 OpenMPFIRPassPipelineOpts opts) {
  using DoConcurrentMappingKind =
      Fortran::frontend::CodeGenOptions::DoConcurrentMappingKind;

  if (opts.doConcurrentMappingKind != DoConcurrentMappingKind::DCMK_None)
    pm.addPass(flangomp::createDoConcurrentConversionPass(
        opts.doConcurrentMappingKind == DoConcurrentMappingKind::DCMK_Device));

  // The MapsForPrivatizedSymbols and AutomapToTargetDataPass pass need to run
  // before MapInfoFinalizationPass because they create new MapInfoOp
  // instances, typically for descriptors. MapInfoFinalizationPass adds
  // MapInfoOp instances for the descriptors underlying data which is necessary
  // to access the data on the offload target device.
  pm.addPass(flangomp::createMapsForPrivatizedSymbolsPass());
  pm.addPass(flangomp::createAutomapToTargetDataPass());
  pm.addPass(flangomp::createMapInfoFinalizationPass());

  pm.addPass(flangomp::createGenericLoopConversionPass());
}

void createDebugPasses(mlir::PassManager &pm,
                       const MLIRToLLVMPassPipelineConfig &config,
                       llvm::StringRef inputFilename) {
  if (config.DebugInfo != llvm::codegenoptions::NoDebugInfo)
    addDebugInfoPass(pm, config, inputFilename);
}

void createDefaultFIRCodeGenPassPipeline(mlir::PassManager &pm,
                                         MLIRToLLVMPassPipelineConfig config,
                                         llvm::StringRef inputFilename) {
  auto &optsCtx = pm.getContext()->getOptionsContext();

  pm.addPass(fir::createMIFOpConversion());
  fir::addBoxedProcedurePass(pm, config.EnableSafeTrampoline);
  if (config.OptLevel != llvm::OptimizationLevel::O0 && config.AliasAnalysis &&
      !llvm::flang_opts::getDisableFirAliasTags(optsCtx) &&
      !llvm::flang_opts::getUseOldAliasTags(optsCtx))
    pm.addPass(fir::createAddAliasTags());
  addNestedPassToAllTopLevelOperations<PassConstructor>(
      pm, fir::createAbstractResultOpt);
  addPassToGPUModuleOperations<PassConstructor>(pm,
                                                fir::createAbstractResultOpt);
  pm.addPass(fir::createRematerializeFIRBoxOpsPass());
  // Do not run CSE between rematerialization and FIR-to-LLVM lowering. CSE will
  // undo the createRematerializeFIRBoxOps pass.
  // LLVM-level CSE can clean up redundant operations after FIR box conversion
  // has materialized region-local allocas.
  fir::addCodeGenRewritePass(
      pm, (config.DebugInfo != llvm::codegenoptions::NoDebugInfo));
  fir::addExternalNameConversionPass(pm, config.Underscoring);
  fir::createDebugPasses(pm, config, inputFilename);
  fir::addTargetRewritePass(pm);
  fir::addCompilerGeneratedNamesConversionPass(pm);

  if (config.VScaleMin != 0)
    pm.addPass(fir::createVScaleAttr({config.VScaleMin, config.VScaleMax}));

  // Add function attributes
  mlir::LLVM::framePointerKind::FramePointerKind framePointerKind;

  if (config.FramePointerKind == llvm::FramePointerKind::NonLeaf)
    framePointerKind = mlir::LLVM::framePointerKind::FramePointerKind::NonLeaf;
  else if (config.FramePointerKind == llvm::FramePointerKind::All)
    framePointerKind = mlir::LLVM::framePointerKind::FramePointerKind::All;
  else if (config.FramePointerKind == llvm::FramePointerKind::Reserved)
    framePointerKind = mlir::LLVM::framePointerKind::FramePointerKind::Reserved;
  else if (config.FramePointerKind == llvm::FramePointerKind::NonLeafNoReserve)
    framePointerKind =
        mlir::LLVM::framePointerKind::FramePointerKind::NonLeafNoReserve;
  else
    framePointerKind = mlir::LLVM::framePointerKind::FramePointerKind::None;

  // TODO: re-enable setNoAlias by default (when optimizing for speed) once
  // function specialization is fixed.
  bool setNoAlias = llvm::flang_opts::getForceNoAlias(optsCtx);
  bool setNoCapture = config.OptLevel != llvm::OptimizationLevel::O0;
  bool setReadOnly = config.OptLevel != llvm::OptimizationLevel::O0;

  pm.addPass(fir::createFunctionAttr(
      {framePointerKind, config.InstrumentFunctionEntry,
       config.InstrumentFunctionExit, config.NoInfsFPMath, config.NoNaNsFPMath,
       config.ApproxFuncFPMath, config.NoSignedZerosFPMath, config.UnsafeFPMath,
       config.Reciprocals, config.PreferVectorWidth, config.UseSampleProfile,
       /*tuneCPU=*/"", setNoCapture, setNoAlias, setReadOnly}));

  if (config.EnableOpenMP) {
    pm.addNestedPass<mlir::func::FuncOp>(
        flangomp::createLowerNontemporalPass());
  }

  bool runOMPNonSimdPasses = config.EnableOpenMP && !config.EnableOpenMPSimd;
  if (runOMPNonSimdPasses) {
    // Propagate implicit declare target information early in order to diagnose
    // target device not-yet-implemented cases based on FIR.
    pm.addPass(mlir::omp::createMarkDeclareTargetPass());
    pm.addPass(flangomp::createUnimplementedDeviceCheckPass());
  }

  fir::addFIRToLLVMPass(pm, config);
  pm.addPass(fir::createEmitMIFGlobalCtors());

  if (runOMPNonSimdPasses) {
    // Since some math operations may be converted to function calls by the
    // ConvertMathToFuncs pass, we need to run the implicit declare_target
    // propagation and dependent passes late in the pipeline.
    pm.addPass(mlir::omp::createMarkDeclareTargetPass());

    // First remove host-only functions from target device modules, and then
    // clean up any remaining host functions holding target regions to only
    // contain the bare minimum host operations needed for target device
    // compilation. These passes must always run back to back to ensure no
    // temporary poison values, introduced by the first pass, cause other passes
    // to encounter UB before the second pass removes them.
    pm.addPass(mlir::omp::createFunctionFilteringPass());
    pm.addPass(mlir::omp::createHostOpFilteringPass());

    // Convert applicable OpenMP stack allocations to shared memory allocations
    // for GPU targets. This pass must run after any alloca-generating passes to
    // ensure all are adequately accounted for.
    pm.addPass(mlir::omp::createStackToSharedPass());
  }
}

/// Create a pass pipeline for lowering from MLIR to LLVM IR
///
/// \param pm - MLIR pass manager that will hold the pipeline definition
/// \param optLevel - optimization level used for creating FIR optimization
///   passes pipeline
void createMLIRToLLVMPassPipeline(mlir::PassManager &pm,
                                  MLIRToLLVMPassPipelineConfig &config,
                                  llvm::StringRef inputFilename) {
  if (config.EnableOpenACC)
    fir::acc::populateHLFIROpenACCPassPipeline(pm);

  auto &optsCtx = pm.getContext()->getOptionsContext();

  fir::EnableOpenMP enableOpenMP = fir::EnableOpenMP::None;
  if (config.EnableOpenMP)
    enableOpenMP = fir::EnableOpenMP::Full;
  if (config.EnableOpenMPSimd)
    enableOpenMP = fir::EnableOpenMP::Simd;
  fir::createHLFIRToFIRPassPipeline(pm, enableOpenMP, config);

  // Add default optimizer pass pipeline.
  fir::createDefaultFIROptimizerPassPipeline(pm, config);

  // Add codegen pass pipeline.
  fir::createDefaultFIRCodeGenPassPipeline(pm, config, inputFilename);

  // Run a pass to prepare for translation of delayed privatization in the
  // context of deferred target tasks.
  bool disabled = llvm::flang_opts::getDisableFirToLlvmir(optsCtx);
  addPassConditionally(pm, disabled, [&]() {
    return mlir::omp::createPrepareForOMPOffloadPrivatizationPass();
  });
}

/// Register the passes used in flang's MLIR pass pipeline so that
/// --mlir-print-ir-before=<pass> and --mlir-print-ir-after=<pass> work.
/// Must be called BEFORE mlir::registerPassManagerCLOptions() because
/// that function creates the PassNameCLParser which snapshots the pass
/// registry during initialization.
void registerFlangPipelinePasses() {
  llvm::clv2::registerDynamicRegistry<&llvm::clv2::FlangOptsReg>();

  // MLIR core passes used in the pipeline.
  mlir::registerCSEPass();
  mlir::registerCanonicalizerPass();
  mlir::registerInlinerPass();

  // MLIR conversion passes used in the pipeline.
  mlir::registerSCFToControlFlowPass();
  mlir::registerConvertMathToFuncs();
  mlir::registerConvertComplexToStandardPass();
  mlir::registerConvertMathToLLVMPass();
  mlir::LLVM::registerLLVMAddComdats();
  mlir::registerReconcileUnrealizedCastsPass();

  // FIR, HLFIR, and OpenMP passes.
  fir::registerOptCodeGenPasses();
  fir::registerOptTransformPasses();
  hlfir::registerHLFIRPasses();
  flangomp::registerFlangOpenMPPasses();
  fir::acc::registerFIROpenACCPasses();
}

} // namespace fir
