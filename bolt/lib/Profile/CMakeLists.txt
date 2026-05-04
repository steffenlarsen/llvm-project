add_llvm_library(LLVMBOLTProfile
  BoltProfileOptionsGetters.cpp
  BoltAddressTranslation.cpp
  BoltProfileOptions.cpp
  DataAggregator.cpp
  DataReader.cpp
  Heatmap.cpp
  StaleProfileMatching.cpp
  YAMLProfileReader.cpp
  YAMLProfileWriter.cpp

  NO_EXPORT
  DISABLE_LLVM_LINK_LLVM_DYLIB

  LINK_LIBS
  ${LLVM_PTHREAD_LIB}

  LINK_COMPONENTS
  BinaryFormat
  Demangle
  MC
  Support
  TransformUtils
  )

target_link_libraries(LLVMBOLTProfile
  PRIVATE
  LLVMBOLTCore
  LLVMBOLTUtils
  )
