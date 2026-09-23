add_llvm_component_library(LLVMAllOptions
  AllOptions.cpp

  LINK_COMPONENTS
  AggressiveInstCombine
  Analysis
  AsmParser
  BitReader
  CGData
  CodeGen
  Core
  Coroutines
  FrontendOpenMP
  IPO
  InstCombine
  Instrumentation
  LTO
  MC
  ObjCARCOpts
  Object
  Passes
  ProfileData
  Remarks
  Scalar
  Support
  TransformUtils
  Vectorize
  )

# Polly's clv2 options are registered from AllOptions.cpp behind
# LINK_POLLY_INTO_TOOLS.  Nothing defined that macro, so -polly-* flags were
# rejected by every tool even though Polly was statically linked in.
if (LLVM_POLLY_LINK_INTO_TOOLS)
  target_compile_definitions(LLVMAllOptions PRIVATE LINK_POLLY_INTO_TOOLS)
  target_include_directories(LLVMAllOptions PRIVATE
    ${LLVM_MAIN_SRC_DIR}/../polly/include
    ${LLVM_BINARY_DIR}/tools/polly/include)
endif()
