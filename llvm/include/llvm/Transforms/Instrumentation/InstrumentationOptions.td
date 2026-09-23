//===- InstrumentationOptions.td - Options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def InstrumentationOptions : OptionsStruct<"llvm"> {}

// =========================================================================
// External enum types
// =========================================================================

// --- AddressSanitizerOptions.h: AsanDetectStackUseAfterReturnMode ---
// Original C++ enum: { Never=0, Runtime=1, Always=2, Invalid=3 }

def AsanDetectStackUseAfterReturnModeEnum :
    OptionEnum<"AsanDetectStackUseAfterReturnMode", [
    EnumMember<"Never", "never">,
    EnumMember<"Runtime", "runtime">,
    EnumMember<"Always", "always">,
    EnumMember<"Invalid", "__invalid">]> {
  let External = 1;
}

// --- AddressSanitizerOptions.h: AsanCtorKind ---

def AsanCtorKindEnum : OptionEnum<"AsanCtorKind", [
    EnumMember<"None", "none">,
    EnumMember<"Global", "global">]> {
  let External = 1;
}

// --- AddressSanitizerOptions.h: AsanDtorKind ---
// Original C++ enum: { None=0, Global=1, Invalid=2 }

def AsanDtorKindEnum : OptionEnum<"AsanDtorKind", [
    EnumMember<"None", "none">,
    EnumMember<"Global", "global">,
    EnumMember<"Invalid", "__invalid">]> {
  let External = 1;
}

// --- ProfCorrelatorKind.h: ProfCorrelatorKind ---

def ProfCorrelatorKindEnum : OptionEnum<"ProfCorrelatorKind", [
    EnumMember<"NONE", "">,
    EnumMember<"DEBUG_INFO", "debug-info">,
    EnumMember<"BINARY", "binary">]> {
  let External = 1;
}

// --- BlockFrequencyInfo.h: PGOViewCountsType ---

def PGOViewCountsTypeEnum : OptionEnum<"PGOViewCountsType", [
    EnumMember<"PGOVCT_None", "none">,
    EnumMember<"PGOVCT_Graph", "graph">,
    EnumMember<"PGOVCT_Text", "text">]> {
  let External = 1;
}

// =========================================================================
// Inline enum types
// =========================================================================

// --- HWAddressSanitizer.cpp: OffsetKind ---
// Original C++ enum: { kFixed=0, kGlobal=1, kIfunc=2, kTls=3 }

def OffsetKindEnum : OptionEnum<"OffsetKind", [
    EnumMember<"kFixed", "__fixed">,
    EnumMember<"kGlobal", "global">,
    EnumMember<"kIfunc", "ifunc">,
    EnumMember<"kTls", "tls">]>;

// --- HWAddressSanitizer.cpp: RecordStackHistoryMode ---

def RecordStackHistoryModeEnum : OptionEnum<"RecordStackHistoryMode", [
    EnumMember<"none", "none">,
    EnumMember<"instr", "instr">,
    EnumMember<"libcall", "libcall">]>;

// =========================================================================
// List options
// =========================================================================

defm INST_AsanInstrumentAddrSpaces : ListField<
    "asan-instrument-address-spaces", "unsigned",
    "Only instrument variables in the specified address spaces.",
    /*commaJoined=*/1, /*appendEquals=*/1, /*hidden=*/1>;

defm INST_DfsanABIListFiles : ListField<
    "dfsan-abilist", "std::string",
    "File listing native ABI functions and how the pass treats them",
    /*commaJoined=*/0, /*appendEquals=*/1, /*hidden=*/1>;

defm INST_DfsanCombineTaintLookupTable : ListField<
    "dfsan-combine-taint-lookup-table", "std::string",
    "When dfsan-combine-offset-labels-on-gep and/or "
    "dfsan-combine-pointer-labels-on-load are false, this flag can "
    "be used to re-enable combining offset and/or pointer taint when "
    "loading specific constant global variables (i.e. lookup tables).",
    /*commaJoined=*/0, /*appendEquals=*/1, /*hidden=*/1>;

defm INST_ICPIgnoredBaseTypes : ListField<
    "icp-ignored-base-types", "std::string",
    "A list of mangled base type names. If a virtual call's base type "
    "is in this list, the call will not be promoted.",
    /*commaJoined=*/0, /*appendEquals=*/1, /*hidden=*/1>;

defm INST_ProfileContextRoot : ListField<
    "profile-context-root", "std::string",
    "A set of context roots.",
    /*commaJoined=*/0, /*appendEquals=*/1, /*hidden=*/1>;

defm INST_CtxPGOSkipCallsiteInstrument : ListField<
    "ctx-prof-skip-callsite-instr", "std::string",
    "Do not instrument callsites to functions in this list. Intended "
    "for testing.",
    /*commaJoined=*/0, /*appendEquals=*/1, /*hidden=*/1>;

// =========================================================================
// Options
// =========================================================================

// ===== AddressSanitizer.cpp =====

defm INST_AsanKernel : OptionalValueField<
    "asan-kernel", "bool", "true",
    "Enable KernelAddressSanitizer instrumentation", /*hidden=*/1>;

defm INST_AsanRecover : OptionalValueField<
    "asan-recover", "bool", "true",
    "Enable recovery mode (continue-after-error).", /*hidden=*/1>;

defm INST_AsanGuardAgainstVersionMismatch : OptionalValueField<
    "asan-guard-against-version-mismatch", "bool", "true",
    "Guard against compiler/runtime version mismatch.", /*hidden=*/1>;

defm INST_AsanInstrumentReads : BoolField<
    "asan-instrument-reads", "true",
    "instrument read instructions", /*hidden=*/1>;

defm INST_AsanInstrumentWrites : BoolField<
    "asan-instrument-writes", "true",
    "instrument write instructions", /*hidden=*/1>;

defm INST_AsanUseStackSafety : BoolField<
    "asan-use-stack-safety", "true",
    "Use Stack Safety analysis results", /*hidden=*/1>;

defm INST_AsanInstrumentAtomics : BoolField<
    "asan-instrument-atomics", "true",
    "instrument atomic instructions (rmw, cmpxchg)", /*hidden=*/1>;

defm INST_AsanInstrumentByval : BoolField<
    "asan-instrument-byval", "true",
    "instrument byval call arguments", /*hidden=*/1>;

defm INST_AsanAlwaysSlowPath : BoolField<
    "asan-always-slow-path", "false",
    "use instrumentation with slow path for all accesses", /*hidden=*/1>;

defm INST_AsanForceDynamicShadow : BoolField<
    "asan-force-dynamic-shadow", "false",
    "Load shadow address into a local variable for each function", /*hidden=*/1>;

defm INST_AsanWithIfunc : BoolField<
    "asan-with-ifunc", "true",
    "Access dynamic shadow through an ifunc global on "
    "platforms that support this", /*hidden=*/1>;

defm INST_AsanShadowAddrSpace : ValueField<
    "asan-shadow-addr-space", "int", "0",
    "Address space for pointers to the shadow map", /*hidden=*/1>;

defm INST_AsanWithIfuncSuppressRemat : BoolField<
    "asan-with-ifunc-suppress-remat", "true",
    "Suppress rematerialization of dynamic shadow address by passing "
    "it through inline asm in prologue.", /*hidden=*/1>;

defm INST_AsanMaxInsPerBB : ValueField<
    "asan-max-ins-per-bb", "int", "10000",
    "maximal number of instructions to instrument in any given BB", /*hidden=*/1>;

defm INST_AsanStack : BoolField<
    "asan-stack", "true",
    "Handle stack memory", /*hidden=*/1>;

defm INST_AsanMaxInlinePoisoningSize : OptionalField<
    "asan-max-inline-poisoning-size", "uint32_t",
    "Inline shadow poisoning for blocks up to the given size in bytes.", /*hidden=*/1>;

