//===- NVPTXOptions.td - NVPTX target options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def NVPTXOptions : OptionsStruct<"llvm"> {}

// --- NVPTXTargetMachine.cpp ---

defm NVPTX_DisableLoadStoreVectorizer : BoolField<
    "disable-nvptx-load-store-vectorizer", "false",
    "Disable load/store vectorizer",
    /*hidden=*/1>;

defm NVPTX_DisableIRPeephole : BoolField<
    "disable-nvptx-ir-peephole", "false",
    "Disable NVPTX IR Peephole",
    /*hidden=*/1>;

defm NVPTX_DisableRequireStructuredCFG : BoolField<
    "disable-nvptx-require-structured-cfg", "false",
    "Transitional flag to turn off NVPTX's requirement on preserving "
    "structured CFG. The requirement should be disabled only when "
    "unexpected regressions happen.",
    /*hidden=*/1>;

defm NVPTX_EarlyByValArgsCopy : BoolField<
    "nvptx-early-byval-copy", "false",
    "Create a copy of byval function arguments early.",
    /*hidden=*/1>;

// --- NVPTXCtorDtorLowering.cpp ---

defm NVPTX_LowerGlobalCtorDtorId : ValueField<
    "nvptx-lower-global-ctor-dtor-id", "std::string", "",
    "Override unique ID of ctor/dtor globals.",
    /*hidden=*/1>;

defm NVPTX_EmitInitFiniKernel : BoolField<
    "nvptx-emit-init-fini-kernel", "1",
    "Emit kernels to call ctor/dtor globals.",
    /*hidden=*/1>;

// --- NVPTXSubtarget.cpp ---

defm NVPTX_NoF16Math : BoolField<
    "nvptx-no-f16-math", "false",
    "NVPTX Specific: Disable generation of f16 math ops.",
    /*hidden=*/1>;

defm NVPTX_NoF32x2 : BoolField<
    "nvptx-no-f32x2", "false",
    "NVPTX Specific: Disable generation of "
    "f32x2 instructions and registers.",
    /*hidden=*/1>;

// --- NVPTXISelLowering.cpp ---

defm NVPTX_Sched4Reg : BoolField<
    "nvptx-sched4reg", "false",
    "NVPTX Specific: schedule for register pressue">;

// std::optional<unsigned>: the old clv2 option used wasOptSpecified() to
// distinguish "not given" (fall back to SDNodeFlags-based heuristics in
// NVPTXISelLowering.cpp) from an explicit value equal to the .td default
// (always honor it). Hand-written (rather than OptionalValueField<>) to
// keep the original Joined ("-nvptx-fma-level=N") + hidden Separate
// ("-nvptx-fma-level N") trio, mirroring ValueField's shape, with no bare
// flag (the original never had one).
def NVPTX_FMAContractLevel : Joined<["-", "--"], "nvptx-fma-level=">,
    HelpText<"NVPTX Specific: FMA contraction (0: don't do it"
             " 1: do it  2: do it aggressively">,
    Flags<[HelpHidden]>,
    Field<"std::optional<unsigned>", "NVPTX_FMAContractLevel",
          "std::nullopt">;
def NVPTX_FMAContractLevel_sep : Separate<["-", "--"], "nvptx-fma-level">,
    Flags<[HelpHidden]>, Alias<NVPTX_FMAContractLevel>;

// NVPTXDivPrecision is this library's own enum (distinct from the real
// NVPTX::DivPrecisionLevel defined in NVPTX.h, which has no "unspecified"
// state); the consumer static_casts the parsed value across, matching the
// old clv2 option's own EnumType and the consumer's existing static_cast.
// TargetDefault is a sentinel (never matched by real command-line input --
// its "" spelling only appears as the field's own default expression) that
// lets the consumer detect "the user didn't specify this option at all",
// mirroring the old clv2::wasOptSpecified() check that used to gate
// NVPTXTargetLowering::getDivF32Level()'s fallback to SDNodeFlags-based
// heuristics. TargetDefault is listed LAST (not first) so that the other
// four members keep ordinal values 0-3, matching NVPTX::DivPrecisionLevel
// exactly -- the consumer's static_cast relies on this ordinal alignment.
def NVPTXDivPrecisionApprox : EnumMember<"Approx", "0">;
def NVPTXDivPrecisionFull : EnumMember<"Full", "1">;
def NVPTXDivPrecisionIEEE754 : EnumMember<"IEEE754", "2">;
def NVPTXDivPrecisionIEEE754NoFTZ : EnumMember<"IEEE754_NoFTZ", "3">;
def NVPTXDivPrecisionTargetDefault : EnumMember<"TargetDefault", "">;

