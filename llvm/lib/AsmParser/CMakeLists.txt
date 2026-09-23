# AsmParser
add_llvm_component_library(LLVMAsmParser
  AsmParserOptions.cpp
  AsmParserContext.cpp
  LLLexer.cpp
  LLParser.cpp
  Parser.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/ASMParser

  DEPENDS
  intrinsics_gen
  AsmParserOptParserTableGen

  LINK_COMPONENTS
  BinaryFormat
  Core
  Option
  Support
  TargetParser
  )
