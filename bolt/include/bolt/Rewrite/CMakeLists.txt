
set(LLVM_TARGET_DEFINITIONS BoltRewriteOptions.td)
tablegen(LLVM BoltRewriteOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(BoltRewriteOptionsTableGen)
