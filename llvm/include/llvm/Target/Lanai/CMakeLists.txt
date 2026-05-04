
set(LLVM_TARGET_DEFINITIONS LanaiOptions.td)
tablegen(LLVM LanaiOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(LanaiOptionsTableGen)
