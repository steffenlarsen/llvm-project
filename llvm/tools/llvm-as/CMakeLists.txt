set(LLVM_LINK_COMPONENTS
  AllOptions
  AsmParser
  BitWriter
  Core
  Support
  )

add_llvm_tool(llvm-as
  llvm-as.cpp

  DEPENDS
  intrinsics_gen
  BitcodeMemProfOptParserTableGen
  )

setup_host_tool(llvm-as LLVM_AS llvm_as_exe llvm_as_target)
