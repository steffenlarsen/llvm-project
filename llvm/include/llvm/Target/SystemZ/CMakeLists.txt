
set(LLVM_TARGET_DEFINITIONS SystemZOptions.td)
tablegen(LLVM SystemZOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(SystemZOptionsTableGen)
