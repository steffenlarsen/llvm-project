
set(LLVM_TARGET_DEFINITIONS VectorizeOptions.td)
tablegen(LLVM VectorizeOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(VectorizeOptionsTableGen)
