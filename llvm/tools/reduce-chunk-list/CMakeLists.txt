set(LLVM_LINK_COMPONENTS
  AllOptions
  Support
  )

add_llvm_tool(reduce-chunk-list
  reduce-chunk-list.cpp

  DEPENDS
  )
