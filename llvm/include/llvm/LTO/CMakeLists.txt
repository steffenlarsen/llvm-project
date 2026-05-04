
set(LLVM_TARGET_DEFINITIONS LTOOptions.td)
tablegen(LLVM LTOOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(LTOOptionsTableGen)
