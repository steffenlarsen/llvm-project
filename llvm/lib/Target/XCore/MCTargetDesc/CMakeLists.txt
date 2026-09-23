add_llvm_component_library(LLVMXCoreDesc
  XCoreInstPrinter.cpp
  XCoreMCTargetDesc.cpp
  XCoreMCAsmInfo.cpp
  XCoreOptions.cpp

  LINK_COMPONENTS
  MC
  Option
  Support
  XCoreInfo

  ADD_TO_COMPONENT
  XCore

  DEPENDS
  XCoreOptParserTableGen
  )