// std::optional<AsanDetectStackUseAfterReturnMode>: genuine tri-state, see migration notes.
def INST_AsanUseAfterReturn : Joined<["-", "--"], "asan-use-after-return=">,
    HelpText<"Sets the mode of detection for stack-use-after-return.">,
    Flags<[HelpHidden]>,
    Field<"std::optional<AsanDetectStackUseAfterReturnMode>", "INST_AsanUseAfterReturn", "std::nullopt">,
    FieldEnum<AsanDetectStackUseAfterReturnModeEnum>;
def INST_AsanUseAfterReturn_sep : Separate<["-", "--"], "asan-use-after-return">, Flags<[HelpHidden]>,
    Alias<!cast<Option>(INST_AsanUseAfterReturn)>;

defm INST_AsanRedzoneByvalArgs : BoolField<
    "asan-redzone-byval-args", "true",
    "Create redzones for byval arguments (extra copy required)", /*hidden=*/1>;

defm INST_AsanUseAfterScope : BoolField<
    "asan-use-after-scope", "false",
    "Check stack-use-after-scope", /*hidden=*/1>;

defm INST_AsanGlobals : BoolField<
    "asan-globals", "true",
    "Handle global objects", /*hidden=*/1>;

defm INST_AsanInitializers : BoolField<
    "asan-initialization-order", "true",
    "Handle C++ initializer order", /*hidden=*/1>;

defm INST_AsanDetectInvalidPointerPair : BoolField<
    "asan-detect-invalid-pointer-pair", "false",
    "Instrument <, <=, >, >=, - with pointer operands", /*hidden=*/1>;

defm INST_AsanDetectInvalidPointerCmp : BoolField<
    "asan-detect-invalid-pointer-cmp", "false",
    "Instrument <, <=, >, >= with pointer operands", /*hidden=*/1>;

defm INST_AsanDetectInvalidPointerSub : BoolField<
    "asan-detect-invalid-pointer-sub", "false",
    "Instrument - operations with pointer operands", /*hidden=*/1>;

defm INST_AsanRealignStack : ValueField<
    "asan-realign-stack", "unsigned", "32u",
    "Realign stack to the value of this flag (power of two)", /*hidden=*/1>;

defm INST_AsanInstrumentationWithCallThreshold : OptionalField<
    "asan-instrumentation-with-call-threshold", "int",
    "If the function being instrumented contains more than "
    "this number of memory accesses, use callbacks instead of "
    "inline checks (-1 means never use callbacks).", /*hidden=*/1>;

// Getter fallback literal matches this option's plain default with no other genuine specified-consumer; collapses to a plain field.
defm INST_AsanMemoryAccessCallbackPrefix : ValueField<
    "asan-memory-access-callback-prefix", "std::string", "\"__asan_\"",
    "Prefix for memory access callbacks", /*hidden=*/1>;

defm INST_AsanKernelMemIntrinsicPrefix : BoolField<
    "asan-kernel-mem-intrinsic-prefix", "false",
    "Use prefix for memory intrinsics in KASAN mode", /*hidden=*/1>;

defm INST_AsanInstrumentDynamicAllocas : BoolField<
    "asan-instrument-dynamic-allocas", "true",
    "instrument dynamic allocas", /*hidden=*/1>;

defm INST_AsanSkipPromotableAllocas : BoolField<
    "asan-skip-promotable-allocas", "true",
    "Do not instrument promotable allocas", /*hidden=*/1>;

// std::optional<AsanCtorKind>: genuine tri-state, see migration notes.
def INST_AsanConstructorKind : Joined<["-", "--"], "asan-constructor-kind=">,
    HelpText<"Sets the ASan constructor kind">,
    Flags<[HelpHidden]>,
    Field<"std::optional<AsanCtorKind>", "INST_AsanConstructorKind", "std::nullopt">,
    FieldEnum<AsanCtorKindEnum>;
def INST_AsanConstructorKind_sep : Separate<["-", "--"], "asan-constructor-kind">, Flags<[HelpHidden]>,
    Alias<!cast<Option>(INST_AsanConstructorKind)>;

defm INST_AsanMappingScale : OptionalField<
    "asan-mapping-scale", "int",
    "scale of asan shadow mapping", /*hidden=*/1>;

defm INST_AsanMappingOffset : OptionalField<
    "asan-mapping-offset", "uint64_t",
    "offset of asan shadow mapping [EXPERIMENTAL]", /*hidden=*/1>;

defm INST_AsanOpt : BoolField<
    "asan-opt", "true",
    "Optimize instrumentation", /*hidden=*/1>;

defm INST_AsanOptimizeCallbacks : BoolField<
    "asan-optimize-callbacks", "false",
    "Optimize callbacks", /*hidden=*/1>;

defm INST_AsanOptSameTemp : BoolField<
    "asan-opt-same-temp", "true",
    "Instrument the same temp just once", /*hidden=*/1>;

defm INST_AsanOptGlobals : BoolField<
    "asan-opt-globals", "true",
    "Don't instrument scalar globals", /*hidden=*/1>;

defm INST_AsanOptStack : BoolField<
    "asan-opt-stack", "false",
    "Don't instrument scalar stack variables", /*hidden=*/1>;

defm INST_AsanDynamicAllocaStack : BoolField<
    "asan-stack-dynamic-alloca", "true",
    "Use dynamic alloca to represent stack variables", /*hidden=*/1>;

defm INST_AsanForceExperiment : ValueField<
    "asan-force-experiment", "uint32_t", "(uint32_t)0",
    "Force optimization experiment (for testing)", /*hidden=*/1>;

defm INST_AsanUsePrivateAlias : OptionalValueField<
    "asan-use-private-alias", "bool", "true",
    "Use private aliases for global variables", /*hidden=*/1>;

defm INST_AsanUseOdrIndicator : OptionalValueField<
    "asan-use-odr-indicator", "bool", "true",
    "Use odr indicators to improve ODR reporting", /*hidden=*/1>;

// Dead: the generated specified-wrapper for this option is never called anywhere; collapses to a plain flag (see migration notes).
defm INST_AsanGlobalsLiveSupport : BoolField<
    "asan-globals-live-support", "true",
    "Use linker features to support dead code stripping of globals", /*hidden=*/1>;

defm INST_AsanWithComdat : BoolField<
    "asan-with-comdat", "true",
    "Place ASan constructors in comdat sections", /*hidden=*/1>;

defm INST_AsanDestructorKind : EnumField<
    "asan-destructor-kind", AsanDtorKindEnum, "AsanDtorKind::Invalid",
    "Sets the ASan destructor kind. The default is to use the value "
    "provided to the pass constructor", /*hidden=*/1>;

defm INST_AsanDebug : ValueField<
    "asan-debug", "int", "0",
    "debug", /*hidden=*/1>;

defm INST_AsanDebugStack : ValueField<
    "asan-debug-stack", "int", "0",
    "debug stack", /*hidden=*/1>;

// Getter fallback literal matches this option's plain default with no other genuine specified-consumer; collapses to a plain field.
defm INST_AsanDebugFunc : ValueField<
    "asan-debug-func", "std::string", "\"\"",
    "Debug func", /*hidden=*/1>;

defm INST_AsanDebugMin : ValueField<
    "asan-debug-min", "int", "-1",
    "Debug min inst", /*hidden=*/1>;

defm INST_AsanDebugMax : ValueField<
    "asan-debug-max", "int", "-1",
    "Debug max inst", /*hidden=*/1>;

// ===== AllocToken.cpp =====

defm INST_AllocTokenPrefix : ValueField<
    "alloc-token-prefix", "std::string", "\"__alloc_token_\"",
    "The allocation function prefix", /*hidden=*/1>;

defm INST_AllocTokenMax : OptionalField<
    "alloc-token-max", "uint64_t",
    "Maximum number of tokens (0 = target SIZE_MAX)", /*hidden=*/1>;

defm INST_AllocTokenFastABI : OptionalValueField<
    "alloc-token-fast-abi", "bool", "true",
    "The token ID is encoded in the function name", /*hidden=*/1>;

defm INST_AllocTokenExtended : OptionalValueField<
    "alloc-token-extended", "bool", "true",
    "Extend coverage to custom allocation functions", /*hidden=*/1>;

