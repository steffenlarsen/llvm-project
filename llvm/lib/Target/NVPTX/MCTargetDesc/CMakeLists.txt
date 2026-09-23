add_llvm_component_library(LLVMNVPTXDesc
  NVPTXInstPrinter.cpp
  NVPTXOptions.cpp
  NVPTXMCAsmInfo.cpp
  NVPTXMCTargetDesc.cpp
  NVPTXTargetStreamer.cpp

  LINK_COMPONENTS
  MC
  NVPTXInfo
  Option
  Support
  TargetParser

  ADD_TO_COMPONENT
  NVPTX

  DEPENDS
  NVPTXOptParserTableGen
  )
