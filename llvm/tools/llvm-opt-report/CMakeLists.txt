set(LLVM_LINK_COMPONENTS
  AllOptions
  Demangle
  Remarks
  Support
  )

add_llvm_tool(llvm-opt-report
  OptReport.cpp
  )