def NVPTXDivPrecisionEnum : OptionEnum<"NVPTXDivPrecision", [
    NVPTXDivPrecisionApprox, NVPTXDivPrecisionFull, NVPTXDivPrecisionIEEE754,
    NVPTXDivPrecisionIEEE754NoFTZ, NVPTXDivPrecisionTargetDefault]>;

defm NVPTX_PrecDivF32 : EnumField<
    "nvptx-prec-divf32", NVPTXDivPrecisionEnum,
    "NVPTXDivPrecision::TargetDefault",
    "NVPTX Specific: Override the precision of the lowering for f32 fdiv",
    /*hidden=*/1>;

// std::optional<bool>: same wasOptSpecified() rationale as
// NVPTX_FMAContractLevel above. Hand-written to keep the original Joined
// ("-nvptx-prec-sqrtf32=bool") + hidden Separate trio (mirroring
// ValueField's shape); the original never had a bare "-nvptx-prec-sqrtf32"
// flag, so this deliberately doesn't add one via BoolField<>/
// OptionalValueField<>.
def NVPTX_PrecSqrtF32 : Joined<["-", "--"], "nvptx-prec-sqrtf32=">,
    HelpText<"NVPTX Specific: 0 use sqrt.approx, 1 use sqrt.rn.">,
    Flags<[HelpHidden]>,
    Field<"std::optional<bool>", "NVPTX_PrecSqrtF32", "std::nullopt">;
def NVPTX_PrecSqrtF32_sep : Separate<["-", "--"], "nvptx-prec-sqrtf32">,
    Flags<[HelpHidden]>, Alias<NVPTX_PrecSqrtF32>;

defm NVPTX_ApproxLog2F32 : BoolField<
    "nvptx-approx-log2f32", "false",
    "NVPTX Specific: whether to use lg2.approx for log2">;

defm NVPTX_AllowFTZAtomics : BoolField<
    "nvptx-allow-ftz-atomics", "1",
    "Allow using native atomics even when FTZ behavior doesn't match",
    /*hidden=*/1>;

// --- NVPTXUtilities.cpp ---

defm NVPTX_ForceMinByValParamAlign : BoolField<
    "nvptx-force-min-byval-param-align", "false",
    "NVPTX Specific: force 4-byte minimal alignment for byval"
    " params of device functions.",
    /*hidden=*/1>;

// --- NVPTXAliasAnalysis.cpp ---

defm NVPTX_TraverseAddressSpacesLimit : ValueField<
    "nvptx-traverse-address-aliasing-limit", "unsigned", "6",
    "Depth limit for finding address space through traversal",
    /*hidden=*/1>;

// --- NVPTXDwarfDebug.cpp ---

defm NVPTX_LineInfoWithInlinedAt : BoolField<
    "line-info-inlined-at", "1",
    "Emit line with inlined_at enhancement for NVPTX",
    /*hidden=*/1>;

// --- NVPTXISelDAGToDAG.cpp ---

defm NVPTX_EnableRsqrtOpt : BoolField<
    "nvptx-rsqrt-approx-opt", "1",
    "Enable reciprocal sqrt optimization",
    /*hidden=*/1>;

defm NVPTX_EnableMADWide : BoolField<
    "nvptx-mad-wide-opt", "false",
    "Enable MAD wide optimization",
    /*hidden=*/1>;

// --- NVVMReflect.cpp ---

defm NVPTX_NVVMReflectEnabled : BoolField<
    "nvvm-reflect-enable", "1",
    "NVVM reflection, enabled by default",
    /*hidden=*/1>;

defm NVPTX_NVVMReflectList : ListField<
    "nvvm-reflect-add", "std::string",
    "A key=value pair. Replace __nvvm_reflect(name) with value.",
    /*commaJoined=*/1, /*appendEquals=*/1, /*hidden=*/1,
    /*metaVar=*/"name=<int>">;
