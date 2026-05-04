
set(LLVM_TARGET_DEFINITIONS SparcOptions.td)
tablegen(LLVM SparcOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(SparcOptionsTableGen)
