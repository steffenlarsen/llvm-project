set(LLVM_LINK_COMPONENTS
  AllOptions
  Demangle
  Support
  )

add_llvm_tool(llvm-undname
  llvm-undname.cpp
  )
