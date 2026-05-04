
set(LLVM_TARGET_DEFINITIONS MCOptions.td)
tablegen(LLVM MCOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(MCOptionsTableGen)
