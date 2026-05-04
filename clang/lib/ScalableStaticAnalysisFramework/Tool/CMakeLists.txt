set(LLVM_LINK_COMPONENTS
  AllOptions
  Support
  )

add_clang_library(clangScalableStaticAnalysisFrameworkTool
  Utils.cpp

  LINK_LIBS
  clangBasic
  clangScalableStaticAnalysisFrameworkCore
  )
