add_llvm_component_library(LLVMFrontendOpenMP
  OMP.cpp
  OMPContext.cpp
  OMPDescriptors.cpp
  OMPIRBuilder.cpp
  OpenMPOptions.cpp
  DirectiveNameParser.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Frontend
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Frontend/OpenMP

  DEPENDS
  intrinsics_gen
  omp_gen
  OpenMPOptParserTableGen

  LINK_COMPONENTS
  Core
  Support
  TargetParser
  TransformUtils
  Analysis
  Demangle
  MC
  Option
  Scalar
  BitReader
  FrontendOffloading
  FrontendAtomic
  FrontendDirective
  )
