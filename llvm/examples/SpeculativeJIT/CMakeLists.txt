set(LLVM_LINK_COMPONENTS
  # RegisterAllLLVMOptions() lives in AllOptions; without it this example
  # fails to link under clv2.
  AllOptions
  Core
  IRReader
  OrcJIT
  OrcTargetProcess
  ExecutionEngine
  Support
  nativecodegen
  Analysis
  Passes
  )

if(LLVM_ENABLE_THREADS)
  add_llvm_example(SpeculativeJIT
    SpeculativeJIT.cpp
  )
endif()
