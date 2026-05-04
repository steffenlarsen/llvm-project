
set(LLVM_TARGET_DEFINITIONS ARMOptions.td)
tablegen(LLVM ARMOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(ARMOptionsTableGen)
