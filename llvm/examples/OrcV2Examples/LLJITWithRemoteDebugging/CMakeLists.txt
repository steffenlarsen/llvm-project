set(LLVM_LINK_COMPONENTS
  # RegisterAllLLVMOptions() lives in AllOptions; without it this example
  # fails to link under clv2.
  AllOptions
  Core
  ExecutionEngine
  IRReader
  JITLink
  OrcDebugging
  OrcJIT
  OrcShared
  OrcTargetProcess
  Support
  TargetParser
  nativecodegen
  )

if (LLVM_INCLUDE_UTILS AND LLVM_ENABLE_THREADS)
  add_llvm_example(LLJITWithRemoteDebugging
    LLJITWithRemoteDebugging.cpp
    RemoteJITUtils.cpp

    DEPENDS
      llvm-jitlink-executor
  )
endif()
