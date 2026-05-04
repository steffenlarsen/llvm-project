
set(LLVM_TARGET_DEFINITIONS AsmParserOptions.td)
tablegen(LLVM AsmParserOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(AsmParserOptionsTableGen)