defm INST_AllocTokenCoverReplaceableNew : BoolField<
    "alloc-token-cover-replaceable-new", "true",
    "Cover replaceable operator new", /*hidden=*/1>;

defm INST_AllocTokenFallback : ValueField<
    "alloc-token-fallback", "uint64_t", "uint64_t{0}",
    "The default fallback token where none could be determined", /*hidden=*/1>;

// ===== BoundsChecking.cpp =====

defm INST_BoundsCheckingSingleTrap : BoolField<
    "bounds-checking-single-trap", "false",
    "Use one trap block per function", /*hidden=*/0>;

// ===== ControlHeightReduction.cpp =====

defm INST_DisableCHR : BoolField<
    "disable-chr", "false",
    "Disable CHR for all functions", /*hidden=*/1>;

defm INST_ForceCHR : BoolField<
    "force-chr", "false",
    "Apply CHR for all functions", /*hidden=*/1>;

defm INST_CHRBiasThreshold : ValueField<
    "chr-bias-threshold", "double", "0.99",
    "CHR considers a branch bias greater than this ratio as biased", /*hidden=*/1>;

defm INST_CHRMergeThreshold : ValueField<
    "chr-merge-threshold", "unsigned", "2",
    "CHR merges a group of N branches/selects where N >= this value", /*hidden=*/1>;

defm INST_CHRModuleList : ValueField<
    "chr-module-list", "std::string", "\"\"",
    "Specify file to retrieve the list of modules to apply CHR to", /*hidden=*/1>;

defm INST_CHRFunctionList : ValueField<
    "chr-function-list", "std::string", "\"\"",
    "Specify file to retrieve the list of functions to apply CHR to", /*hidden=*/1>;

defm INST_CHRDupThreshold : ValueField<
    "chr-dup-threshold", "unsigned", "3",
    "Max number of duplications by CHR for a region", /*hidden=*/1>;

// ===== DataFlowSanitizer.cpp =====

defm INST_DfsanPreserveAlignment : BoolField<
    "dfsan-preserve-alignment", "false",
    "respect alignment requirements provided by input IR", /*hidden=*/1>;

defm INST_DfsanCombinePointerLabelsOnLoad : BoolField<
    "dfsan-combine-pointer-labels-on-load", "true",
    "Combine the label of the pointer with the label of the data when "
    "loading from memory.", /*hidden=*/1>;

defm INST_DfsanCombinePointerLabelsOnStore : BoolField<
    "dfsan-combine-pointer-labels-on-store", "false",
    "Combine the label of the pointer with the label of the data when "
    "storing in memory.", /*hidden=*/1>;

defm INST_DfsanCombineOffsetLabelsOnGEP : BoolField<
    "dfsan-combine-offset-labels-on-gep", "true",
    "Combine the label of the offset with the label of the pointer when "
    "doing pointer arithmetic.", /*hidden=*/1>;

defm INST_DfsanDebugNonzeroLabels : BoolField<
    "dfsan-debug-nonzero-labels", "false",
    "Insert calls to __dfsan_nonzero_label on observing a parameter, "
    "load or return with a nonzero label", /*hidden=*/1>;

defm INST_DfsanEventCallbacks : BoolField<
    "dfsan-event-callbacks", "false",
    "Insert calls to __dfsan_*_callback functions on data events.", /*hidden=*/1>;

defm INST_DfsanConditionalCallbacks : BoolField<
    "dfsan-conditional-callbacks", "false",
    "Insert calls to callback functions on conditionals.", /*hidden=*/1>;

defm INST_DfsanReachesFunctionCallbacks : BoolField<
    "dfsan-reaches-function-callbacks", "false",
    "Insert calls to callback functions on data reaching a function.", /*hidden=*/1>;

defm INST_DfsanTrackSelectControlFlow : BoolField<
    "dfsan-track-select-control-flow", "true",
    "Propagate labels from condition values of select instructions "
    "to results.", /*hidden=*/1>;

defm INST_DfsanInstrumentWithCallThreshold : ValueField<
    "dfsan-instrument-with-call-threshold", "int", "3500",
    "If the function being instrumented requires more than "
    "this number of origin stores, use callbacks instead of "
    "inline checks (-1 means never use callbacks).", /*hidden=*/1>;

defm INST_DfsanTrackOrigins : ValueField<
    "dfsan-track-origins", "int", "0",
    "Track origins of labels", /*hidden=*/1>;

defm INST_DfsanIgnorePersonalityRoutine : BoolField<
    "dfsan-ignore-personality-routine", "false",
    "If a personality routine is marked uninstrumented from the ABI "
    "list, don't create a wrapper for it.", /*hidden=*/1>;

defm INST_DfsanAddGlobalNameSuffix : BoolField<
    "dfsan-add-global-name-suffix", "true",
    "Whether to add .dfsan suffix to global names", /*hidden=*/1>;

// ===== GCOVProfiling.cpp =====

defm INST_DefaultGCOVVersion : OptionalField<
    "default-gcov-version", "std::string",
    "", /*hidden=*/1>;

defm INST_GcovAtomicCounter : OptionalValueField<
    "gcov-atomic-counter", "bool", "true",
    "Make counter updates atomic", /*hidden=*/1>;

// ===== HWAddressSanitizer.cpp =====

// Getter fallback literal matches this option's plain default with no other genuine specified-consumer; collapses to a plain field.
defm INST_HwasanMemoryAccessCallbackPrefix : ValueField<
    "hwasan-memory-access-callback-prefix", "std::string", "\"__hwasan_\"",
    "Prefix for memory access callbacks", /*hidden=*/1>;

defm INST_HwasanKernelMemIntrinsicPrefix : BoolField<
    "hwasan-kernel-mem-intrinsic-prefix", "false",
    "Use prefix for memory intrinsics in KASAN mode", /*hidden=*/1>;

defm INST_HwasanInstrumentWithCalls : OptionalValueField<
    "hwasan-instrument-with-calls", "bool", "true",
    "instrument reads and writes with callbacks", /*hidden=*/1>;

defm INST_HwasanInstrumentReads : BoolField<
    "hwasan-instrument-reads", "true",
    "instrument read instructions", /*hidden=*/1>;

defm INST_HwasanInstrumentWrites : BoolField<
    "hwasan-instrument-writes", "true",
    "instrument write instructions", /*hidden=*/1>;

defm INST_HwasanInstrumentAtomics : BoolField<
    "hwasan-instrument-atomics", "true",
    "instrument atomic instructions (rmw, cmpxchg)", /*hidden=*/1>;

defm INST_HwasanInstrumentByval : BoolField<
    "hwasan-instrument-byval", "true",
    "instrument byval arguments", /*hidden=*/1>;

defm INST_HwasanRecover : OptionalValueField<
    "hwasan-recover", "bool", "true",
    "Enable recovery mode (continue-after-error).", /*hidden=*/1>;

defm INST_HwasanInstrumentStack : BoolField<
    "hwasan-instrument-stack", "true",
    "instrument stack (allocas)", /*hidden=*/1>;

defm INST_HwasanUseStackSafety : OptionalValueField<
    "hwasan-use-stack-safety", "bool", "true",
    "Use Stack Safety analysis results", /*hidden=*/1>;

defm INST_HwasanMaxLifetimes : ValueField<
    "hwasan-max-lifetimes-for-alloca", "size_t", "(size_t)3",
    "How many lifetime ends to handle for a single alloca.", /*hidden=*/1>;

defm INST_HwasanUseAfterScope : BoolField<
    "hwasan-use-after-scope", "true",
    "detect use after scope within function", /*hidden=*/1>;

defm INST_HwasanStrictUseAfterScope : BoolField<
    "hwasan-strict-use-after-scope", "true",
    "for complicated lifetimes, tag both on end and return", /*hidden=*/1>;

defm INST_HwasanGenerateTagsWithCalls : BoolField<
    "hwasan-generate-tags-with-calls", "false",
    "generate new tags with runtime library calls", /*hidden=*/1>;

