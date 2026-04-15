add_mlir_dialect_library(MLIROffloadTransforms
  SplitSingleSource.cpp
  LowerHostRuntime.cpp
  LowerSharedGlobals.cpp
  TightenLaunchBounds.cpp

  ADDITIONAL_HEADER_DIRS
  ${MLIR_MAIN_INCLUDE_DIR}/mlir/Dialect/Offload

  DEPENDS
  MLIROffloadPassIncGen
  MLIROffloadOpsIncGen

  LINK_LIBS PUBLIC
  MLIRArithDialect
  MLIRFuncDialect
  MLIRGPUDialect
  MLIRIR
  MLIRLLVMDialect
  MLIROffloadDialect
  MLIRPass
  MLIRSupport
  MLIRTransformUtils
)
