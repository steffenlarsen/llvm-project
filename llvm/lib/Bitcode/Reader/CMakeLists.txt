add_llvm_component_library(LLVMBitReader
  BitcodeMemProfOptions.cpp
  BitcodeAnalyzer.cpp
  BitcodeOptions.cpp
  BitReader.cpp
  BitcodeReader.cpp
  MetadataLoader.cpp
  ValueList.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Bitcode

  DEPENDS
  intrinsics_gen
  BitcodeMemProfOptParserTableGen
  BitcodeOptParserTableGen

  LINK_COMPONENTS
  BitstreamReader
  Core
  Option
  Support
  TargetParser
  )
