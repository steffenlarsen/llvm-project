set(LLVM_TARGET_DEFINITIONS OMP.td)
tablegen(LLVM OMP.h.inc --gen-directive-decl)
tablegen(LLVM OMP.inc --gen-directive-impl)
add_public_tablegen_target(omp_gen)

# Reset TABLEGEN_OUTPUT first: it accumulates across every tablegen() call in
# this directory, so without this the clv2 target would also depend on the
# unrelated .inc files generated above -- and in MLIR's case on mlir-tblgen,
# which creates a dependency cycle.
set(TABLEGEN_OUTPUT "")
set(LLVM_TARGET_DEFINITIONS OpenMPOptions.td)
tablegen(LLVM OpenMPOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(OpenMPOptionsTableGen)