defm INST_HwasanGlobals : OptionalValueField<
    "hwasan-globals", "bool", "true",
    "Instrument globals", /*hidden=*/1>;

defm INST_HwasanAllGlobals : BoolField<
    "hwasan-all-globals", "false",
    "Instrument globals, even those within user-defined sections.", /*hidden=*/1>;

defm INST_HwasanMatchAllTag : OptionalField<
    "hwasan-match-all-tag", "int",
    "don't report bad accesses via pointers with this tag", /*hidden=*/1>;

defm INST_HwasanKernel : OptionalValueField<
    "hwasan-kernel", "bool", "true",
    "Enable KernelHWAddressSanitizer instrumentation", /*hidden=*/1>;

defm INST_HwasanMappingOffset : OptionalField<
    "hwasan-mapping-offset", "uint64_t",
    "HWASan shadow mapping offset [EXPERIMENTAL]", /*hidden=*/1>;

// std::optional<OffsetKind>: genuine tri-state, see migration notes.
def INST_HwasanMappingOffsetDynamic : Joined<["-", "--"], "hwasan-mapping-offset-dynamic=">,
    HelpText<"HWASan shadow mapping dynamic offset location">,
    Flags<[HelpHidden]>,
    Field<"std::optional<OffsetKind>", "INST_HwasanMappingOffsetDynamic", "std::nullopt">,
    FieldEnum<OffsetKindEnum>;
def INST_HwasanMappingOffsetDynamic_sep : Separate<["-", "--"], "hwasan-mapping-offset-dynamic">, Flags<[HelpHidden]>,
    Alias<!cast<Option>(INST_HwasanMappingOffsetDynamic)>;

defm INST_HwasanFrameRecords : OptionalValueField<
    "hwasan-with-frame-record", "bool", "true",
    "Use ring buffer for stack allocations", /*hidden=*/1>;

defm INST_HwasanPercentileCutoffHot : OptionalField<
    "hwasan-percentile-cutoff-hot", "int",
    "Hot percentile cutoff.", /*hidden=*/0>;

defm INST_HwasanRandomRate : OptionalField<
    "hwasan-random-rate", "float",
    "Probability value in the range [0.0, 1.0] "
    "to keep instrumentation of a function.", /*hidden=*/0>;

defm INST_HwasanStaticLinking : BoolField<
    "hwasan-static-linking", "false",
    "Don't use .note.hwasan.globals section to instrument globals "
    "from loadable libraries.", /*hidden=*/1>;

// std::optional<RecordStackHistoryMode>: genuine tri-state, see migration notes.
def INST_HwasanRecordStackHistory : Joined<["-", "--"], "hwasan-record-stack-history=">,
    HelpText<"Record stack frames with tagged allocations in a thread-local "
    "ring buffer">,
    Flags<[HelpHidden]>,
    Field<"std::optional<RecordStackHistoryMode>", "INST_HwasanRecordStackHistory", "std::nullopt">,
    FieldEnum<RecordStackHistoryModeEnum>;
def INST_HwasanRecordStackHistory_sep : Separate<["-", "--"], "hwasan-record-stack-history">, Flags<[HelpHidden]>,
    Alias<!cast<Option>(INST_HwasanRecordStackHistory)>;

defm INST_HwasanInstrumentMemIntrinsics : BoolField<
    "hwasan-instrument-mem-intrinsics", "true",
    "instrument memory intrinsics", /*hidden=*/1>;

defm INST_HwasanInstrumentLandingPads : OptionalValueField<
    "hwasan-instrument-landing-pads", "bool", "true",
    "instrument landing pads", /*hidden=*/1>;

defm INST_HwasanUseShortGranules : OptionalValueField<
    "hwasan-use-short-granules", "bool", "true",
    "use short granules in allocas and outlined checks", /*hidden=*/1>;

defm INST_HwasanInstrumentPersonalityFunctions : OptionalValueField<
    "hwasan-instrument-personality-functions", "bool", "true",
    "instrument personality functions", /*hidden=*/1>;

defm INST_HwasanInlineAllChecks : OptionalValueField<
    "hwasan-inline-all-checks", "bool", "true",
    "inline all checks", /*hidden=*/1>;

defm INST_HwasanInlineFastPathChecks : OptionalValueField<
    "hwasan-inline-fast-path-checks", "bool", "true",
    "inline all checks", /*hidden=*/1>;

defm INST_HwasanUsePageAliases : BoolField<
    "hwasan-experimental-use-page-aliases", "false",
    "Use page aliasing in HWASan", /*hidden=*/1>;

defm INST_HwasanTagBits : ValueField<
    "hwasan-tag-bits", "uint64_t", "(uint64_t)0",
    "Restrict tag to at most N bits. Needs to be > 4.", /*hidden=*/1>;

// ===== IndirectCallPromotion.cpp =====

defm INST_DisableICP : BoolField<
    "disable-icp", "false",
    "Disable indirect call promotion", /*hidden=*/1>;

defm INST_ICPCutOff : ValueField<
    "icp-cutoff", "unsigned", "0",
    "Max number of promotions for this compilation", /*hidden=*/1>;

defm INST_ICPCSSkip : ValueField<
    "icp-csskip", "unsigned", "0",
    "Skip Callsite up to this number for this compilation", /*hidden=*/1>;

defm INST_ICPAllowDecls : BoolField<
    "icp-allow-decls", "false",
    "Promote the target candidate even when the definition "
    "is not available.", /*hidden=*/1>;

defm INST_HotFuncCutoffForICP : ValueField<
    "hot-func-cutoff-for-icp", "int", "-1",
    "A count is hot for indirect call promotion if it exceeds the minimum "
    "count to reach this percentile of total counts. Note that this percentile "
    "is specified as percentile * 10000 = HotFuncCutoffForICP. Default value "
    "-1 means that if the flag is unspecified then the value of "
    "ProfileSummaryCutoffHot will be used instead.", /*hidden=*/1>;

defm INST_ICPAllowHotOnly : BoolField<
    "icp-allow-hot-only", "true",
    "Promote the target candidate only if it is a "
    "hot callsite.", /*hidden=*/1>;

defm INST_ICPAllowCandidateSkip : BoolField<
    "icp-allow-candidate-skip", "false",
    "Continue with the remaining targets instead of exiting "
    "when encountering an unpromotable target.", /*hidden=*/1>;

defm INST_ICPLTOMode : BoolField<
    "icp-lto", "false",
    "Run indirect-call promotion in LTO mode", /*hidden=*/1>;

defm INST_ICPSamplePGOMode : BoolField<
    "icp-samplepgo", "false",
    "Run indirect-call promotion in SamplePGO mode", /*hidden=*/1>;

defm INST_ICPCallOnly : BoolField<
    "icp-call-only", "false",
    "Run indirect-call promotion for call instructions only", /*hidden=*/1>;

defm INST_ICPInvokeOnly : BoolField<
    "icp-invoke-only", "false",
    "Run indirect-call promotion for invoke instructions only", /*hidden=*/1>;

defm INST_ICPDumpAfter : BoolField<
    "icp-dumpafter", "false",
    "Dump IR after transformation happens", /*hidden=*/1>;

defm INST_ICPVTablePercentageThreshold : ValueField<
    "icp-vtable-percentage-threshold", "float", "0.995f",
    "The percentage threshold of vtable-count / function-count for "
    "comparing vtable candidate with function candidate.", /*hidden=*/1>;

defm INST_ICPMaxNumVTableLastCandidate : ValueField<
    "icp-max-num-vtable-last-candidate", "int", "1",
    "The maximum number of vtable for the last candidate.", /*hidden=*/1>;

// ===== InstrProfiling.cpp =====

// std::optional<ProfCorrelatorKind>: genuine tri-state, see migration notes.
def INST_ProfileCorrelate : Joined<["-", "--"], "profile-correlate=">,
    HelpText<"Use debug info or binary file to correlate profiles.">,
    Flags<[]>,
    Field<"std::optional<ProfCorrelatorKind>", "INST_ProfileCorrelate", "std::nullopt">,
    FieldEnum<ProfCorrelatorKindEnum>;
