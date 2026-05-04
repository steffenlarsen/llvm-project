set(LLVM_LINK_COMPONENTS
  AllOptions
  Support
  Symbolize
  )

add_llvm_tool(sanstats
  sanstats.cpp

  DEPENDS
  intrinsics_gen
  )
