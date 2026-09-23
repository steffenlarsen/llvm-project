set(LLVM_LINK_COMPONENTS
  AllOptions
  Core
  Object
  ProfileData
  Support
  )

add_llvm_tool(llvm-profdata
  llvm-profdata.cpp

  DEPENDS
  intrinsics_gen
  ProfileDataOptParserTableGen
  )

target_link_libraries(llvm-profdata PRIVATE LLVMDebuginfod LLVMHTTP)
