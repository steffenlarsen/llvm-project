
set(LLVM_TARGET_DEFINITIONS LoongArchOptions.td)
tablegen(LLVM LoongArchOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(LoongArchOptionsTableGen)
