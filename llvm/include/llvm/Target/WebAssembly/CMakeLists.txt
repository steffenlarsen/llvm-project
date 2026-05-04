
set(LLVM_TARGET_DEFINITIONS WebAssemblyOptions.td)
tablegen(LLVM WebAssemblyOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(WebAssemblyOptionsTableGen)
