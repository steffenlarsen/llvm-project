add_subdirectory(TargetLowering)
add_subdirectory(OffloadOpt)

add_clang_library(MLIRCIRTransforms
  CallConvLoweringPass.cpp
  CIRCanonicalize.cpp
  CIRSimplify.cpp
  CIRTransformUtils.cpp
  CUDARegistration.cpp
  CXXABILowering.cpp
  EHABILowering.cpp
  TargetLowering.cpp
  FlattenCFG.cpp
  HoistAllocas.cpp
  ResolveIndirectCalls.cpp
  LoweringPrepare.cpp
  GotoSolver.cpp
  IdiomRecognizer.cpp
  LibOpt.cpp

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
  MLIROpenACCDialect
  MLIROpenMPDialect
)
