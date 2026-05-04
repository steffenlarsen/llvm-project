set(LLVM_TARGET_DEFINITIONS PollyOptions.td)
tablegen(LLVM PollyOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(PollyOptionsTableGen)
