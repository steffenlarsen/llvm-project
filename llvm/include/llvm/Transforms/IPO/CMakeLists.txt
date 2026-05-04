
set(LLVM_TARGET_DEFINITIONS IPOOptions.td)
tablegen(LLVM IPOOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(IPOOptionsTableGen)
