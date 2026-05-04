
set(LLVM_TARGET_DEFINITIONS BitcodeOptions.td)
tablegen(LLVM BitcodeOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(BitcodeOptionsTableGen)
