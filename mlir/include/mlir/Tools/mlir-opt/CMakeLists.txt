
set(LLVM_TARGET_DEFINITIONS MlirOptToolOptions.td)
tablegen(LLVM MlirOptToolOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(MlirOptToolOptionsTableGen)
