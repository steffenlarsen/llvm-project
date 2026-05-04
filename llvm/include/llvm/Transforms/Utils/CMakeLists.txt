
set(LLVM_TARGET_DEFINITIONS UtilsOptions.td)
tablegen(LLVM UtilsOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(UtilsOptionsTableGen)
