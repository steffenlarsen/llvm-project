# Keep the command-line implementation in a separate library so other LLVM
# tools can reuse the llc driver without compiling or rewriting llc.cpp.
add_llvm_library(LLVMLlcDriver
  STATIC
  llcdriver.cpp
  NewPMDriver.cpp

  DEPENDS
  intrinsics_gen
  AArch64OptParserTableGen
  AMDGPUOptParserTableGen
  BPFOptParserTableGen
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
  InstrumentationOptParserTableGen
  IPOOptParserTableGen
  ScalarOptParserTableGen
  LTOOptParserTableGen
  LoongArchOptParserTableGen
  NVPTXOptParserTableGen
  ProfileDataOptParserTableGen
  SPIRVOptParserTableGen
  UtilsOptParserTableGen
  WebAssemblyOptParserTableGen
  )
