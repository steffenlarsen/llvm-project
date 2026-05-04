
set(LLVM_TARGET_DEFINITIONS MSP430Options.td)
tablegen(LLVM MSP430OptionsOptInfos.inc -gen-clv2-options)
add_public_tablegen_target(MSP430OptionsTableGen)
