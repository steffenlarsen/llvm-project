
set(LLVM_TARGET_DEFINITIONS BoltUtilsOptions.td)
tablegen(LLVM BoltUtilsOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(BoltUtilsOptionsTableGen)
