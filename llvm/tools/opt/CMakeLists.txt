set(LLVM_LINK_COMPONENTS
  AllOptions
  AllTargetsAsmParsers
  AllTargetsCodeGens
  AllTargetsDescs
  AllTargetsInfos
  AggressiveInstCombine
  Analysis
  AsmParser
  BitWriter
  CFGuard
  CodeGen
  Core
  Coroutines
  Extensions
  IPO
  IRReader
  IRPrinter
  InstCombine
  Instrumentation
  MC
  ObjCARCOpts
  Plugins
  Remarks
  ScalarOpts
  Support
  Target
  TargetParser
  TransformUtils
  Vectorize
  Passes
  LTO
  )

# We don't want to link this into libLLVM
add_llvm_library(LLVMOptDriver
  STATIC
  NewPMDriver.cpp
  optdriver.cpp
  PARTIAL_SOURCES_INTENDED
  DEPENDS
  intrinsics_gen
  AArch64OptParserTableGen
  AMDGPUOptParserTableGen
  BPFOptParserTableGen
  BitcodeMemProfOptParserTableGen
  CGDataOptParserTableGen
  CodeGenAsmPrintOptParserTableGen
  CodeGenCore1OptParserTableGen
  CodeGenCore2OptParserTableGen
  CodeGenGISelOptParserTableGen
  CodeGenMachine1OptParserTableGen
  CodeGenMachine2OptParserTableGen
  CodeGenRegAllocOptParserTableGen
  CodeGenSched1OptParserTableGen
  CodeGenSched2OptParserTableGen
  CodeGenSelDAGOptParserTableGen
  HexagonOptParserTableGen
  InstCombineOptParserTableGen
  InstrumentationOptParserTableGen
  IPOOptParserTableGen
  ScalarOptParserTableGen
  LTOOptParserTableGen
  LoongArchOptParserTableGen
  NVPTXOptParserTableGen
  ProfileDataOptParserTableGen
  UtilsOptParserTableGen
)

add_llvm_tool(opt
  PARTIAL_SOURCES_INTENDED
  opt.cpp
  DEPENDS
  intrinsics_gen
  SUPPORT_PLUGINS

  EXPORT_SYMBOLS
  )
target_link_libraries(opt PRIVATE LLVMOptDriver)

setup_host_tool(opt OPT opt_exe opt_target)

if (LLVM_POLLY_LINK_INTO_TOOLS)
  target_compile_definitions(LLVMOptDriver PRIVATE LINK_POLLY_INTO_TOOLS)
  target_include_directories(LLVMOptDriver PRIVATE
    ${LLVM_MAIN_SRC_DIR}/../polly/include
    ${LLVM_BINARY_DIR}/tools/polly/include)
  target_compile_definitions(opt PRIVATE LINK_POLLY_INTO_TOOLS)
  target_include_directories(opt PRIVATE
    ${LLVM_MAIN_SRC_DIR}/../polly/include
    ${LLVM_BINARY_DIR}/tools/polly/include)
endif()
