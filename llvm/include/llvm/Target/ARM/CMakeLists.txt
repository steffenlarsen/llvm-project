
set(LLVM_TABLEGEN_PROJECT "")
set(LLVM_TARGET_DEFINITIONS ARMOptions.td)
tablegen(LLVM ARMOptions.inc -gen-opt-parser-defs)
add_public_tablegen_target(ARMOptParserTableGen)
