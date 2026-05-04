
set(LLVM_TARGET_DEFINITIONS X86Options.td)
tablegen(LLVM X86OptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(X86OptionsTableGen)
