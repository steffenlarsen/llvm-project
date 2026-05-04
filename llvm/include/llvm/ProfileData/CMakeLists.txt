
set(LLVM_TARGET_DEFINITIONS ProfileDataOptions.td)
tablegen(LLVM ProfileDataOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(ProfileDataOptionsTableGen)
