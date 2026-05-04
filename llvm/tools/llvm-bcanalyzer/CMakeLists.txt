set(LLVM_LINK_COMPONENTS
  AllOptions
  BitReader
  BitstreamReader
  Support
  )

add_llvm_tool(llvm-bcanalyzer
  llvm-bcanalyzer.cpp

  DEPENDS
  intrinsics_gen
  )
