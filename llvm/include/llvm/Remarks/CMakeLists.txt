
set(LLVM_TARGET_DEFINITIONS RemarksOptions.td)
tablegen(LLVM RemarksOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(RemarksOptionsTableGen)
