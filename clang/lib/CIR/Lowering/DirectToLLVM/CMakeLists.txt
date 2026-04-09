set(LLVM_LINK_COMPONENTS
  Core
  Support
  )

get_property(dialect_libs GLOBAL PROPERTY MLIR_DIALECT_LIBS)

add_clang_library(clangCIRLoweringDirectToLLVM
  LowerToLLVM.cpp
  LowerToLLVMIR.cpp

  DEPENDS
  CIRLowering
  MLIRCIREnumsGen
  MLIRCIROpsIncGen
  MLIRCIROpInterfacesIncGen

  LINK_LIBS
  clangCIRLoweringCommon
  ${dialect_libs}
  MLIRCIR
  MLIRCIRTargetLowering
  MLIRBuiltinToLLVMIRTranslation
  MLIRGPUToLLVMIRTranslation
  MLIRArithToLLVM
  MLIRControlFlowToLLVM
  MLIRFuncToLLVM
  MLIRGPUToROCDLTransforms
  MLIRGPUTransforms
  MLIRIndexToLLVM
  MLIRMathToLLVM
  MLIRMemRefToLLVM
  MLIROpenMPToLLVM
  MLIRUBToLLVM
  MLIRVectorToLLVM
  MLIRReconcileUnrealizedCasts
  MLIRLLVMToLLVMIRTranslation
  MLIROpenMPToLLVMIRTranslation
  MLIROpenMPTransforms
  MLIRROCDLTarget
  MLIRROCDLToLLVMIRTranslation
  MLIRIR
  )

target_include_directories(clangCIRLoweringDirectToLLVM PRIVATE
  ${CLANG_SOURCE_DIR}/lib/CIR/Dialect/Transforms/TargetLowering
  )
