set(LLVM_LINK_COMPONENTS
  AllOptions
  BitReader
  Core
  Support
  )

add_llvm_tool(llvm-dis
  llvm-dis.cpp

  DEPENDS
  intrinsics_gen
  )
