add_mlir_dialect_library(MLIROffloadTransforms
  SplitSingleSource.cpp
  LowerHostRuntime.cpp

  ADDITIONAL_HEADER_DIRS
  ${MLIR_MAIN_INCLUDE_DIR}/mlir/Dialect/Offload

  DEPENDS
  MLIROffloadPassIncGen
  MLIROffloadOpsIncGen

  LINK_LIBS PUBLIC
  MLIRFuncDialect
  MLIRGPUDialect
  MLIRIR
  MLIROffloadDialect
  MLIRPass
  MLIRSupport
  MLIRTransformUtils
)
