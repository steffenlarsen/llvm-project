
set(LLVM_TARGET_DEFINITIONS FileCheckOptions.td)
# -gen-opt-parser-defs is only implemented by the full llvm-tblgen, not the
# llvm-min-tblgen bootstrap tool.
set(LLVM_TABLEGEN_PROJECT "")
tablegen(LLVM FileCheckOptions.inc -gen-opt-parser-defs)
# Deliberately not named *OptionsTableGen: that suffix is matched by
# _collect_clv2_options_targets() (TableGen.cmake) to build the
# llvm_clv2_options_gen aggregate, which is itself a dependency of
# llvm-tblgen (via LLVMCodeGenTypes). Since this target depends on
# llvm-tblgen above, matching the suffix would create a cycle.
add_public_tablegen_target(FileCheckOptParserTableGen)

add_llvm_utility(FileCheck
  FileCheck.cpp
  FileCheckOptions.cpp

  DEPENDS
  FileCheckOptParserTableGen
  )

target_link_libraries(FileCheck PRIVATE LLVMFileCheck LLVMSupport LLVMOption)