def INST_ProfileCorrelate_sep : Separate<["-", "--"], "profile-correlate">, Flags<[HelpHidden]>,
    Alias<!cast<Option>(INST_ProfileCorrelate)>;

defm INST_DoHashBasedCounterSplit : BoolField<
    "hash-based-counter-split", "true",
    "Rename counter variable of a comdat function based on cfg hash", /*hidden=*/0>;

defm INST_RuntimeCounterRelocation : OptionalValueField<
    "runtime-counter-relocation", "bool", "true",
    "Enable relocating counters at runtime.", /*hidden=*/0>;

defm INST_ValueProfileStaticAlloc : BoolField<
    "vp-static-alloc", "true",
    "Do static counter allocation for value profiler", /*hidden=*/0>;

defm INST_NumCountersPerValueSite : ValueField<
    "vp-counters-per-site", "double", "1.0",
    "The average number of profile counters allocated "
    "per value profiling site.", /*hidden=*/0>;

defm INST_AtomicCounterUpdateAll : BoolField<
    "instrprof-atomic-counter-update-all", "false",
    "Make all profile counter updates atomic (for testing only)", /*hidden=*/0>;

defm INST_VerifyAtomicPromotion : BoolField<
    "verify-atomic-counter-promoted", "false",
    "Check that all profile counter updates were made atomic; no-op "
    "if atomic updates are not requested (-fprofile-update=atomic)", /*hidden=*/0>;

defm INST_AtomicCounterUpdatePromoted : BoolField<
    "atomic-counter-update-promoted", "false",
    "Do counter update using atomic fetch add "
    "for promoted counters only", /*hidden=*/0>;

defm INST_AtomicFirstCounter : BoolField<
    "atomic-first-counter", "false",
    "Use atomic fetch add for first counter in a function (usually "
    "the entry counter)", /*hidden=*/0>;

defm INST_ConditionalCounterUpdate : BoolField<
    "conditional-counter-update", "false",
    "Do conditional counter updates in single byte counters mode)", /*hidden=*/0>;

defm INST_DoCounterPromotion : OptionalValueField<
    "do-counter-promotion", "bool", "true",
    "Do counter register promotion", /*hidden=*/0>;

defm INST_MaxNumOfPromotionsPerLoop : ValueField<
    "max-counter-promotions-per-loop", "unsigned", "20",
    "Max number counter promotions per loop to avoid "
    "increasing register pressure too much", /*hidden=*/0>;

defm INST_MaxNumOfPromotions : ValueField<
    "max-counter-promotions", "int", "-1",
    "Max number of allowed counter promotions", /*hidden=*/0>;

defm INST_SpeculativeCounterPromotionMaxExiting : ValueField<
    "speculative-counter-promotion-max-exiting", "unsigned", "3",
    "The max number of exiting blocks of a loop to allow "
    "speculative counter promotion", /*hidden=*/0>;

defm INST_SpeculativeCounterPromotionToLoop : BoolField<
    "speculative-counter-promotion-to-loop", "false",
    "When the option is false, if the target block is in a loop, "
    "the promotion will be disallowed unless the promoted counter "
    "update can be further/iteratively promoted into an acyclic "
    "region.", /*hidden=*/0>;

defm INST_OffloadPGOSampling : ValueField<
    "offload-pgo-sampling", "unsigned", "3",
    "Log2 of the sampling period for offload PGO instrumentation. "
    "Only 1 in every 2^N blocks is instrumented. "
    "0 = all blocks, 1 = 50%, 2 = 25%, 3 = 12.5% (default). "
    "Higher values reduce overhead at the cost of sparser profiles.", /*hidden=*/0>;

defm INST_IterativeCounterPromotion : BoolField<
    "iterative-counter-promotion", "true",
    "Allow counter promotion across the whole loop nest.", /*hidden=*/0>;

defm INST_SkipRetExitBlock : BoolField<
    "skip-ret-exit-block", "true",
    "Suppress counter promotion if exit blocks contain ret.", /*hidden=*/0>;

defm INST_SampledInstrumentation : OptionalValueField<
    "sampled-instrumentation", "bool", "true",
    "Do PGO instrumentation sampling", /*hidden=*/0>;

defm INST_SampledInstrPeriod : ValueField<
    "sampled-instr-period", "unsigned", "65536",
    "Set the profile instrumentation sample period.", /*hidden=*/0>;

defm INST_SampledInstrBurstDuration : ValueField<
    "sampled-instr-burst-duration", "unsigned", "200",
    "Set the profile instrumentation burst duration, which can range "
    "from 1 to the value of 'sampled-instr-period' (0 is invalid).", /*hidden=*/0>;

// ===== LowerAllowCheckPass.cpp =====

defm INST_LowerAllowCheckPercentileCutoffHot : OptionalField<
    "lower-allow-check-percentile-cutoff-hot", "int",
    "Hot percentile cutoff.", /*hidden=*/0>;

defm INST_LowerAllowCheckRandomRate : OptionalField<
    "lower-allow-check-random-rate", "float",
    "Probability value in the range [0.0, 1.0] of "
    "keeping a check.", /*hidden=*/0>;

// ===== MemorySanitizer.cpp =====

defm INST_MsanTrackOrigins : OptionalField<
    "msan-track-origins", "int",
    "Track origins (allocation sites) of poisoned memory", /*hidden=*/1>;

defm INST_MsanKeepGoing : OptionalValueField<
    "msan-keep-going", "bool", "true",
    "keep going after reporting a UMR", /*hidden=*/1>;

defm INST_MsanPoisonStack : BoolField<
    "msan-poison-stack", "true",
    "poison uninitialized stack variables", /*hidden=*/1>;

defm INST_MsanPoisonStackWithCall : BoolField<
    "msan-poison-stack-with-call", "false",
    "poison uninitialized stack variables with a call", /*hidden=*/1>;

defm INST_MsanPoisonStackPattern : ValueField<
    "msan-poison-stack-pattern", "int", "0xff",
    "poison uninitialized stack variables with the given pattern", /*hidden=*/1>;

defm INST_MsanPrintStackNames : BoolField<
    "msan-print-stack-names", "true",
    "Print name of local stack variable", /*hidden=*/1>;

defm INST_MsanPoisonUndef : BoolField<
    "msan-poison-undef", "true",
    "Poison fully undef temporary values.", /*hidden=*/1>;

defm INST_MsanPoisonUndefVectors : BoolField<
    "msan-poison-undef-vectors", "false",
    "Precisely poison partially undefined constant vectors.", /*hidden=*/1>;

defm INST_MsanPreciseDisjointOr : BoolField<
    "msan-precise-disjoint-or", "false",
    "Precisely poison disjoint OR.", /*hidden=*/1>;

defm INST_MsanHandleICmp : BoolField<
    "msan-handle-icmp", "true",
    "propagate shadow through ICmpEQ and ICmpNE", /*hidden=*/1>;

defm INST_MsanHandleICmpExact : BoolField<
    "msan-handle-icmp-exact", "true",
    "exact handling of relational integer ICmp", /*hidden=*/1>;

defm INST_MsanSwitchPrecision : ValueField<
    "msan-switch-precision", "int", "99",
    "Controls the number of cases considered by MSan for LLVM switch "
    "instructions.", /*hidden=*/1>;

defm INST_MsanHandleLifetimeIntrinsics : BoolField<
    "msan-handle-lifetime-intrinsics", "true",
    "when possible, poison scoped variables at the beginning of the "
    "scope (slower, but more precise)", /*hidden=*/1>;

defm INST_MsanHandleAsmConservative : BoolField<
    "msan-handle-asm-conservative", "true",
    "conservative handling of inline assembly", /*hidden=*/1>;

defm INST_MsanCheckAccessAddress : BoolField<
    "msan-check-access-address", "true",
    "report accesses through a pointer which has poisoned shadow", /*hidden=*/1>;

