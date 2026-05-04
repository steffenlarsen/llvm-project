set(LLVM_LINK_COMPONENTS
  AllOptions
  BitReader
  Core
  ExecutionEngine
  OrcJIT
  OrcTargetProcess
  Support
  TargetParser
  nativecodegen
  )

add_llvm_example(LLJITWithThinLTOSummaries
  LLJITWithThinLTOSummaries.cpp
  )
