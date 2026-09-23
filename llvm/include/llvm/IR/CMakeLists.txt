set(LLVM_TARGET_DEFINITIONS Attributes.td)
tablegen(LLVM Attributes.inc -gen-attrs)

set(LLVM_TARGET_DEFINITIONS RuntimeLibcalls.td)
tablegen(LLVM RuntimeLibcalls.inc -gen-runtime-libcalls)

set(LLVM_TARGET_DEFINITIONS Intrinsics.td)
tablegen(LLVM IntrinsicImpl.inc -gen-intrinsic-impl)
tablegen(LLVM IntrinsicEnums.inc -gen-intrinsic-enums)
tablegen(LLVM IntrinsicsAArch64.h -gen-intrinsic-enums -intrinsic-prefix=aarch64)
tablegen(LLVM IntrinsicsAMDGPU.h -gen-intrinsic-enums -intrinsic-prefix=amdgcn)
tablegen(LLVM IntrinsicsARM.h -gen-intrinsic-enums -intrinsic-prefix=arm)
tablegen(LLVM IntrinsicsAVR.h -gen-intrinsic-enums -intrinsic-prefix=avr)
tablegen(LLVM IntrinsicsBPF.h -gen-intrinsic-enums -intrinsic-prefix=bpf)
tablegen(LLVM IntrinsicsDirectX.h -gen-intrinsic-enums -intrinsic-prefix=dx)
tablegen(LLVM IntrinsicsHexagon.h -gen-intrinsic-enums -intrinsic-prefix=hexagon)
tablegen(LLVM IntrinsicsLoongArch.h -gen-intrinsic-enums -intrinsic-prefix=loongarch)
tablegen(LLVM IntrinsicsMips.h -gen-intrinsic-enums -intrinsic-prefix=mips)
tablegen(LLVM IntrinsicsNVPTX.h -gen-intrinsic-enums -intrinsic-prefix=nvvm)
tablegen(LLVM IntrinsicsPowerPC.h -gen-intrinsic-enums -intrinsic-prefix=ppc)
tablegen(LLVM IntrinsicsR600.h -gen-intrinsic-enums -intrinsic-prefix=r600)
tablegen(LLVM IntrinsicsRISCV.h -gen-intrinsic-enums -intrinsic-prefix=riscv)
tablegen(LLVM IntrinsicsSPIRV.h -gen-intrinsic-enums -intrinsic-prefix=spv)
tablegen(LLVM IntrinsicsS390.h -gen-intrinsic-enums -intrinsic-prefix=s390)
tablegen(LLVM IntrinsicsWebAssembly.h -gen-intrinsic-enums -intrinsic-prefix=wasm)
tablegen(LLVM IntrinsicsX86.h -gen-intrinsic-enums -intrinsic-prefix=x86)
tablegen(LLVM IntrinsicsXCore.h -gen-intrinsic-enums -intrinsic-prefix=xcore)
tablegen(LLVM IntrinsicsVE.h -gen-intrinsic-enums -intrinsic-prefix=ve)
add_public_tablegen_target(intrinsics_gen)

# Reset TABLEGEN_OUTPUT first: it accumulates across every tablegen() call in
# this directory, so without this this target would also depend on the
# unrelated .inc files generated above -- and in MLIR's case on mlir-tblgen,
# which creates a dependency cycle.
set(TABLEGEN_OUTPUT "")
set(LLVM_TARGET_DEFINITIONS IROptions.td)
# -gen-opt-parser-defs is only implemented by the full llvm-tblgen, not the
# llvm-min-tblgen bootstrap tool that LLVM_HEADERS (set above us in
# llvm/include/llvm/CMakeLists.txt) would otherwise route this to.
set(LLVM_TABLEGEN_PROJECT "")
tablegen(LLVM IROptions.inc -gen-opt-parser-defs)
# Deliberately not named *OptionsTableGen: that suffix is matched by
# _collect_clv2_options_targets() (TableGen.cmake) to build the
# llvm_clv2_options_gen aggregate, which is itself a dependency of
# llvm-tblgen (via LLVMCodeGenTypes). Since this target depends on
# llvm-tblgen above, matching the suffix would create a cycle.
add_public_tablegen_target(IROptParserTableGen)
