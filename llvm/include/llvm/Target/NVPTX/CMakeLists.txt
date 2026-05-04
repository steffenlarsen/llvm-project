
set(LLVM_TARGET_DEFINITIONS NVPTXOptions.td)
tablegen(LLVM NVPTXOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(NVPTXOptionsTableGen)
