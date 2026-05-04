
set(LLVM_TARGET_DEFINITIONS SPIRVOptions.td)
tablegen(LLVM SPIRVOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(SPIRVOptionsTableGen)
