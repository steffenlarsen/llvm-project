
set(LLVM_TARGET_DEFINITIONS ObjectOptions.td)
tablegen(LLVM ObjectOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(ObjectOptionsTableGen)
