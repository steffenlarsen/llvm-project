
set(LLVM_TARGET_DEFINITIONS InstCombineOptions.td)
tablegen(LLVM InstCombineOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(InstCombineOptionsTableGen)
