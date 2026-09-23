add_llvm_component_library(LLVMRemarks
  RemarksOptions.cpp
  BitstreamRemarkParser.cpp
  BitstreamRemarkSerializer.cpp
  Remark.cpp
  RemarkFormat.cpp
  RemarkLinker.cpp
  RemarkParser.cpp
  RemarkSerializer.cpp
  RemarkStreamer.cpp
  RemarkStringTable.cpp
  YAMLRemarkParser.cpp
  YAMLRemarkSerializer.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Remarks

  LINK_COMPONENTS
  BitstreamReader
  Option
  Support

  DEPENDS
  intrinsics_gen
  RemarksOptParserTableGen
)
