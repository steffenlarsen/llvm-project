
set(LLVM_TARGET_DEFINITIONS ARCOptions.td)
tablegen(LLVM ARCOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(ARCOptionsTableGen)
