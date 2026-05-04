
set(LLVM_TARGET_DEFINITIONS ScalarOptions.td)
tablegen(LLVM ScalarOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(ScalarOptionsTableGen)
