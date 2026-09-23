set(LLVM_TARGET_DEFINITIONS OMP.td)
tablegen(LLVM OMP.h.inc --gen-directive-decl)
tablegen(LLVM OMP.inc --gen-directive-impl)
add_public_tablegen_target(omp_gen)

# Reset TABLEGEN_OUTPUT first: it accumulates across every tablegen() call in
# this directory, so without this the new target would also depend on the
# unrelated .inc files generated above -- and in MLIR's case on mlir-tblgen,
# which creates a dependency cycle.
set(TABLEGEN_OUTPUT "")
# -gen-opt-parser-defs is only implemented by the full llvm-tblgen, not the
# llvm-min-tblgen bootstrap tool that LLVM_HEADERS (set above us in
# llvm/include/llvm/CMakeLists.txt) would otherwise route this to.
set(LLVM_TABLEGEN_PROJECT "")
set(LLVM_TARGET_DEFINITIONS OpenMPOptions.td)
tablegen(LLVM OpenMPOptions.inc -gen-opt-parser-defs)
# Deliberately not named *OptionsTableGen: that suffix is matched by
# _collect_clv2_options_targets() (TableGen.cmake) to build the
# llvm_clv2_options_gen aggregate, which is itself a dependency of
# llvm-tblgen. Since this target depends on llvm-tblgen above, matching the
# suffix would create a cycle.
add_public_tablegen_target(OpenMPOptParserTableGen)
