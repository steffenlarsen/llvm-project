//===- NVPTXOptions.td - CLV2 option defs for NVPTX ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def NVPTXOptsReg : Registry<"llvm::nvptx_opts"> {
  // Getter bodies are compiled once, in NVPTXOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

// --- NVPTXTargetMachine.cpp ---

def NVPTX_DisableLoadStoreVectorizer : Option<
    "disable-nvptx-load-store-vectorizer",
    "Disable load/store vectorizer",
    "bool"> {
  let Registry = NVPTXOptsReg;
  let Hidden = 1;
}

def NVPTX_DisableIRPeephole : Option<
    "disable-nvptx-ir-peephole",
    "Disable NVPTX IR Peephole",
    "bool"> {
  let Registry = NVPTXOptsReg;
  let Hidden = 1;
}

def NVPTX_DisableRequireStructuredCFG : Option<
    "disable-nvptx-require-structured-cfg",
    "Transitional flag to turn off NVPTX's requirement on preserving "
    "structured CFG. The requirement should be disabled only when "
    "unexpected regressions happen.",
    "bool"> {
  let Registry = NVPTXOptsReg;
  let Hidden = 1;
}

def NVPTX_EarlyByValArgsCopy : Option<
    "nvptx-early-byval-copy",
    "Create a copy of byval function arguments early.",
    "bool"> {
  let Registry = NVPTXOptsReg;
  let Hidden = 1;
}

// --- NVPTXCtorDtorLowering.cpp ---

def NVPTX_LowerGlobalCtorDtorId : Option<
    "nvptx-lower-global-ctor-dtor-id",
    "Override unique ID of ctor/dtor globals.",
    "std::string"> {
  let Registry = NVPTXOptsReg;
  let Hidden = 1;
}

def NVPTX_EmitInitFiniKernel : Option<
    "nvptx-emit-init-fini-kernel",
    "Emit kernels to call ctor/dtor globals.",
    "bool"> {
  let Registry = NVPTXOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- NVPTXSubtarget.cpp ---

def NVPTX_NoF16Math : Option<
    "nvptx-no-f16-math",
    "NVPTX Specific: Disable generation of f16 math ops.",
    "bool"> {
  let Registry = NVPTXOptsReg;
  let Hidden = 1;
}

def NVPTX_NoF32x2 : Option<
    "nvptx-no-f32x2",
    "NVPTX Specific: Disable generation of "
    "f32x2 instructions and registers.",
    "bool"> {
  let Registry = NVPTXOptsReg;
  let Hidden = 1;
}

// --- NVPTXISelLowering.cpp ---

def NVPTX_Sched4Reg : Option<
    "nvptx-sched4reg",
    "NVPTX Specific: schedule for register pressue",
    "bool"> {
  let Registry = NVPTXOptsReg;
}

def NVPTX_FMAContractLevel : Option<
    "nvptx-fma-level",
    "NVPTX Specific: FMA contraction (0: don't do it"
    " 1: do it  2: do it aggressively",
    "unsigned"> {
  let Registry = NVPTXOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def NVPTXDivPrecisionApprox : EnumValue<"0",
    "Use div.approx"> {
  let CppName = "Approx";
  let Value = 0;
}

def NVPTXDivPrecisionFull : EnumValue<"1",
    "Use div.full"> {
  let CppName = "Full";
  let Value = 1;
}

def NVPTXDivPrecisionIEEE754 : EnumValue<"2",
    "Use IEEE Compliant F32 div.rnd if available (default)"> {
  let CppName = "IEEE754";
  let Value = 2;
}

def NVPTXDivPrecisionIEEE754NoFTZ : EnumValue<"3",
    "Use IEEE Compliant F32 div.rnd if available, no FTZ"> {
  let CppName = "IEEE754_NoFTZ";
  let Value = 3;
}

def NVPTXDivPrecisionEnum : EnumType<"NVPTXDivPrecision",
    [NVPTXDivPrecisionApprox, NVPTXDivPrecisionFull,
     NVPTXDivPrecisionIEEE754, NVPTXDivPrecisionIEEE754NoFTZ]>;

def NVPTX_PrecDivF32 : Option<
    "nvptx-prec-divf32",
    "NVPTX Specific: Override the precision of the lowering for f32 fdiv",
    "NVPTXDivPrecision"> {
  let Registry = NVPTXOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def NVPTX_PrecSqrtF32 : Option<
    "nvptx-prec-sqrtf32",
    "NVPTX Specific: 0 use sqrt.approx, 1 use sqrt.rn.",
    "bool"> {
  let Registry = NVPTXOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def NVPTX_ApproxLog2F32 : Option<
    "nvptx-approx-log2f32",
    "NVPTX Specific: whether to use lg2.approx for log2",
    "bool"> {
  let Registry = NVPTXOptsReg;
}

def NVPTX_AllowFTZAtomics : Option<
    "nvptx-allow-ftz-atomics",
    "Allow using native atomics even when FTZ behavior doesn't match",
    "bool"> {
  let Registry = NVPTXOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- NVPTXUtilities.cpp ---

def NVPTX_ForceMinByValParamAlign : Option<
    "nvptx-force-min-byval-param-align",
    "NVPTX Specific: force 4-byte minimal alignment for byval"
    " params of device functions.",
    "bool"> {
  let Registry = NVPTXOptsReg;
  let Hidden = 1;
}

// --- NVPTXAliasAnalysis.cpp ---

def NVPTX_TraverseAddressSpacesLimit : Option<
    "nvptx-traverse-address-aliasing-limit",
    "Depth limit for finding address space through traversal",
    "unsigned"> {
  let Registry = NVPTXOptsReg;
  let Default = "6";
  let Hidden = 1;
}

// --- NVPTXDwarfDebug.cpp ---

def NVPTX_LineInfoWithInlinedAt : Option<
    "line-info-inlined-at",
    "Emit line with inlined_at enhancement for NVPTX",
    "bool"> {
  let Registry = NVPTXOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- NVPTXISelDAGToDAG.cpp ---

def NVPTX_EnableRsqrtOpt : Option<
    "nvptx-rsqrt-approx-opt",
    "Enable reciprocal sqrt optimization",
    "bool"> {
  let Registry = NVPTXOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def NVPTX_EnableMADWide : Option<
    "nvptx-mad-wide-opt",
    "Enable MAD wide optimization",
    "bool"> {
  let Registry = NVPTXOptsReg;
  let Hidden = 1;
}

// --- NVVMReflect.cpp ---

def NVPTX_NVVMReflectEnabled : Option<
    "nvvm-reflect-enable",
    "NVVM reflection, enabled by default",
    "bool"> {
  let Registry = NVPTXOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def NVPTX_NVVMReflectList : ListOption<
    "nvvm-reflect-add",
    "A key=value pair. Replace __nvvm_reflect(name) with value.",
    "std::string"> {
  let Registry = NVPTXOptsReg;
  let ValueDesc = "name=<int>";
  let Hidden = 1;
}
