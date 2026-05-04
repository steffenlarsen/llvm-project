set(LLVM_TARGET_DEFINITIONS TargetLibraryInfo.td)
tablegen(LLVM TargetLibraryInfo.inc -gen-target-library-info)
add_public_tablegen_target(analysis_gen)

# Reset TABLEGEN_OUTPUT first: it accumulates across every tablegen() call in
# this directory, so without this the clv2 target would also depend on the
# unrelated .inc files generated above -- and in MLIR's case on mlir-tblgen,
# which creates a dependency cycle.
set(TABLEGEN_OUTPUT "")
set(LLVM_TARGET_DEFINITIONS AnalysisOptions.td)
tablegen(LLVM AnalysisOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(AnalysisOptionsTableGen)
