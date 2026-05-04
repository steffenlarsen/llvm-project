set(LLVM_LINK_COMPONENTS
  # RegisterAllLLVMOptions() lives in AllOptions; without it this example
  # fails to link under clv2.
  AllOptions
  Core
  IRReader
  JITLink
  OrcJIT
  OrcShared
  Support
  nativecodegen
  )

add_llvm_example(LLJITWithObjectLinkingLayerPlugin
  LLJITWithObjectLinkingLayerPlugin.cpp
  )
