
set(LLVM_TARGET_DEFINITIONS ObjCARCOptions.td)
tablegen(LLVM ObjCARCOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(ObjCARCOptionsTableGen)
