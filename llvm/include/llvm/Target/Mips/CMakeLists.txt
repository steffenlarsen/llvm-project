
set(LLVM_TARGET_DEFINITIONS MipsOptions.td)
tablegen(LLVM MipsOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(MipsOptionsTableGen)
