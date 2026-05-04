
set(LLVM_TARGET_DEFINITIONS BoltPassesOptions.td)
tablegen(LLVM BoltPassesOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(BoltPassesOptionsTableGen)