defm INST_MsanEagerChecks : OptionalValueField<
    "msan-eager-checks", "bool", "true",
    "check arguments and return values at function call boundaries", /*hidden=*/1>;

defm INST_MsanDumpStrictInstructions : BoolField<
    "msan-dump-strict-instructions", "false",
    "print out instructions with default strict semantics", /*hidden=*/1>;

defm INST_MsanDumpHeuristicInstructions : BoolField<
    "msan-dump-heuristic-instructions", "false",
    "Prints 'unknown' instructions that were handled heuristically.", /*hidden=*/1>;

defm INST_MsanInstrumentationWithCallThreshold : ValueField<
    "msan-instrumentation-with-call-threshold", "int", "3500",
    "If the function being instrumented requires more than "
    "this number of checks, use callbacks instead of inline checks.", /*hidden=*/1>;

defm INST_MsanKernel : OptionalValueField<
    "msan-kernel", "bool", "true",
    "Enable KernelMemorySanitizer instrumentation", /*hidden=*/1>;

defm INST_MsanDisableChecks : BoolField<
    "msan-disable-checks", "false",
    "Apply no_sanitize to the whole file", /*hidden=*/1>;

defm INST_MsanCheckConstantShadow : BoolField<
    "msan-check-constant-shadow", "true",
    "Insert checks for constant shadow values", /*hidden=*/1>;

defm INST_MsanWithComdat : BoolField<
    "msan-with-comdat", "false",
    "Place MSan constructors in comdat sections", /*hidden=*/1>;

defm INST_MsanAndMask : ValueField<
    "msan-and-mask", "uint64_t", "0",
    "Define custom MSan AndMask", /*hidden=*/1>;

defm INST_MsanXorMask : ValueField<
    "msan-xor-mask", "uint64_t", "0",
    "Define custom MSan XorMask", /*hidden=*/1>;

defm INST_MsanShadowBase : OptionalField<
    "msan-shadow-base", "uint64_t",
    "Define custom MSan ShadowBase", /*hidden=*/1>;

defm INST_MsanOriginBase : OptionalField<
    "msan-origin-base", "uint64_t",
    "Define custom MSan OriginBase", /*hidden=*/1>;

defm INST_MsanDisambiguateWarning : ValueField<
    "msan-disambiguate-warning-threshold", "int", "3",
    "Define threshold for number of checks per "
    "debug location to force origin update.", /*hidden=*/1>;

// ===== MemProfInstrumentation.cpp =====

defm INST_MemprofGuardAgainstVersionMismatch : BoolField<
    "memprof-guard-against-version-mismatch", "true",
    "Guard against compiler/runtime version mismatch.", /*hidden=*/1>;

defm INST_MemprofInstrumentReads : BoolField<
    "memprof-instrument-reads", "true",
    "instrument read instructions", /*hidden=*/1>;

defm INST_MemprofInstrumentWrites : BoolField<
    "memprof-instrument-writes", "true",
    "instrument write instructions", /*hidden=*/1>;

defm INST_MemprofInstrumentAtomics : BoolField<
    "memprof-instrument-atomics", "true",
    "instrument atomic instructions (rmw, cmpxchg)", /*hidden=*/1>;

defm INST_MemprofUseCallbacks : BoolField<
    "memprof-use-callbacks", "false",
    "Use callbacks instead of inline instrumentation sequences.", /*hidden=*/1>;

// Getter fallback literal matches this option's plain default with no other genuine specified-consumer; collapses to a plain field.
defm INST_MemprofMemoryAccessCallbackPrefix : ValueField<
    "memprof-memory-access-callback-prefix", "std::string", "\"__memprof_\"",
    "Prefix for memory access callbacks", /*hidden=*/1>;

defm INST_MemprofMappingScale : ValueField<
    "memprof-mapping-scale", "int", "3",
    "scale of memprof shadow mapping", /*hidden=*/1>;

defm INST_MemprofMappingGranularity : ValueField<
    "memprof-mapping-granularity", "int", "64",
    "granularity of memprof shadow mapping", /*hidden=*/1>;

defm INST_MemprofInstrumentStack : BoolField<
    "memprof-instrument-stack", "false",
    "Instrument scalar stack variables", /*hidden=*/1>;

defm INST_MemprofDebug : ValueField<
    "memprof-debug", "int", "0",
    "debug", /*hidden=*/1>;

// Getter fallback literal matches this option's plain default with no other genuine specified-consumer; collapses to a plain field.
defm INST_MemprofDebugFunc : ValueField<
    "memprof-debug-func", "std::string", "\"\"",
    "Debug func", /*hidden=*/1>;

defm INST_MemprofDebugMin : ValueField<
    "memprof-debug-min", "int", "-1",
    "Debug min inst", /*hidden=*/1>;

defm INST_MemprofDebugMax : ValueField<
    "memprof-debug-max", "int", "-1",
    "Debug max inst", /*hidden=*/1>;

defm INST_MemprofHistogram : BoolField<
    "memprof-histogram", "false",
    "Collect access count histograms", /*hidden=*/1>;

// Getter fallback literal matches this option's plain default with no other genuine specified-consumer; collapses to a plain field.
defm INST_MemprofRuntimeDefaultOptions : ValueField<
    "memprof-runtime-default-options", "std::string", "\"\"",
    "The default memprof options", /*hidden=*/1>;

// ===== MemProfUse.cpp =====

defm INST_MemprofMatchHotColdNew : BoolField<
    "memprof-match-hot-cold-new", "false",
    "Match allocation profiles onto existing hot/cold operator new calls", /*hidden=*/1>;

defm INST_MemprofPrintMatchInfo : BoolField<
    "memprof-print-match-info", "false",
    "Print matching stats for each allocation context in this "
    "module's profiles", /*hidden=*/1>;

defm INST_MemprofPrintMatchedAllocStack : BoolField<
    "memprof-print-matched-alloc-stack", "false",
    "Print full stack context for matched allocations "
    "with -memprof-print-match-info.", /*hidden=*/1>;

defm INST_MemprofPrintFunctionGuids : BoolField<
    "memprof-print-function-guids", "false",
    "Print function GUIDs computed for matching", /*hidden=*/1>;

defm INST_MemprofSalvageStaleProfile : BoolField<
    "memprof-salvage-stale-profile", "false",
    "Salvage stale MemProf profile", /*hidden=*/1>;

defm INST_MemprofAttachCalleeGuids : BoolField<
    "memprof-attach-calleeguids", "true",
    "Attach calleeguids as value profile metadata for indirect calls.", /*hidden=*/1>;

defm INST_MemprofMatchingColdThreshold : ValueField<
    "memprof-matching-cold-threshold", "unsigned", "100u",
    "Min percent of cold bytes matched to hint allocation cold", /*hidden=*/1>;

defm INST_MemprofAnnotateStaticDataPrefix : BoolField<
    "memprof-annotate-static-data-prefix", "false",
    "If true, annotate the static data section prefix", /*hidden=*/1>;

// ===== NumericalStabilitySanitizer.cpp =====

defm INST_NsanShadowTypeMapping : ValueField<
    "nsan-shadow-type-mapping", "std::string", "\"dqq\"",
    "One shadow type id for each of `float`, `double`, `long double`. "
    "`d`,`l`,`q`,`e` mean double, x86_fp80, fp128 (quad) and "
    "ppc_fp128 (extended double) respectively.", /*hidden=*/1>;

defm INST_NsanInstrumentFCmp : BoolField<
    "nsan-instrument-fcmp", "true",
    "Instrument floating-point comparisons", /*hidden=*/1>;

defm INST_CheckFunctionsFilter : ValueField<
    "check-functions-filter", "std::string", "\"\"",
    "Only emit checks for arguments of functions "
    "whose names match the given regular expression", /*hidden=*/0, "regex">;

defm INST_NsanTruncateFCmpEq : BoolField<
    "nsan-truncate-fcmp-eq", "true",
    "This flag controls the behaviour of fcmp equality comparisons.", /*hidden=*/1>;

