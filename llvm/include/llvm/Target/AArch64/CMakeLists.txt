set(LLVM_TARGET_DEFINITIONS AArch64Options.td)
tablegen(LLVM AArch64OptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(AArch64OptionsTableGen)
