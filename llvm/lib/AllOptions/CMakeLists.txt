# Experimental targets are not always in the build, so their registration TUs
# are added conditionally and gated by a matching macro below.
set(LLVMAllOptions_EXPERIMENTAL_SRCS "")
foreach(t ARC CSKY M68k)
  if (${t} IN_LIST LLVM_TARGETS_TO_BUILD)
    list(APPEND LLVMAllOptions_EXPERIMENTAL_SRCS Targets/${t}.cpp)
  endif()
endforeach()

add_llvm_component_library(LLVMAllOptions
  AllOptions.cpp
  ${LLVMAllOptions_EXPERIMENTAL_SRCS}
  Targets/AArch64.cpp
  Targets/AMDGPU.cpp
  Targets/ARM.cpp
  Targets/BPF.cpp
  Targets/Hexagon.cpp
  Targets/Lanai.cpp
  Targets/LoongArch.cpp
  Targets/Mips.cpp
  Targets/MSP430.cpp
  Targets/NVPTX.cpp
  Targets/PowerPC.cpp
  Targets/RISCV.cpp
  Targets/Sparc.cpp
  Targets/SPIRV.cpp
  Targets/SystemZ.cpp
  Targets/WebAssembly.cpp
  Targets/X86.cpp
  Targets/XCore.cpp

  LINK_COMPONENTS
  AggressiveInstCombine
  Analysis
  AsmParser
  BitReader
  CGData
  CodeGen
  Core
  Coroutines
  FrontendOpenMP
  IPO
  InstCombine
  Instrumentation
  LTO
  MC
  ObjCARCOpts
  Object
  Passes
  ProfileData
  Remarks
  Scalar
  Support
  TransformUtils
  Vectorize
  )

# Polly's clv2 options are registered from AllOptions.cpp behind
# LINK_POLLY_INTO_TOOLS.  Nothing defined that macro, so -polly-* flags were
# rejected by every tool even though Polly was statically linked in.
if (LLVM_POLLY_LINK_INTO_TOOLS)
  target_compile_definitions(LLVMAllOptions PRIVATE LINK_POLLY_INTO_TOOLS)
  target_include_directories(LLVMAllOptions PRIVATE
    ${LLVM_MAIN_SRC_DIR}/../polly/include
    ${LLVM_BINARY_DIR}/tools/polly/include)
endif()
