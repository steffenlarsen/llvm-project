
set(LLVM_TARGET_DEFINITIONS AMDGPUOptions.td)
tablegen(LLVM AMDGPUOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(AMDGPUOptionsTableGen)