defm INST_NsanCheckLoads : BoolField<
    "nsan-check-loads", "false",
    "Check floating-point load", /*hidden=*/1>;

defm INST_NsanCheckStores : BoolField<
    "nsan-check-stores", "true",
    "Check floating-point stores", /*hidden=*/1>;

defm INST_NsanCheckRet : BoolField<
    "nsan-check-ret", "true",
    "Check floating-point return values", /*hidden=*/1>;

defm INST_NsanPropagateNonFTConstStoresAsFT : BoolField<
    "nsan-propagate-non-ft-const-stores-as-ft", "false",
    "Propagate non floating-point const stores as floating point values. "
    "For debugging purposes only", /*hidden=*/1>;

// ===== PGOCtxProfLowering.cpp =====

// ===== PGOInstrumentation.cpp =====

defm INST_PGOTestProfileFile : OptionalField<
    "pgo-test-profile-file", "std::string",
    "Specify the path of profile data file. This is "
    "mainly for test purpose.", /*hidden=*/1, "filename">;

defm INST_PGOTestProfileRemappingFile : OptionalField<
    "pgo-test-profile-remapping-file", "std::string",
    "Specify the path of profile remapping file. This is mainly for "
    "test purpose.", /*hidden=*/1, "filename">;

defm INST_DisableValueProfiling : BoolField<
    "disable-vp", "false",
    "Disable Value Profiling", /*hidden=*/1>;

defm INST_ICPMaxAnnotations : ValueField<
    "icp-max-annotations", "unsigned", "3u",
    "Max number of annotations for a single indirect "
    "call callsite", /*hidden=*/1>;

defm INST_MemopMaxAnnotations : ValueField<
    "memop-max-annotations", "unsigned", "4u",
    "Max number of precise value annotations for a single memop "
    "intrinsic", /*hidden=*/1>;

defm INST_DoComdatRenaming : BoolField<
    "do-comdat-renaming", "false",
    "Append function hash to the name of COMDAT function to avoid "
    "function hash mismatch due to the preinliner", /*hidden=*/1>;

defm INST_PGOWarnMissing : BoolField<
    "pgo-warn-missing-function", "false",
    "Use this option to turn on/off warnings about missing profile "
    "data for functions.", /*hidden=*/1>;

defm INST_NoPGOWarnMismatch : BoolField<
    "no-pgo-warn-mismatch", "false",
    "Use this option to turn off/on warnings about profile cfg mismatch.", /*hidden=*/1>;

defm INST_NoPGOWarnMismatchComdatWeak : BoolField<
    "no-pgo-warn-mismatch-comdat-weak", "true",
    "The option is used to turn on/off warnings about hash mismatch "
    "for comdat or weak functions.", /*hidden=*/1>;

defm INST_PGOInstrSelect : BoolField<
    "pgo-instr-select", "true",
    "Use this option to turn on/off SELECT instruction instrumentation.", /*hidden=*/1>;

defm INST_PGOViewRawCounts : EnumField<
    "pgo-view-raw-counts", PGOViewCountsTypeEnum, "PGOViewCountsType::PGOVCT_None",
    "A boolean option to show CFG dag or text "
    "with raw profile counts from "
    "profile data. See also option "
    "-pgo-view-counts. To limit graph "
    "display to only one function, use "
    "filtering option -view-bfi-func-name.", /*hidden=*/1>;

defm INST_PGOInstrMemOP : BoolField<
    "pgo-instr-memop", "true",
    "Use this option to turn on/off "
    "memory intrinsic size profiling.", /*hidden=*/1>;

defm INST_PGOEmitBranchProb : BoolField<
    "pgo-emit-branch-prob", "false",
    "When this option is on, the annotated branch probability will be "
    "emitted as optimization remarks.", /*hidden=*/1>;

defm INST_PGOInstrumentEntry : OptionalValueField<
    "pgo-instrument-entry", "bool", "true",
    "Force to instrument function entry basicblock.", /*hidden=*/1>;

defm INST_PGOInstrumentLoopEntries : OptionalValueField<
    "pgo-instrument-loop-entries", "bool", "true",
    "Force to instrument loop entries.", /*hidden=*/1>;

defm INST_PGOFunctionEntryCoverage : BoolField<
    "pgo-function-entry-coverage", "false",
    "Use this option to enable function entry coverage instrumentation.", /*hidden=*/1>;

defm INST_PGOBlockCoverage : BoolField<
    "pgo-block-coverage", "false",
    "Use this option to enable basic block coverage instrumentation", /*hidden=*/0>;

defm INST_PGOViewBlockCoverageGraph : BoolField<
    "pgo-view-block-coverage-graph", "false",
    "Create a dot file of CFGs with block coverage inference information", /*hidden=*/0>;

defm INST_PGOTemporalInstrumentation : BoolField<
    "pgo-temporal-instrumentation", "false",
    "Use this option to enable temporal instrumentation", /*hidden=*/0>;

defm INST_PGOFixEntryCount : BoolField<
    "pgo-fix-entry-count", "true",
    "Fix function entry count in profile use.", /*hidden=*/1>;

defm INST_PGOVerifyHotBFI : BoolField<
    "pgo-verify-hot-bfi", "false",
    "Print out the non-match BFI count if a hot raw profile count "
    "becomes non-hot, or a cold raw profile count becomes hot.", /*hidden=*/1>;

defm INST_PGOVerifyBFI : BoolField<
    "pgo-verify-bfi", "false",
    "Print out mismatched BFI counts after setting profile metadata.", /*hidden=*/1>;

defm INST_PGOVerifyBFIRatio : ValueField<
    "pgo-verify-bfi-ratio", "unsigned", "2u",
    "Set the threshold for pgo-verify-bfi:  only print out "
    "mismatched BFI if the difference percentage is greater than "
    "this value (in percentage).", /*hidden=*/1>;

defm INST_PGOVerifyBFICutoff : ValueField<
    "pgo-verify-bfi-cutoff", "unsigned", "5u",
    "Set the threshold for pgo-verify-bfi: skip the counts whose "
    "profile count value is below.", /*hidden=*/1>;

defm INST_PGOTraceFuncHash : ValueField<
    "pgo-trace-func-hash", "std::string", "std::string(\"-\")",
    "Trace the hash of the function with this name.", /*hidden=*/1, "function name">;

defm INST_PGOFunctionSizeThreshold : ValueField<
    "pgo-function-size-threshold", "unsigned", "0u",
    "Do not instrument functions smaller than this threshold.", /*hidden=*/1>;

defm INST_PGOFunctionCriticalEdgeThreshold : ValueField<
    "pgo-critical-edge-threshold", "unsigned", "20000u",
    "Do not instrument functions with the number of critical edges "
    "greater than this threshold.", /*hidden=*/1>;

defm INST_PGOColdInstrumentEntryThreshold : ValueField<
    "pgo-cold-instrument-entry-threshold", "uint64_t", "uint64_t{0}",
    "For cold function instrumentation, skip instrumenting functions "
    "whose entry count is above the given value.", /*hidden=*/1>;

defm INST_PGOTreatUnknownAsCold : BoolField<
    "pgo-treat-unknown-as-cold", "false",
    "For cold function instrumentation, treat count unknown(e.g. "
    "unprofiled) functions as cold.", /*hidden=*/1>;

defm INST_PGOInstrumentColdFunctionOnly : BoolField<
    "pgo-instrument-cold-function-only", "false",
    "Enable cold function only instrumentation.", /*hidden=*/1>;

// ===== PGOMemOPSizeOpt.cpp =====

defm INST_PGOMemopCountThreshold : ValueField<
    "pgo-memop-count-threshold", "unsigned", "1000",
    "The minimum count to optimize memory intrinsic calls", /*hidden=*/1>;

defm INST_DisableMemopOpt : BoolField<
    "disable-memop-opt", "false",
    "Disable optimize", /*hidden=*/1>;

defm INST_PGOMemopPercentThreshold : ValueField<
    "pgo-memop-percent-threshold", "unsigned", "40",
    "The percentage threshold for the memory intrinsic calls "
    "optimization", /*hidden=*/1>;

