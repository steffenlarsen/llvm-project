set(LLVM_LINK_COMPONENTS
  AllOptions
  AllTargetsDisassemblers
  AllTargetsDescs
  AllTargetsInfos
  DebugInfoDWARF
  ExecutionEngine
  MC
  Object
  RuntimeDyld
  Support
  TargetParser
  )

add_llvm_tool(llvm-rtdyld
  llvm-rtdyld.cpp
  )
