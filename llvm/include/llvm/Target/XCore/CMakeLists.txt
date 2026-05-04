
set(LLVM_TARGET_DEFINITIONS XCoreOptions.td)
tablegen(LLVM XCoreOptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(XCoreOptionsTableGen)
