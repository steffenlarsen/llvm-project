add_subdirectory(TargetLowering)

add_clang_library(MLIRCIRTransforms
  CallConvLoweringPass.cpp
  CIRCanonicalize.cpp
  CIRSimplify.cpp
  CIRTransformUtils.cpp
  MergeOffloadModules.cpp
  CXXABILowering.cpp
  EHABILowering.cpp
  TargetLowering.cpp
  FlattenCFG.cpp
  HoistAllocas.cpp
  LoweringPrepare.cpp
  GotoSolver.cpp
  IdiomRecognizer.cpp
  LibOpt.cpp
  OffloadDevirtualizeLaunches.cpp
  OffloadSpecializeLaunchWrappers.cpp
  OffloadPropagateBlockShape.cpp
  OffloadTightenLaunchBounds.cpp
  OffloadPropagatePointerFacts.cpp
  OffloadSpecializeScalarArgs.cpp
  OffloadEliminateCoveredGuards.cpp
  OffloadMultiversionDivisibility.cpp
  OffloadSpecializeSharedMemory.cpp
  OffloadPropagateGridCoverage.cpp
  OffloadDeadArgElimination.cpp
  OffloadAnnotateLoopInvariantArgs.cpp
  OffloadPromoteConstantArgs.cpp
  OffloadUnrollBarrierLoops.cpp

  DEPENDS
  MLIRCIRPassIncGen

  LINK_LIBS PUBLIC
  clangAST
  clangBasic

  LLVMABI
  MLIRABI
  MLIRAnalysis
  MLIRDLTIDialect
  MLIRIR
  MLIRPass
  MLIRTransformUtils

  MLIRCIR
  MLIRCIRInterfaces
  MLIRCIRTargetLowering
  MLIRGPUDialect
  MLIRParser
  MLIROpenACCDialect
  MLIROpenMPDialect
)
