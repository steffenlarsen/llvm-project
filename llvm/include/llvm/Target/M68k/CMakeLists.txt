
set(LLVM_TARGET_DEFINITIONS M68kOptions.td)
tablegen(LLVM M68kOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(M68kOptionsTableGen)
