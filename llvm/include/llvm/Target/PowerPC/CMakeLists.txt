
# -gen-opt-parser-defs is only implemented by the full llvm-tblgen, not the
# llvm-min-tblgen bootstrap tool that LLVM_HEADERS (set above us in
# llvm/include/llvm/CMakeLists.txt) would otherwise route this to.
set(LLVM_TABLEGEN_PROJECT "")
set(LLVM_TARGET_DEFINITIONS PowerPCOptions.td)
tablegen(LLVM PowerPCOptions.inc -gen-opt-parser-defs)
# Deliberately not named *OptionsTableGen: that suffix is matched by
# TableGen.cmake's _collect_clv2_options_targets() into the
# llvm_clv2_options_gen aggregate, itself a dependency of llvm-tblgen --
# using it here would create a build cycle.
add_public_tablegen_target(PowerPCOptParserTableGen)
