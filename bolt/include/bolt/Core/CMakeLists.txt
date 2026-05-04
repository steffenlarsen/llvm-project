
set(LLVM_TARGET_DEFINITIONS BoltCoreOptions.td)
tablegen(LLVM BoltCoreOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(BoltCoreOptionsTableGen)
