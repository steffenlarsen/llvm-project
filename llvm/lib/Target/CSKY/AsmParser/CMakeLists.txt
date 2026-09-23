add_llvm_component_library(LLVMCSKYAsmParser
  CSKYAsmParser.cpp

  DEPENDS
  CSKYOptParserTableGen

  LINK_COMPONENTS
  CSKYDesc
  CSKYInfo
  MC
  MCParser
  Support
  TargetParser

  ADD_TO_COMPONENT
  CSKY
  )
