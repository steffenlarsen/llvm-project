//===- SPIRVOptions.td - SPIRV target options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def SPIRVOptions : OptionsStruct<"llvm"> {}

// --- SPIRVCtorDtorLowering.cpp ---

defm SPIRV_LowerGlobalCtorDtorId : ValueField<
    "spirv-lower-global-ctor-dtor-id", "std::string", "\"\"",
    "Override unique ID of ctor/dtor globals.",
    /*hidden=*/1>;

defm SPIRV_EmitInitFiniKernel : BoolField<
    "spirv-emit-init-fini-kernel", "true",
    "Emit kernels to call ctor/dtor globals.",
    /*hidden=*/1>;

// --- SPIRVEmitIntrinsics.cpp ---

defm SPIRV_EmitOpNames : BoolField<
    "spirv-emit-op-names", "false",
    "Emit OpName for all instructions">;

// --- SPIRVModuleAnalysis.cpp ---

defm SPIRV_DumpDeps : BoolField<
    "spv-dump-deps", "false",
    "Dump MIR with SPIR-V dependencies info">;

// --- SPIRVTargetMachine.cpp ---

defm SPIRV_EnableNonSemanticDI : BoolField<
    "spv-emit-nonsemantic-debug-info", "false",
    "Deprecated. Use -g to emit SPIR-V NonSemantic.Shader.DebugInfo "
    "instructions">;

// --- SPIRVPrepareFunctions.cpp ---

// Hand-authored rather than ListField<>: ListField's default "-name value"
// (Separate) spelling is an Alias of the Joined "-name=value" spelling, so it
// unconditionally steals the next argv token as its value -- even for a bare
// "-spv-allow-unknown-intrinsics" with nothing attached. The old clv2
// ListOption had ValueOptional=1, which never steals a following separate
// token (see CommandLineV2.cpp's ValueOptional case), and
// llvm/test/CodeGen/SPIRV/allow_unknown_intrinsics.ll relies on the bare flag
// NOT consuming the next positional argument (the input file). So this pair
// mirrors OptionalValueField's Joined-plus-bare-Flag shape (no Separate
// alias) instead, adapted to a list-valued field.
def SPIRV_AllowUnknownIntrinsics : Joined<["-", "--"],
    "spv-allow-unknown-intrinsics=">,
    HelpText<"Emit unknown intrinsics as calls to external functions. A "
             "comma-separated input list of intrinsic prefixes must be "
             "provided, and only intrinsics carrying a listed prefix get "
             "emitted as described.">,
    Field<"std::vector<std::string>", "SPIRV_AllowUnknownIntrinsics", "{}">,
    ListElement<"std::string", /*commaSplit=*/1, /*alwaysPrefix=*/0>,
    MetaVarName<"intrinsic_prefix_0,intrinsic_prefix_1">;
def SPIRV_AllowUnknownIntrinsics_bare : Flag<["-", "--"],
    "spv-allow-unknown-intrinsics">,
    Flags<[HelpHidden]>,
    Assign<"SPIRV_AllowUnknownIntrinsics", "{}">;

// --- SPIRVSubtarget.cpp ---

defm SPIRV_TranslatorCompat : BoolField<
    "translator-compatibility-mode", "false",
    "SPIR-V Translator compatibility mode">;

// spirv-ext is NOT declared here: validating it needs SPIRVExtensionsParser,
// which lives in this backend's library, so the descriptor is hand-written
// in SPIRVSubtarget.cpp instead.

// --- SPIRVAsmPrinter.cpp ---

def SPIRVFPContractModeEnum : OptionEnum<"SPIRVFPContractMode", [
    EnumMember<"On", "on">,
    EnumMember<"Off", "off">,
    EnumMember<"Fast", "fast">]>;

defm SPIRV_FPContract : EnumField<
    "spirv-fp-contract", SPIRVFPContractModeEnum, "SPIRVFPContractMode::On",
    "Override FP contraction policy for SPIR-V kernel entry points">;

// --- SPIRVAuxDataHandler.cpp ---

defm SPIRV_PreserveAuxData : BoolField<
    "spirv-preserve-auxdata", "false",
    "Preserve LLVM attributes and metadata as "
    "NonSemantic.AuxData ExtInst annotations (requires "
    "SPV_KHR_non_semantic_info)",
    /*hidden=*/1>;
