add_mlir_dialect_library(MLIROffloadDialect
  OffloadDialect.cpp

  ADDITIONAL_HEADER_DIRS
  ${MLIR_MAIN_INCLUDE_DIR}/mlir/Dialect/Offload

  DEPENDS
  MLIROffloadOpsIncGen
  MLIROffloadOpsEnumsGen
  MLIROffloadOpsAttributesIncGen
  MLIROffloadOpsTypesIncGen

  LINK_LIBS PUBLIC
  MLIRGPUDialect
  MLIRFunctionInterfaces
  MLIRIR
  MLIRSideEffectInterfaces
  MLIRSupport
)
