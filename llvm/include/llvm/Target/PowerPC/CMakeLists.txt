
set(LLVM_TARGET_DEFINITIONS PowerPCOptions.td)
tablegen(LLVM PowerPCOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(PowerPCOptionsTableGen)
