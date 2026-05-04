
set(LLVM_TARGET_DEFINITIONS CSKYOptions.td)
tablegen(LLVM CSKYOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(CSKYOptionsTableGen)
