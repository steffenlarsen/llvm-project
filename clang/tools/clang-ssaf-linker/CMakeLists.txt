set(LLVM_LINK_COMPONENTS
  AllOptions
  Option
  Support
  TargetParser
  )

add_clang_tool(clang-ssaf-linker
  LinkCLI.cpp
  MultiArchCreateCLI.cpp
  StaticLibraryCreateCLI.cpp
  SSAFLinker.cpp
  )

clang_target_link_libraries(clang-ssaf-linker
  PRIVATE
  clangBasic
  clangScalableStaticAnalysisAnalyses
  clangScalableStaticAnalysisCore
  clangScalableStaticAnalysisSourceTransformation
  clangScalableStaticAnalysisTool
  )