defm INST_PGOMemopMaxVersion : ValueField<
    "pgo-memop-max-version", "unsigned", "3",
    "The max version for the optimized memory intrinsic calls", /*hidden=*/1>;

defm INST_PGOMemopScaleCount : BoolField<
    "pgo-memop-scale-count", "true",
    "Scale the memop size counts using the basic block count value", /*hidden=*/1>;

defm INST_PGOMemopOptMemcmpBcmp : BoolField<
    "pgo-memop-optimize-memcmp-bcmp", "true",
    "Size-specialize memcmp and bcmp calls", /*hidden=*/1>;

defm INST_MemopValueProfMaxOptSize : ValueField<
    "memop-value-prof-max-opt-size", "unsigned", "128",
    "Optimize the memop size <= this value", /*hidden=*/1>;

// ===== SanitizerBinaryMetadata.cpp =====

defm INST_SanitizerMetadataWeakCallbacks : BoolField<
    "sanitizer-metadata-weak-callbacks", "true",
    "Declare callbacks extern weak, and only call if non-null.", /*hidden=*/1>;

defm INST_SanitizerMetadataNosanitize : BoolField<
    "sanitizer-metadata-nosanitize-attr", "true",
    "Mark some metadata features uncovered in functions with "
    "disable_sanitizer_instrumentation attribute.", /*hidden=*/1>;

defm INST_SanitizerMetadataCovered : BoolField<
    "sanitizer-metadata-covered", "false",
    "Emit PCs for covered functions.", /*hidden=*/1>;

defm INST_SanitizerMetadataAtomics : BoolField<
    "sanitizer-metadata-atomics", "false",
    "Emit PCs for atomic operations.", /*hidden=*/1>;

defm INST_SanitizerMetadataUAR : BoolField<
    "sanitizer-metadata-uar", "false",
    "Emit PCs for start of functions that are subject to "
    "use-after-return checking.", /*hidden=*/1>;

// ===== SanitizerCoverage.cpp =====

defm INST_SanCovLevel : ValueField<
    "sanitizer-coverage-level", "int", "0",
    "Sanitizer Coverage. 0: none, 1: entry block, 2: all blocks, "
    "3: all blocks and critical edges", /*hidden=*/1>;

defm INST_SanCovTracePC : BoolField<
    "sanitizer-coverage-trace-pc", "false",
    "Experimental pc tracing", /*hidden=*/1>;

defm INST_SanCovTracePCEntryExit : BoolField<
    "sanitizer-coverage-trace-pc-entry-exit", "false",
    "pc tracing with separate entry/exit callbacks", /*hidden=*/1>;

defm INST_SanCovTracePCGuard : BoolField<
    "sanitizer-coverage-trace-pc-guard", "false",
    "pc tracing with a guard", /*hidden=*/1>;

defm INST_SanCovCreatePCTable : BoolField<
    "sanitizer-coverage-pc-table", "false",
    "create a static PC table", /*hidden=*/1>;

defm INST_SanCovInline8bitCounters : BoolField<
    "sanitizer-coverage-inline-8bit-counters", "false",
    "increments 8-bit counter for every edge", /*hidden=*/1>;

defm INST_SanCovDropCtors : BoolField<
    "sanitizer-coverage-drop-ctors", "false",
    "do not emit module ctors for global counters", /*hidden=*/1>;

defm INST_SanCovInlineBoolFlag : BoolField<
    "sanitizer-coverage-inline-bool-flag", "false",
    "sets a boolean flag for every edge", /*hidden=*/1>;

defm INST_SanCovTraceCompares : BoolField<
    "sanitizer-coverage-trace-compares", "false",
    "Tracing of CMP and similar instructions", /*hidden=*/1>;

defm INST_SanCovTraceDivs : BoolField<
    "sanitizer-coverage-trace-divs", "false",
    "Tracing of DIV instructions", /*hidden=*/1>;

defm INST_SanCovTraceLoads : BoolField<
    "sanitizer-coverage-trace-loads", "false",
    "Tracing of load instructions", /*hidden=*/1>;

defm INST_SanCovTraceStores : BoolField<
    "sanitizer-coverage-trace-stores", "false",
    "Tracing of store instructions", /*hidden=*/1>;

defm INST_SanCovTraceGeps : BoolField<
    "sanitizer-coverage-trace-geps", "false",
    "Tracing of GEP instructions", /*hidden=*/1>;

defm INST_SanCovPruneBlocks : BoolField<
    "sanitizer-coverage-prune-blocks", "true",
    "Reduce the number of instrumented blocks", /*hidden=*/1>;

defm INST_SanCovStackDepth : BoolField<
    "sanitizer-coverage-stack-depth", "false",
    "max stack depth tracing", /*hidden=*/1>;

defm INST_SanCovStackDepthCallbackMin : ValueField<
    "sanitizer-coverage-stack-depth-callback-min", "int", "0",
    "max stack depth tracing should use callback and only when "
    "stack depth more than specified", /*hidden=*/1>;

defm INST_SanCovControlFlow : BoolField<
    "sanitizer-coverage-control-flow", "false",
    "collect control flow for each function", /*hidden=*/1>;

defm INST_SanCovGatedCallbacks : BoolField<
    "sanitizer-coverage-gated-trace-callbacks", "false",
    "Gate the invocation of the tracing callbacks on a global variable. "
    "Currently only supported for trace-pc-guard and trace-cmp.", /*hidden=*/1>;

// ===== ThreadSanitizer.cpp =====

defm INST_TsanInstrumentMemoryAccesses : BoolField<
    "tsan-instrument-memory-accesses", "true",
    "Instrument memory accesses", /*hidden=*/1>;

defm INST_TsanInstrumentFuncEntryExit : BoolField<
    "tsan-instrument-func-entry-exit", "true",
    "Instrument function entry and exit", /*hidden=*/1>;

defm INST_TsanHandleCxxExceptions : BoolField<
    "tsan-handle-cxx-exceptions", "true",
    "Handle C++ exceptions (insert cleanup blocks for unwinding)", /*hidden=*/1>;

defm INST_TsanInstrumentAtomics : BoolField<
    "tsan-instrument-atomics", "true",
    "Instrument atomics", /*hidden=*/1>;

defm INST_TsanInstrumentMemIntrinsics : BoolField<
    "tsan-instrument-memintrinsics", "true",
    "Instrument memintrinsics (memset/memcpy/memmove)", /*hidden=*/1>;

defm INST_TsanDistinguishVolatile : BoolField<
    "tsan-distinguish-volatile", "false",
    "Emit special instrumentation for accesses to volatiles", /*hidden=*/1>;

defm INST_TsanInstrumentReadBeforeWrite : OptionalValueField<
    "tsan-instrument-read-before-write", "bool", "true",
    "Do not eliminate read instrumentation for read-before-writes", /*hidden=*/1>;

defm INST_TsanCompoundReadBeforeWrite : OptionalValueField<
    "tsan-compound-read-before-write", "bool", "true",
    "Emit special compound instrumentation for reads-before-writes", /*hidden=*/1>;

defm INST_TsanOmitNonCaptured : BoolField<
    "tsan-omit-by-pointer-capturing", "true",
    "Omit accesses due to pointer capturing", /*hidden=*/1>;

// ===== TypeSanitizer.cpp =====

defm INST_TysanWritesAlwaysSetType : BoolField<
    "tysan-writes-always-set-type", "false",
    "Writes always set the type", /*hidden=*/1>;

defm INST_TysanOutlineInstrumentation : BoolField<
    "tysan-outline-instrumentation", "true",
    "Uses function calls for all TySan instrumentation, reducing "
    "ELF size", /*hidden=*/1>;

defm INST_TysanVerifyOutlinedInstrumentation : BoolField<
    "tysan-verify-outlined-instrumentation", "false",
    "Check types twice with both inlined instrumentation and "
    "function calls. This verifies that they behave the same.", /*hidden=*/1>;
