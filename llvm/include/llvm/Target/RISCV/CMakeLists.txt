
set(LLVM_TARGET_DEFINITIONS RISCVOptions.td)
tablegen(LLVM RISCVOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(RISCVOptionsTableGen)
