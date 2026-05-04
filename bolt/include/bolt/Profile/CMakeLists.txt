
set(LLVM_TARGET_DEFINITIONS BoltProfileOptions.td)
tablegen(LLVM BoltProfileOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(BoltProfileOptionsTableGen)
