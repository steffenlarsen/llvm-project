set(LLVM_LINK_COMPONENTS
  # RegisterAllLLVMOptions() lives in AllOptions; without it this example
  # fails to link under clv2.
  AllOptions
  Core
  ExecutionEngine
  IRReader
  JITLink
  OrcJIT
  OrcTargetProcess
  Support
  nativecodegen
  )

add_llvm_example(LLJITWithGDBRegistrationListener
  LLJITWithGDBRegistrationListener.cpp
  )
