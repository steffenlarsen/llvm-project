set(LLVM_LINK_COMPONENTS
  # RegisterAllLLVMOptions() lives in AllOptions; without it this example
  # fails to link under clv2.
  AllOptions
  Core
  ExecutionEngine
  IRReader
  OrcJIT
  Support
  nativecodegen
  )

add_llvm_example(LLJITWithExecutorProcessControl
  LLJITWithExecutorProcessControl.cpp
  )
