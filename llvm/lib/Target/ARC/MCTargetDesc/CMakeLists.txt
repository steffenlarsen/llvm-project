add_llvm_component_library(LLVMARCDesc
  ARCInstPrinter.cpp
  ARCOptions.cpp
  ARCMCTargetDesc.cpp
  ARCMCAsmInfo.cpp

  LINK_COMPONENTS
  MC
  Option
  Support
  ARCInfo

  ADD_TO_COMPONENT
  ARC

  DEPENDS
  ARCOptParserTableGen
  )
