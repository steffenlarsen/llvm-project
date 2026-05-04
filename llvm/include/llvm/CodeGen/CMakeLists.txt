set(LLVM_TARGET_DEFINITIONS ${PROJECT_SOURCE_DIR}/include/llvm/CodeGen/ValueTypes.td)
tablegen(LLVM GenVT.inc -gen-vt)
add_public_tablegen_target(vt_gen)

# Reset TABLEGEN_OUTPUT first: it accumulates across every tablegen() call in
# this directory, so without this the clv2 target would also depend on the
# unrelated .inc files generated above -- and in MLIR's case on mlir-tblgen,
# which creates a dependency cycle.
set(TABLEGEN_OUTPUT "")
set(LLVM_TARGET_DEFINITIONS CommandFlagsOptions.td)
tablegen(LLVM CommandFlagsOptInfos.inc -gen-clv2-options)

set(LLVM_TARGET_DEFINITIONS CodeGenPassOptions.td)
tablegen(LLVM CodeGenPassOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(CodeGenPassOptionsTableGen)
add_public_tablegen_target(CommandFlagsTableGen)
