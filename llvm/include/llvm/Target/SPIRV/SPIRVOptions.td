//===- SPIRVOptions.td - CLV2 option defs for SPIRV ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def SPIRVOptsReg : Registry<"llvm::spirv_opts"> {
  // Getter bodies are compiled once, in SPIRVOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

// --- SPIRVCtorDtorLowering.cpp ---

def SPIRV_LowerGlobalCtorDtorId : Option<
    "spirv-lower-global-ctor-dtor-id",
    "Override unique ID of ctor/dtor globals.",
    "std::string"> {
  let Default = "\"\"";
  let Registry = SPIRVOptsReg;
  let Hidden = 1;
}

def SPIRV_EmitInitFiniKernel : Option<
    "spirv-emit-init-fini-kernel",
    "Emit kernels to call ctor/dtor globals.",
    "bool"> {
  let Registry = SPIRVOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- SPIRVEmitIntrinsics.cpp ---

def SPIRV_EmitOpNames : Option<
    "spirv-emit-op-names",
    "Emit OpName for all instructions",
    "bool"> {
  let Default = "false";
  let Registry = SPIRVOptsReg;
}

// --- SPIRVModuleAnalysis.cpp ---

def SPIRV_DumpDeps : Option<
    "spv-dump-deps",
    "Dump MIR with SPIR-V dependencies info",
    "bool"> {
  let Default = "false";
  let Registry = SPIRVOptsReg;
}

// --- SPIRVTargetMachine.cpp ---

def SPIRV_EnableNonSemanticDI : Option<
    "spv-emit-nonsemantic-debug-info",
    "Deprecated. Use -g to emit SPIR-V NonSemantic.Shader.DebugInfo "
    "instructions",
    "bool"> {
  let Default = "false";
  let Registry = SPIRVOptsReg;
}

// --- SPIRVPrepareFunctions.cpp ---

def SPIRV_AllowUnknownIntrinsics : ListOption<
    "spv-allow-unknown-intrinsics",
    "Emit unknown intrinsics as calls to external functions. A "
    "comma-separated input list of intrinsic prefixes must be "
    "provided, and only intrinsics carrying a listed prefix get "
    "emitted as described.",
    "std::string"> {
  let Registry = SPIRVOptsReg;
  let ValueDesc = "intrinsic_prefix_0,intrinsic_prefix_1";
  let CommaSeparated = 1;
  let ValueOptional = 1;
}

// --- SPIRVSubtarget.cpp ---

def SPIRV_TranslatorCompat : Option<
    "translator-compatibility-mode",
    "SPIR-V Translator compatibility mode",
    "bool"> {
  let Default = "false";
  let Registry = SPIRVOptsReg;
}

// spirv-ext is NOT declared here: validating it needs SPIRVExtensionsParser,
// which lives in this backend's library.  A .td descriptor is a header-inline
// constexpr whose Validate body must be self-contained (see CLV2Options.td), so
// the descriptor is hand-written in SPIRVSubtarget.cpp instead.


// --- SPIRVAsmPrinter.cpp ---

def SPIRVFPContractModeOn : EnumValue<"on",
    "Follow IR metadata (default)"> {
  let CppName = "On";
}

def SPIRVFPContractModeOff : EnumValue<"off",
    "Force ContractionOff on all kernel entry points"> {
  let CppName = "Off";
}

def SPIRVFPContractModeFast : EnumValue<"fast",
    "Suppress ContractionOff on all kernel entry points"> {
  let CppName = "Fast";
}

def SPIRVFPContractModeEnum : EnumType<"SPIRVFPContractMode",
    [SPIRVFPContractModeOn, SPIRVFPContractModeOff, SPIRVFPContractModeFast]>;

def SPIRV_FPContract : Option<
    "spirv-fp-contract",
    "Override FP contraction policy for SPIR-V kernel entry points",
    "SPIRVFPContractMode"> {
  let Registry = SPIRVOptsReg;
  let Default = "on";
}

// --- SPIRVAuxDataHandler.cpp ---

def SPIRV_PreserveAuxData : Option<
    "spirv-preserve-auxdata",
    "Preserve LLVM attributes and metadata as "
    "NonSemantic.AuxData ExtInst annotations (requires "
    "SPV_KHR_non_semantic_info)",
    "bool"> {
  let Registry = SPIRVOptsReg;
  let Hidden = 1;
}
