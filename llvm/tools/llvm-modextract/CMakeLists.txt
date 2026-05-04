set(LLVM_LINK_COMPONENTS
  AllOptions
  BitReader
  BitWriter
  Core
  Support
  )

add_llvm_tool(llvm-modextract
  llvm-modextract.cpp
  )
