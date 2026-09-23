add_llvm_component_library(LLVMLanaiDesc
  LanaiAsmBackend.cpp
  LanaiOptions.cpp
  LanaiELFObjectWriter.cpp
  LanaiInstPrinter.cpp
  LanaiMCAsmInfo.cpp
  LanaiMCCodeEmitter.cpp
  LanaiMCTargetDesc.cpp

  LINK_COMPONENTS
  LanaiInfo
  MC
  MCDisassembler
  Option
  Support
  TargetParser

  ADD_TO_COMPONENT
  Lanai

  DEPENDS
  LanaiOptParserTableGen
)
