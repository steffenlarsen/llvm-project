add_llvm_component_library(LLVMSparcDesc
  SparcAsmBackend.cpp
  SparcOptions.cpp
  SparcELFObjectWriter.cpp
  SparcInstPrinter.cpp
  SparcMCAsmInfo.cpp
  SparcMCCodeEmitter.cpp
  SparcMCTargetDesc.cpp
  SparcMCExpr.cpp
  SparcTargetStreamer.cpp

  LINK_COMPONENTS
  MC
  Option
  SparcInfo
  Support
  TargetParser

  ADD_TO_COMPONENT
  Sparc

  DEPENDS
  SparcOptParserTableGen
  )
