set(LLVM_LINK_COMPONENTS
  Support
  )

add_clang_tool(list-globals
  ListGlobals.cpp
)

clang_target_link_libraries(list-globals
  PRIVATE
  clangAST
  clangASTMatchers
  clangBasic
  clangFrontend
  clangTooling
  )