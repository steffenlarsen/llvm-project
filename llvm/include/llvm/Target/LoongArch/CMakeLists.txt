
# -gen-opt-parser-defs is only implemented by the full llvm-tblgen, not the
# llvm-min-tblgen bootstrap tool that LLVM_HEADERS (set above us in
# llvm/include/llvm/CMakeLists.txt) would otherwise route this to.
set(LLVM_TABLEGEN_PROJECT "")
set(LLVM_TARGET_DEFINITIONS LoongArchOptions.td)
tablegen(LLVM LoongArchOptions.inc -gen-opt-parser-defs)
# Deliberately not named *OptionsTableGen: that suffix is matched by
# _collect_clv2_options_targets() (TableGen.cmake) to build the
# llvm_clv2_options_gen aggregate, which is itself a dependency of
# llvm-tblgen. Since this target depends on llvm-tblgen above, matching the
# suffix would create a cycle.
add_public_tablegen_target(LoongArchOptParserTableGen)
