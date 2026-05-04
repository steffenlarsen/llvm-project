
set(LLVM_TARGET_DEFINITIONS CoroutinesOptions.td)
tablegen(LLVM CoroutinesOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(CoroutinesOptionsTableGen)
