
set(LLVM_TARGET_DEFINITIONS FlangOptions.td)
tablegen(LLVM FlangOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(FlangOptionsTableGen)
