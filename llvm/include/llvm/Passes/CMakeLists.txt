
set(LLVM_TARGET_DEFINITIONS PassesOptions.td)
tablegen(LLVM PassesOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(PassesOptionsTableGen)
