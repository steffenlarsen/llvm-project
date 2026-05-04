
set(LLVM_TARGET_DEFINITIONS BPFOptions.td)
tablegen(LLVM BPFOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(BPFOptionsTableGen)
