
set(LLVM_TARGET_DEFINITIONS CGDataOptions.td)
tablegen(LLVM CGDataOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(CGDataOptionsTableGen)
