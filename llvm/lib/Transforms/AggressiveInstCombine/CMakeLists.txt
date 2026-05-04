add_llvm_component_library(LLVMAggressiveInstCombine
  AggressiveInstCombineOptionsGetters.cpp
  AggressiveInstCombineOptionsRegistration.cpp
  AggressiveInstCombine.cpp
  AggressiveInstCombineOptions.cpp
  TruncInstCombine.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms/AggressiveInstCombine

  DEPENDS
  intrinsics_gen

  LINK_COMPONENTS
  Analysis
  Core
  Support
  TransformUtils
  )
