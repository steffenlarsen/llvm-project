
set(LLVM_TARGET_DEFINITIONS HexagonOptions.td)
tablegen(LLVM HexagonOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(HexagonOptionsTableGen)
