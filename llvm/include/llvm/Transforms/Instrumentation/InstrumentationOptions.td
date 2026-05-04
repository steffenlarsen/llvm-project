//===- InstrumentationOptions.td - CLV2 option defs for Instrumentation -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def InstrumentationOptsReg : Registry<"llvm::inst_opts"> { let OutOfLineGetters = 1; }

// =========================================================================
// External enum types
// =========================================================================

// --- AddressSanitizerOptions.h: AsanDetectStackUseAfterReturnMode ---

def AsanUARModeNever : EnumValue<"never",
    "Never detect stack use after return."> {
  let CppName = "Never";
  let Value = 0;
}

def AsanUARModeRuntime : EnumValue<"runtime",
    "Detect stack use after return if binary flag "
    "'ASAN_OPTIONS=detect_stack_use_after_return' is set."> {
  let CppName = "Runtime";
  let Value = 1;
}

def AsanUARModeAlways : EnumValue<"always",
    "Always detect stack use after return."> {
  let CppName = "Always";
  let Value = 2;
}

// Original C++ enum: { Never=0, Runtime=1, Always=2, Invalid=3 }

def AsanUARModeInvalid : EnumValue<"__invalid",
    "Not a valid detect mode."> {
  let CppName = "Invalid";
  let Value = 3;
}

def AsanDetectStackUseAfterReturnModeEnum :
    EnumType<"AsanDetectStackUseAfterReturnMode",
    [AsanUARModeNever, AsanUARModeRuntime, AsanUARModeAlways,
     AsanUARModeInvalid]> {
  let External = 1;
}

// --- AddressSanitizerOptions.h: AsanCtorKind ---

def AsanCtorKindNone : EnumValue<"none", "No constructors"> {
  let CppName = "None";
}

def AsanCtorKindGlobal : EnumValue<"global", "Use global constructors"> {
  let CppName = "Global";
}

def AsanCtorKindEnum : EnumType<"AsanCtorKind",
    [AsanCtorKindNone, AsanCtorKindGlobal]> {
  let External = 1;
}

// --- AddressSanitizerOptions.h: AsanDtorKind ---
// Original C++ enum: { None=0, Global=1, Invalid=2 }

def AsanDtorKindNone : EnumValue<"none", "No destructors"> {
  let CppName = "None";
  let Value = 0;
}

def AsanDtorKindGlobal : EnumValue<"global", "Use global destructors"> {
  let CppName = "Global";
  let Value = 1;
}

def AsanDtorKindInvalid : EnumValue<"__invalid", "Not a valid destructor kind"> {
  let CppName = "Invalid";
  let Value = 2;
}

def AsanDtorKindEnum : EnumType<"AsanDtorKind",
    [AsanDtorKindNone, AsanDtorKindGlobal, AsanDtorKindInvalid]> {
  let External = 1;
}

// --- ProfCorrelatorKind.h: ProfCorrelatorKind ---

def ProfCorrelatorKindNone : EnumValue<"",
    "No profile correlation"> {
  let CppName = "NONE";
}

def ProfCorrelatorKindDebugInfo : EnumValue<"debug-info",
    "Use debug info to correlate"> {
  let CppName = "DEBUG_INFO";
}

def ProfCorrelatorKindBinary : EnumValue<"binary",
    "Use binary to correlate"> {
  let CppName = "BINARY";
}

def ProfCorrelatorKindEnum :
    EnumType<"ProfCorrelatorKind",
    [ProfCorrelatorKindNone, ProfCorrelatorKindDebugInfo,
     ProfCorrelatorKindBinary]> {
  let External = 1;
}

// --- BlockFrequencyInfo.h: PGOViewCountsType ---

def INST_PGOViewCountsTypeNone : EnumValue<"none", "do not show."> {
  let CppName = "PGOVCT_None";
}

def INST_PGOViewCountsTypeGraph : EnumValue<"graph", "show a graph."> {
  let CppName = "PGOVCT_Graph";
}

def INST_PGOViewCountsTypeText : EnumValue<"text", "show in text."> {
  let CppName = "PGOVCT_Text";
}

def INST_PGOViewCountsTypeEnum : EnumType<"PGOViewCountsType",
    [INST_PGOViewCountsTypeNone, INST_PGOViewCountsTypeGraph,
     INST_PGOViewCountsTypeText]> {
  let External = 1;
}

// =========================================================================
// Inline enum types
// =========================================================================

// --- HWAddressSanitizer.cpp: OffsetKind ---
// Original C++ enum: { kFixed=0, kGlobal=1, kIfunc=2, kTls=3 }

def HwasanOffsetKindFixed : EnumValue<"__fixed", "Use fixed offset"> {
  let CppName = "kFixed";
  let Value = 0;
}

def HwasanOffsetKindGlobal : EnumValue<"global", "Use global"> {
  let CppName = "kGlobal";
  let Value = 1;
}

def HwasanOffsetKindIfunc : EnumValue<"ifunc", "Use ifunc global"> {
  let CppName = "kIfunc";
  let Value = 2;
}

def HwasanOffsetKindTls : EnumValue<"tls", "Use TLS"> {
  let CppName = "kTls";
  let Value = 3;
}

def HwasanOffsetKindEnum : EnumType<"OffsetKind",
    [HwasanOffsetKindFixed, HwasanOffsetKindGlobal, HwasanOffsetKindIfunc,
     HwasanOffsetKindTls]>;

// --- HWAddressSanitizer.cpp: RecordStackHistoryMode ---

def HwasanRecordStackHistoryNone : EnumValue<"none",
    "Do not record stack ring history"> {
  let CppName = "none";
}

def HwasanRecordStackHistoryInstr : EnumValue<"instr",
    "Insert instructions into the prologue for "
    "storing into the stack ring buffer directly"> {
  let CppName = "instr";
}

def HwasanRecordStackHistoryLibcall : EnumValue<"libcall",
    "Add a call to __hwasan_add_frame_record for "
    "storing into the stack ring buffer"> {
  let CppName = "libcall";
}

def HwasanRecordStackHistoryModeEnum : EnumType<"RecordStackHistoryMode",
    [HwasanRecordStackHistoryNone, HwasanRecordStackHistoryInstr,
     HwasanRecordStackHistoryLibcall]>;

// =========================================================================
// List options
// =========================================================================

def INST_AsanInstrumentAddrSpaces : ListOption<
    "asan-instrument-address-spaces",
    "Only instrument variables in the specified address spaces.",
    "unsigned"> {
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
  let CommaSeparated = 1;
}

def INST_DfsanABIListFiles : ListOption<
    "dfsan-abilist",
    "File listing native ABI functions and how the pass treats them",
    "std::string"> {
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_DfsanCombineTaintLookupTable : ListOption<
    "dfsan-combine-taint-lookup-table",
    "When dfsan-combine-offset-labels-on-gep and/or "
    "dfsan-combine-pointer-labels-on-load are false, this flag can "
    "be used to re-enable combining offset and/or pointer taint when "
    "loading specific constant global variables (i.e. lookup tables).",
    "std::string"> {
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_ICPIgnoredBaseTypes : ListOption<
    "icp-ignored-base-types",
    "A list of mangled base type names. If a virtual call's base type "
    "is in this list, the call will not be promoted.",
    "std::string"> {
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_ProfileContextRoot : ListOption<
    "profile-context-root",
    "A set of context roots.",
    "std::string"> {
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_CtxPGOSkipCallsiteInstrument : ListOption<
    "ctx-prof-skip-callsite-instr",
    "Do not instrument callsites to functions in this list. Intended "
    "for testing.",
    "std::string"> {
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

// =========================================================================
// Options
// =========================================================================

// ===== AddressSanitizer.cpp =====

def INST_AsanKernel : Option<
    "asan-kernel",
    "Enable KernelAddressSanitizer instrumentation",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_AsanRecover : Option<
    "asan-recover",
    "Enable recovery mode (continue-after-error).",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_AsanGuardAgainstVersionMismatch : Option<
    "asan-guard-against-version-mismatch",
    "Guard against compiler/runtime version mismatch.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_AsanInstrumentReads : Option<
    "asan-instrument-reads",
    "instrument read instructions",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_AsanInstrumentWrites : Option<
    "asan-instrument-writes",
    "instrument write instructions",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_AsanUseStackSafety : Option<
    "asan-use-stack-safety",
    "Use Stack Safety analysis results",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_AsanInstrumentAtomics : Option<
    "asan-instrument-atomics",
    "instrument atomic instructions (rmw, cmpxchg)",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_AsanInstrumentByval : Option<
    "asan-instrument-byval",
    "instrument byval call arguments",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_AsanAlwaysSlowPath : Option<
    "asan-always-slow-path",
    "use instrumentation with slow path for all accesses",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_AsanForceDynamicShadow : Option<
    "asan-force-dynamic-shadow",
    "Load shadow address into a local variable for each function",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_AsanWithIfunc : Option<
    "asan-with-ifunc",
    "Access dynamic shadow through an ifunc global on "
    "platforms that support this",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_AsanShadowAddrSpace : Option<
    "asan-shadow-addr-space",
    "Address space for pointers to the shadow map",
    "int"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_AsanWithIfuncSuppressRemat : Option<
    "asan-with-ifunc-suppress-remat",
    "Suppress rematerialization of dynamic shadow address by passing "
    "it through inline asm in prologue.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_AsanMaxInsPerBB : Option<
    "asan-max-ins-per-bb",
    "maximal number of instructions to instrument in any given BB",
    "int"> {
  let Registry = InstrumentationOptsReg;
  let Default = "10000";
  let Hidden = 1;
}

def INST_AsanStack : Option<
    "asan-stack",
    "Handle stack memory",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_AsanMaxInlinePoisoningSize : Option<
    "asan-max-inline-poisoning-size",
    "Inline shadow poisoning for blocks up to the given size in bytes.",
    "uint32_t"> {
  let Registry = InstrumentationOptsReg;
  let Default = "uint32_t(64)";
  let Hidden = 1;
}

def INST_AsanUseAfterReturn : Option<
    "asan-use-after-return",
    "Sets the mode of detection for stack-use-after-return.",
    "AsanDetectStackUseAfterReturnMode"> {
  let Registry = InstrumentationOptsReg;
  let Default = "runtime";
  let Hidden = 1;
}

def INST_AsanRedzoneByvalArgs : Option<
    "asan-redzone-byval-args",
    "Create redzones for byval arguments (extra copy required)",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_AsanUseAfterScope : Option<
    "asan-use-after-scope",
    "Check stack-use-after-scope",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_AsanGlobals : Option<
    "asan-globals",
    "Handle global objects",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_AsanInitializers : Option<
    "asan-initialization-order",
    "Handle C++ initializer order",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_AsanDetectInvalidPointerPair : Option<
    "asan-detect-invalid-pointer-pair",
    "Instrument <, <=, >, >=, - with pointer operands",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_AsanDetectInvalidPointerCmp : Option<
    "asan-detect-invalid-pointer-cmp",
    "Instrument <, <=, >, >= with pointer operands",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_AsanDetectInvalidPointerSub : Option<
    "asan-detect-invalid-pointer-sub",
    "Instrument - operations with pointer operands",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_AsanRealignStack : Option<
    "asan-realign-stack",
    "Realign stack to the value of this flag (power of two)",
    "unsigned"> {
  let Registry = InstrumentationOptsReg;
  let Default = "32";
  let Hidden = 1;
}

def INST_AsanInstrumentationWithCallThreshold : Option<
    "asan-instrumentation-with-call-threshold",
    "If the function being instrumented contains more than "
    "this number of memory accesses, use callbacks instead of "
    "inline checks (-1 means never use callbacks).",
    "int"> {
  let Registry = InstrumentationOptsReg;
  let Default = "7000";
  let Hidden = 1;
}

def INST_AsanMemoryAccessCallbackPrefix : Option<
    "asan-memory-access-callback-prefix",
    "Prefix for memory access callbacks",
    "std::string"> {
  let Registry = InstrumentationOptsReg;
  let Default = "\"__asan_\"";
  let Hidden = 1;
}

def INST_AsanKernelMemIntrinsicPrefix : Option<
    "asan-kernel-mem-intrinsic-prefix",
    "Use prefix for memory intrinsics in KASAN mode",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_AsanInstrumentDynamicAllocas : Option<
    "asan-instrument-dynamic-allocas",
    "instrument dynamic allocas",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_AsanSkipPromotableAllocas : Option<
    "asan-skip-promotable-allocas",
    "Do not instrument promotable allocas",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_AsanConstructorKind : Option<
    "asan-constructor-kind",
    "Sets the ASan constructor kind",
    "AsanCtorKind"> {
  let Registry = InstrumentationOptsReg;
  let Default = "global";
  let Hidden = 1;
}

def INST_AsanMappingScale : Option<
    "asan-mapping-scale",
    "scale of asan shadow mapping",
    "int"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_AsanMappingOffset : Option<
    "asan-mapping-offset",
    "offset of asan shadow mapping [EXPERIMENTAL]",
    "uint64_t"> {
  let Registry = InstrumentationOptsReg;
  let Default = "uint64_t{0}";
  let Hidden = 1;
}

def INST_AsanOpt : Option<
    "asan-opt",
    "Optimize instrumentation",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_AsanOptimizeCallbacks : Option<
    "asan-optimize-callbacks",
    "Optimize callbacks",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_AsanOptSameTemp : Option<
    "asan-opt-same-temp",
    "Instrument the same temp just once",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_AsanOptGlobals : Option<
    "asan-opt-globals",
    "Don't instrument scalar globals",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_AsanOptStack : Option<
    "asan-opt-stack",
    "Don't instrument scalar stack variables",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_AsanDynamicAllocaStack : Option<
    "asan-stack-dynamic-alloca",
    "Use dynamic alloca to represent stack variables",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_AsanForceExperiment : Option<
    "asan-force-experiment",
    "Force optimization experiment (for testing)",
    "uint32_t"> {
  let Registry = InstrumentationOptsReg;
  let Default = "uint32_t(0)";
  let Hidden = 1;
}

def INST_AsanUsePrivateAlias : Option<
    "asan-use-private-alias",
    "Use private aliases for global variables",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_AsanUseOdrIndicator : Option<
    "asan-use-odr-indicator",
    "Use odr indicators to improve ODR reporting",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_AsanGlobalsLiveSupport : Option<
    "asan-globals-live-support",
    "Use linker features to support dead code stripping of globals",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_AsanWithComdat : Option<
    "asan-with-comdat",
    "Place ASan constructors in comdat sections",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_AsanDestructorKind : Option<
    "asan-destructor-kind",
    "Sets the ASan destructor kind. The default is to use the value "
    "provided to the pass constructor",
    "AsanDtorKind"> {
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_AsanDebug : Option<
    "asan-debug", "debug",
    "int"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_AsanDebugStack : Option<
    "asan-debug-stack", "debug stack",
    "int"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_AsanDebugFunc : Option<
    "asan-debug-func", "Debug func",
    "std::string"> {
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_AsanDebugMin : Option<
    "asan-debug-min", "Debug min inst",
    "int"> {
  let Registry = InstrumentationOptsReg;
  let Default = "-1";
  let Hidden = 1;
}

def INST_AsanDebugMax : Option<
    "asan-debug-max", "Debug max inst",
    "int"> {
  let Registry = InstrumentationOptsReg;
  let Default = "-1";
  let Hidden = 1;
}

// ===== AllocToken.cpp =====

def INST_AllocTokenPrefix : Option<
    "alloc-token-prefix",
    "The allocation function prefix",
    "std::string"> {
  let Registry = InstrumentationOptsReg;
  let Default = "\"__alloc_token_\"";
  let Hidden = 1;
}

def INST_AllocTokenMax : Option<
    "alloc-token-max",
    "Maximum number of tokens (0 = target SIZE_MAX)",
    "uint64_t"> {
  let Registry = InstrumentationOptsReg;
  let Default = "uint64_t{0}";
  let Hidden = 1;
}

def INST_AllocTokenFastABI : Option<
    "alloc-token-fast-abi",
    "The token ID is encoded in the function name",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_AllocTokenExtended : Option<
    "alloc-token-extended",
    "Extend coverage to custom allocation functions",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_AllocTokenCoverReplaceableNew : Option<
    "alloc-token-cover-replaceable-new",
    "Cover replaceable operator new",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_AllocTokenFallback : Option<
    "alloc-token-fallback",
    "The default fallback token where none could be determined",
    "uint64_t"> {
  let Registry = InstrumentationOptsReg;
  let Default = "uint64_t{0}";
  let Hidden = 1;
}

// ===== BoundsChecking.cpp =====

def INST_BoundsCheckingSingleTrap : Option<
    "bounds-checking-single-trap",
    "Use one trap block per function",
    "bool"> {
  let Default = "false";
  let Registry = InstrumentationOptsReg;
}

// ===== ControlHeightReduction.cpp =====

def INST_DisableCHR : Option<
    "disable-chr",
    "Disable CHR for all functions",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_ForceCHR : Option<
    "force-chr",
    "Apply CHR for all functions",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_CHRBiasThreshold : Option<
    "chr-bias-threshold",
    "CHR considers a branch bias greater than this ratio as biased",
    "double"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0.99";
  let Hidden = 1;
}

def INST_CHRMergeThreshold : Option<
    "chr-merge-threshold",
    "CHR merges a group of N branches/selects where N >= this value",
    "unsigned"> {
  let Registry = InstrumentationOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def INST_CHRModuleList : Option<
    "chr-module-list",
    "Specify file to retrieve the list of modules to apply CHR to",
    "std::string"> {
  let Registry = InstrumentationOptsReg;
  let Default = "\"\"";
  let Hidden = 1;
}

def INST_CHRFunctionList : Option<
    "chr-function-list",
    "Specify file to retrieve the list of functions to apply CHR to",
    "std::string"> {
  let Registry = InstrumentationOptsReg;
  let Default = "\"\"";
  let Hidden = 1;
}

def INST_CHRDupThreshold : Option<
    "chr-dup-threshold",
    "Max number of duplications by CHR for a region",
    "unsigned"> {
  let Registry = InstrumentationOptsReg;
  let Default = "3";
  let Hidden = 1;
}

// ===== DataFlowSanitizer.cpp =====

def INST_DfsanPreserveAlignment : Option<
    "dfsan-preserve-alignment",
    "respect alignment requirements provided by input IR",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_DfsanCombinePointerLabelsOnLoad : Option<
    "dfsan-combine-pointer-labels-on-load",
    "Combine the label of the pointer with the label of the data when "
    "loading from memory.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_DfsanCombinePointerLabelsOnStore : Option<
    "dfsan-combine-pointer-labels-on-store",
    "Combine the label of the pointer with the label of the data when "
    "storing in memory.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_DfsanCombineOffsetLabelsOnGEP : Option<
    "dfsan-combine-offset-labels-on-gep",
    "Combine the label of the offset with the label of the pointer when "
    "doing pointer arithmetic.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_DfsanDebugNonzeroLabels : Option<
    "dfsan-debug-nonzero-labels",
    "Insert calls to __dfsan_nonzero_label on observing a parameter, "
    "load or return with a nonzero label",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_DfsanEventCallbacks : Option<
    "dfsan-event-callbacks",
    "Insert calls to __dfsan_*_callback functions on data events.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_DfsanConditionalCallbacks : Option<
    "dfsan-conditional-callbacks",
    "Insert calls to callback functions on conditionals.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_DfsanReachesFunctionCallbacks : Option<
    "dfsan-reaches-function-callbacks",
    "Insert calls to callback functions on data reaching a function.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_DfsanTrackSelectControlFlow : Option<
    "dfsan-track-select-control-flow",
    "Propagate labels from condition values of select instructions "
    "to results.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_DfsanInstrumentWithCallThreshold : Option<
    "dfsan-instrument-with-call-threshold",
    "If the function being instrumented requires more than "
    "this number of origin stores, use callbacks instead of "
    "inline checks (-1 means never use callbacks).",
    "int"> {
  let Registry = InstrumentationOptsReg;
  let Default = "3500";
  let Hidden = 1;
}

def INST_DfsanTrackOrigins : Option<
    "dfsan-track-origins",
    "Track origins of labels",
    "int"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_DfsanIgnorePersonalityRoutine : Option<
    "dfsan-ignore-personality-routine",
    "If a personality routine is marked uninstrumented from the ABI "
    "list, don't create a wrapper for it.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_DfsanAddGlobalNameSuffix : Option<
    "dfsan-add-global-name-suffix",
    "Whether to add .dfsan suffix to global names",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// ===== GCOVProfiling.cpp =====

def INST_DefaultGCOVVersion : Option<
    "default-gcov-version", "",
    "std::string"> {
  let Registry = InstrumentationOptsReg;
  let Default = "\"0000\"";
  let Hidden = 1;
}

def INST_GcovAtomicCounter : Option<
    "gcov-atomic-counter",
    "Make counter updates atomic",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

// ===== HWAddressSanitizer.cpp =====

def INST_HwasanMemoryAccessCallbackPrefix : Option<
    "hwasan-memory-access-callback-prefix",
    "Prefix for memory access callbacks",
    "std::string"> {
  let Registry = InstrumentationOptsReg;
  let Default = "\"__hwasan_\"";
  let Hidden = 1;
}

def INST_HwasanKernelMemIntrinsicPrefix : Option<
    "hwasan-kernel-mem-intrinsic-prefix",
    "Use prefix for memory intrinsics in KASAN mode",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_HwasanInstrumentWithCalls : Option<
    "hwasan-instrument-with-calls",
    "instrument reads and writes with callbacks",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_HwasanInstrumentReads : Option<
    "hwasan-instrument-reads",
    "instrument read instructions",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_HwasanInstrumentWrites : Option<
    "hwasan-instrument-writes",
    "instrument write instructions",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_HwasanInstrumentAtomics : Option<
    "hwasan-instrument-atomics",
    "instrument atomic instructions (rmw, cmpxchg)",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_HwasanInstrumentByval : Option<
    "hwasan-instrument-byval",
    "instrument byval arguments",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_HwasanRecover : Option<
    "hwasan-recover",
    "Enable recovery mode (continue-after-error).",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_HwasanInstrumentStack : Option<
    "hwasan-instrument-stack",
    "instrument stack (allocas)",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_HwasanUseStackSafety : Option<
    "hwasan-use-stack-safety",
    "Use Stack Safety analysis results",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_HwasanMaxLifetimes : Option<
    "hwasan-max-lifetimes-for-alloca",
    "How many lifetime ends to handle for a single alloca.",
    "size_t"> {
  let Registry = InstrumentationOptsReg;
  let Default = "size_t(3)";
  let Hidden = 1;
  let ReallyHidden = 1;
}

def INST_HwasanUseAfterScope : Option<
    "hwasan-use-after-scope",
    "detect use after scope within function",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_HwasanStrictUseAfterScope : Option<
    "hwasan-strict-use-after-scope",
    "for complicated lifetimes, tag both on end and return",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_HwasanGenerateTagsWithCalls : Option<
    "hwasan-generate-tags-with-calls",
    "generate new tags with runtime library calls",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_HwasanGlobals : Option<
    "hwasan-globals",
    "Instrument globals",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_HwasanAllGlobals : Option<
    "hwasan-all-globals",
    "Instrument globals, even those within user-defined sections.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_HwasanMatchAllTag : Option<
    "hwasan-match-all-tag",
    "don't report bad accesses via pointers with this tag",
    "int"> {
  let Registry = InstrumentationOptsReg;
  let Default = "-1";
  let Hidden = 1;
}

def INST_HwasanKernel : Option<
    "hwasan-kernel",
    "Enable KernelHWAddressSanitizer instrumentation",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_HwasanMappingOffset : Option<
    "hwasan-mapping-offset",
    "HWASan shadow mapping offset [EXPERIMENTAL]",
    "uint64_t"> {
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_HwasanMappingOffsetDynamic : Option<
    "hwasan-mapping-offset-dynamic",
    "HWASan shadow mapping dynamic offset location",
    "OffsetKind"> {
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_HwasanFrameRecords : Option<
    "hwasan-with-frame-record",
    "Use ring buffer for stack allocations",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_HwasanPercentileCutoffHot : Option<
    "hwasan-percentile-cutoff-hot",
    "Hot percentile cutoff.",
    "int"> {
  let Registry = InstrumentationOptsReg;
}

def INST_HwasanRandomRate : Option<
    "hwasan-random-rate",
    "Probability value in the range [0.0, 1.0] "
    "to keep instrumentation of a function.",
    "float"> {
  let Registry = InstrumentationOptsReg;
}

def INST_HwasanStaticLinking : Option<
    "hwasan-static-linking",
    "Don't use .note.hwasan.globals section to instrument globals "
    "from loadable libraries.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_HwasanRecordStackHistory : Option<
    "hwasan-record-stack-history",
    "Record stack frames with tagged allocations in a thread-local "
    "ring buffer",
    "RecordStackHistoryMode"> {
  let Registry = InstrumentationOptsReg;
  let Default = "instr";
  let Hidden = 1;
}

def INST_HwasanInstrumentMemIntrinsics : Option<
    "hwasan-instrument-mem-intrinsics",
    "instrument memory intrinsics",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_HwasanInstrumentLandingPads : Option<
    "hwasan-instrument-landing-pads",
    "instrument landing pads",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_HwasanUseShortGranules : Option<
    "hwasan-use-short-granules",
    "use short granules in allocas and outlined checks",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_HwasanInstrumentPersonalityFunctions : Option<
    "hwasan-instrument-personality-functions",
    "instrument personality functions",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_HwasanInlineAllChecks : Option<
    "hwasan-inline-all-checks",
    "inline all checks",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_HwasanInlineFastPathChecks : Option<
    "hwasan-inline-fast-path-checks",
    "inline all checks",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_HwasanUsePageAliases : Option<
    "hwasan-experimental-use-page-aliases",
    "Use page aliasing in HWASan",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_HwasanTagBits : Option<
    "hwasan-tag-bits",
    "Restrict tag to at most N bits. Needs to be > 4.",
    "uint64_t"> {
  let Registry = InstrumentationOptsReg;
  let Default = "uint64_t{0}";
  let Hidden = 1;
}

// ===== IndirectCallPromotion.cpp =====

def INST_DisableICP : Option<
    "disable-icp",
    "Disable indirect call promotion",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_ICPCutOff : Option<
    "icp-cutoff",
    "Max number of promotions for this compilation",
    "unsigned"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_ICPCSSkip : Option<
    "icp-csskip",
    "Skip Callsite up to this number for this compilation",
    "unsigned"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_ICPAllowDecls : Option<
    "icp-allow-decls",
    "Promote the target candidate even when the definition "
    "is not available.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_HotFuncCutoffForICP : Option<
    "hot-func-cutoff-for-icp",
    "A count is hot for indirect call promotion if it exceeds the minimum "
    "count to reach this percentile of total counts. Note that this percentile "
    "is specified as percentile * 10000 = HotFuncCutoffForICP. Default value "
    "-1 means that if the flag is unspecified then the value of "
    "ProfileSummaryCutoffHot will be used instead.",
    "int"> {
  let Registry = InstrumentationOptsReg;
  let Default = "-1";
  let Hidden = 1;
}

def INST_ICPAllowHotOnly : Option<
    "icp-allow-hot-only",
    "Promote the target candidate only if it is a "
    "hot callsite.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_ICPAllowCandidateSkip : Option<
    "icp-allow-candidate-skip",
    "Continue with the remaining targets instead of exiting "
    "when encountering an unpromotable target.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_ICPLTOMode : Option<
    "icp-lto",
    "Run indirect-call promotion in LTO mode",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_ICPSamplePGOMode : Option<
    "icp-samplepgo",
    "Run indirect-call promotion in SamplePGO mode",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_ICPCallOnly : Option<
    "icp-call-only",
    "Run indirect-call promotion for call instructions only",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_ICPInvokeOnly : Option<
    "icp-invoke-only",
    "Run indirect-call promotion for invoke instructions only",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_ICPDumpAfter : Option<
    "icp-dumpafter",
    "Dump IR after transformation happens",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_ICPVTablePercentageThreshold : Option<
    "icp-vtable-percentage-threshold",
    "The percentage threshold of vtable-count / function-count for "
    "comparing vtable candidate with function candidate.",
    "float"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0.995f";
  let Hidden = 1;
}

def INST_ICPMaxNumVTableLastCandidate : Option<
    "icp-max-num-vtable-last-candidate",
    "The maximum number of vtable for the last candidate.",
    "int"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// ===== InstrProfiling.cpp =====

def INST_ProfileCorrelate : Option<
    "profile-correlate",
    "Use debug info or binary file to correlate profiles.",
    "ProfCorrelatorKind"> {
  let Registry = InstrumentationOptsReg;
}

def INST_DoHashBasedCounterSplit : Option<
    "hash-based-counter-split",
    "Rename counter variable of a comdat function based on cfg hash",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
}

def INST_RuntimeCounterRelocation : Option<
    "runtime-counter-relocation",
    "Enable relocating counters at runtime.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
}

def INST_ValueProfileStaticAlloc : Option<
    "vp-static-alloc",
    "Do static counter allocation for value profiler",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
}

def INST_NumCountersPerValueSite : Option<
    "vp-counters-per-site",
    "The average number of profile counters allocated "
    "per value profiling site.",
    "double"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1.0";
}

def INST_AtomicCounterUpdateAll : Option<
    "instrprof-atomic-counter-update-all",
    "Make all profile counter updates atomic (for testing only)",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
}

def INST_VerifyAtomicPromotion : Option<
    "verify-atomic-counter-promoted",
    "Check that all profile counter updates were made atomic; no-op "
    "if atomic updates are not requested (-fprofile-update=atomic)",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
}

def INST_AtomicCounterUpdatePromoted : Option<
    "atomic-counter-update-promoted",
    "Do counter update using atomic fetch add "
    "for promoted counters only",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
}

def INST_AtomicFirstCounter : Option<
    "atomic-first-counter",
    "Use atomic fetch add for first counter in a function (usually "
    "the entry counter)",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
}

def INST_ConditionalCounterUpdate : Option<
    "conditional-counter-update",
    "Do conditional counter updates in single byte counters mode)",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
}

def INST_DoCounterPromotion : Option<
    "do-counter-promotion",
    "Do counter register promotion",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
}

def INST_MaxNumOfPromotionsPerLoop : Option<
    "max-counter-promotions-per-loop",
    "Max number counter promotions per loop to avoid "
    "increasing register pressure too much",
    "unsigned"> {
  let Registry = InstrumentationOptsReg;
  let Default = "20";
}

def INST_MaxNumOfPromotions : Option<
    "max-counter-promotions",
    "Max number of allowed counter promotions",
    "int"> {
  let Registry = InstrumentationOptsReg;
  let Default = "-1";
}

def INST_SpeculativeCounterPromotionMaxExiting : Option<
    "speculative-counter-promotion-max-exiting",
    "The max number of exiting blocks of a loop to allow "
    "speculative counter promotion",
    "unsigned"> {
  let Registry = InstrumentationOptsReg;
  let Default = "3";
}

def INST_SpeculativeCounterPromotionToLoop : Option<
    "speculative-counter-promotion-to-loop",
    "When the option is false, if the target block is in a loop, "
    "the promotion will be disallowed unless the promoted counter "
    "update can be further/iteratively promoted into an acyclic "
    "region.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
}

def INST_OffloadPGOSampling : Option<
    "offload-pgo-sampling",
    "Log2 of the sampling period for offload PGO instrumentation. "
    "Only 1 in every 2^N blocks is instrumented. "
    "0 = all blocks, 1 = 50%, 2 = 25%, 3 = 12.5% (default). "
    "Higher values reduce overhead at the cost of sparser profiles.",
    "unsigned"> {
  let Registry = InstrumentationOptsReg;
  let Default = "3";
}

def INST_IterativeCounterPromotion : Option<
    "iterative-counter-promotion",
    "Allow counter promotion across the whole loop nest.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
}

def INST_SkipRetExitBlock : Option<
    "skip-ret-exit-block",
    "Suppress counter promotion if exit blocks contain ret.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
}

def INST_SampledInstrumentation : Option<
    "sampled-instrumentation",
    "Do PGO instrumentation sampling",
    "bool"> {
  let Registry = InstrumentationOptsReg;
}

def INST_SampledInstrPeriod : Option<
    "sampled-instr-period",
    "Set the profile instrumentation sample period.",
    "unsigned"> {
  let Registry = InstrumentationOptsReg;
  let Default = "65536";
}

def INST_SampledInstrBurstDuration : Option<
    "sampled-instr-burst-duration",
    "Set the profile instrumentation burst duration, which can range "
    "from 1 to the value of 'sampled-instr-period' (0 is invalid).",
    "unsigned"> {
  let Registry = InstrumentationOptsReg;
  let Default = "200";
}

// ===== LowerAllowCheckPass.cpp =====

def INST_LowerAllowCheckPercentileCutoffHot : Option<
    "lower-allow-check-percentile-cutoff-hot",
    "Hot percentile cutoff.",
    "int"> {
  let Registry = InstrumentationOptsReg;
}

def INST_LowerAllowCheckRandomRate : Option<
    "lower-allow-check-random-rate",
    "Probability value in the range [0.0, 1.0] of "
    "keeping a check.",
    "float"> {
  let Registry = InstrumentationOptsReg;
}

// ===== MemorySanitizer.cpp =====

def INST_MsanTrackOrigins : Option<
    "msan-track-origins",
    "Track origins (allocation sites) of poisoned memory",
    "int"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_MsanKeepGoing : Option<
    "msan-keep-going",
    "keep going after reporting a UMR",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_MsanPoisonStack : Option<
    "msan-poison-stack",
    "poison uninitialized stack variables",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_MsanPoisonStackWithCall : Option<
    "msan-poison-stack-with-call",
    "poison uninitialized stack variables with a call",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_MsanPoisonStackPattern : Option<
    "msan-poison-stack-pattern",
    "poison uninitialized stack variables with the given pattern",
    "int"> {
  let Registry = InstrumentationOptsReg;
  let Default = "255";
  let Hidden = 1;
}

def INST_MsanPrintStackNames : Option<
    "msan-print-stack-names",
    "Print name of local stack variable",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_MsanPoisonUndef : Option<
    "msan-poison-undef",
    "Poison fully undef temporary values.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_MsanPoisonUndefVectors : Option<
    "msan-poison-undef-vectors",
    "Precisely poison partially undefined constant vectors.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_MsanPreciseDisjointOr : Option<
    "msan-precise-disjoint-or",
    "Precisely poison disjoint OR.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_MsanHandleICmp : Option<
    "msan-handle-icmp",
    "propagate shadow through ICmpEQ and ICmpNE",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_MsanHandleICmpExact : Option<
    "msan-handle-icmp-exact",
    "exact handling of relational integer ICmp",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_MsanSwitchPrecision : Option<
    "msan-switch-precision",
    "Controls the number of cases considered by MSan for LLVM switch "
    "instructions.",
    "int"> {
  let Registry = InstrumentationOptsReg;
  let Default = "99";
  let Hidden = 1;
}

def INST_MsanHandleLifetimeIntrinsics : Option<
    "msan-handle-lifetime-intrinsics",
    "when possible, poison scoped variables at the beginning of the "
    "scope (slower, but more precise)",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_MsanHandleAsmConservative : Option<
    "msan-handle-asm-conservative",
    "conservative handling of inline assembly",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_MsanCheckAccessAddress : Option<
    "msan-check-access-address",
    "report accesses through a pointer which has poisoned shadow",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_MsanEagerChecks : Option<
    "msan-eager-checks",
    "check arguments and return values at function call boundaries",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_MsanDumpStrictInstructions : Option<
    "msan-dump-strict-instructions",
    "print out instructions with default strict semantics",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_MsanDumpHeuristicInstructions : Option<
    "msan-dump-heuristic-instructions",
    "Prints 'unknown' instructions that were handled heuristically.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_MsanInstrumentationWithCallThreshold : Option<
    "msan-instrumentation-with-call-threshold",
    "If the function being instrumented requires more than "
    "this number of checks, use callbacks instead of inline checks.",
    "int"> {
  let Registry = InstrumentationOptsReg;
  let Default = "3500";
  let Hidden = 1;
}

def INST_MsanKernel : Option<
    "msan-kernel",
    "Enable KernelMemorySanitizer instrumentation",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_MsanDisableChecks : Option<
    "msan-disable-checks",
    "Apply no_sanitize to the whole file",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_MsanCheckConstantShadow : Option<
    "msan-check-constant-shadow",
    "Insert checks for constant shadow values",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_MsanWithComdat : Option<
    "msan-with-comdat",
    "Place MSan constructors in comdat sections",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_MsanAndMask : Option<
    "msan-and-mask",
    "Define custom MSan AndMask",
    "uint64_t"> {
  let Registry = InstrumentationOptsReg;
  let Default = "uint64_t{0}";
  let Hidden = 1;
}

def INST_MsanXorMask : Option<
    "msan-xor-mask",
    "Define custom MSan XorMask",
    "uint64_t"> {
  let Registry = InstrumentationOptsReg;
  let Default = "uint64_t{0}";
  let Hidden = 1;
}

def INST_MsanShadowBase : Option<
    "msan-shadow-base",
    "Define custom MSan ShadowBase",
    "uint64_t"> {
  let Registry = InstrumentationOptsReg;
  let Default = "uint64_t{0}";
  let Hidden = 1;
}

def INST_MsanOriginBase : Option<
    "msan-origin-base",
    "Define custom MSan OriginBase",
    "uint64_t"> {
  let Registry = InstrumentationOptsReg;
  let Default = "uint64_t{0}";
  let Hidden = 1;
}

def INST_MsanDisambiguateWarning : Option<
    "msan-disambiguate-warning-threshold",
    "Define threshold for number of checks per "
    "debug location to force origin update.",
    "int"> {
  let Registry = InstrumentationOptsReg;
  let Default = "3";
  let Hidden = 1;
}

// ===== MemProfInstrumentation.cpp =====

def INST_MemprofGuardAgainstVersionMismatch : Option<
    "memprof-guard-against-version-mismatch",
    "Guard against compiler/runtime version mismatch.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_MemprofInstrumentReads : Option<
    "memprof-instrument-reads",
    "instrument read instructions",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_MemprofInstrumentWrites : Option<
    "memprof-instrument-writes",
    "instrument write instructions",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_MemprofInstrumentAtomics : Option<
    "memprof-instrument-atomics",
    "instrument atomic instructions (rmw, cmpxchg)",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_MemprofUseCallbacks : Option<
    "memprof-use-callbacks",
    "Use callbacks instead of inline instrumentation sequences.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_MemprofMemoryAccessCallbackPrefix : Option<
    "memprof-memory-access-callback-prefix",
    "Prefix for memory access callbacks",
    "std::string"> {
  let Registry = InstrumentationOptsReg;
  let Default = "\"__memprof_\"";
  let Hidden = 1;
}

def INST_MemprofMappingScale : Option<
    "memprof-mapping-scale",
    "scale of memprof shadow mapping",
    "int"> {
  let Registry = InstrumentationOptsReg;
  let Default = "3";
  let Hidden = 1;
}

def INST_MemprofMappingGranularity : Option<
    "memprof-mapping-granularity",
    "granularity of memprof shadow mapping",
    "int"> {
  let Registry = InstrumentationOptsReg;
  let Default = "64";
  let Hidden = 1;
}

def INST_MemprofInstrumentStack : Option<
    "memprof-instrument-stack",
    "Instrument scalar stack variables",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_MemprofDebug : Option<
    "memprof-debug", "debug",
    "int"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_MemprofDebugFunc : Option<
    "memprof-debug-func", "Debug func",
    "std::string"> {
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_MemprofDebugMin : Option<
    "memprof-debug-min", "Debug min inst",
    "int"> {
  let Registry = InstrumentationOptsReg;
  let Default = "-1";
  let Hidden = 1;
}

def INST_MemprofDebugMax : Option<
    "memprof-debug-max", "Debug max inst",
    "int"> {
  let Registry = InstrumentationOptsReg;
  let Default = "-1";
  let Hidden = 1;
}

def INST_MemprofHistogram : Option<
    "memprof-histogram",
    "Collect access count histograms",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_MemprofRuntimeDefaultOptions : Option<
    "memprof-runtime-default-options",
    "The default memprof options",
    "std::string"> {
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

// ===== MemProfUse.cpp =====

def INST_MemprofMatchHotColdNew : Option<
    "memprof-match-hot-cold-new",
    "Match allocation profiles onto existing hot/cold operator new calls",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_MemprofPrintMatchInfo : Option<
    "memprof-print-match-info",
    "Print matching stats for each allocation context in this "
    "module's profiles",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_MemprofPrintMatchedAllocStack : Option<
    "memprof-print-matched-alloc-stack",
    "Print full stack context for matched allocations "
    "with -memprof-print-match-info.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_MemprofPrintFunctionGuids : Option<
    "memprof-print-function-guids",
    "Print function GUIDs computed for matching",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_MemprofSalvageStaleProfile : Option<
    "memprof-salvage-stale-profile",
    "Salvage stale MemProf profile",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_MemprofAttachCalleeGuids : Option<
    "memprof-attach-calleeguids",
    "Attach calleeguids as value profile metadata for indirect calls.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_MemprofMatchingColdThreshold : Option<
    "memprof-matching-cold-threshold",
    "Min percent of cold bytes matched to hint allocation cold",
    "unsigned"> {
  let Registry = InstrumentationOptsReg;
  let Default = "100";
  let Hidden = 1;
}

def INST_MemprofAnnotateStaticDataPrefix : Option<
    "memprof-annotate-static-data-prefix",
    "If true, annotate the static data section prefix",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// ===== NumericalStabilitySanitizer.cpp =====

def INST_NsanShadowTypeMapping : Option<
    "nsan-shadow-type-mapping",
    "One shadow type id for each of `float`, `double`, `long double`. "
    "`d`,`l`,`q`,`e` mean double, x86_fp80, fp128 (quad) and "
    "ppc_fp128 (extended double) respectively.",
    "std::string"> {
  let Registry = InstrumentationOptsReg;
  let Default = "\"dqq\"";
  let Hidden = 1;
}

def INST_NsanInstrumentFCmp : Option<
    "nsan-instrument-fcmp",
    "Instrument floating-point comparisons",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_CheckFunctionsFilter : Option<
    "check-functions-filter",
    "Only emit checks for arguments of functions "
    "whose names match the given regular expression",
    "std::string"> {
  let Registry = InstrumentationOptsReg;
  let ValueDesc = "regex";
}

def INST_NsanTruncateFCmpEq : Option<
    "nsan-truncate-fcmp-eq",
    "This flag controls the behaviour of fcmp equality comparisons.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_NsanCheckLoads : Option<
    "nsan-check-loads",
    "Check floating-point load",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_NsanCheckStores : Option<
    "nsan-check-stores",
    "Check floating-point stores",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_NsanCheckRet : Option<
    "nsan-check-ret",
    "Check floating-point return values",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_NsanPropagateNonFTConstStoresAsFT : Option<
    "nsan-propagate-non-ft-const-stores-as-ft",
    "Propagate non floating-point const stores as floating point values. "
    "For debugging purposes only",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

// ===== PGOCtxProfLowering.cpp =====

// (ProfileContextRoot and CtxPGOSkipCallsiteInstrument are list options above)

// ===== PGOInstrumentation.cpp =====

def INST_PGOTestProfileFile : Option<
    "pgo-test-profile-file",
    "Specify the path of profile data file. This is "
    "mainly for test purpose.",
    "std::string"> {
  let Registry = InstrumentationOptsReg;
  let Default = "\"\"";
  let Hidden = 1;
  let ValueDesc = "filename";
}

def INST_PGOTestProfileRemappingFile : Option<
    "pgo-test-profile-remapping-file",
    "Specify the path of profile remapping file. This is mainly for "
    "test purpose.",
    "std::string"> {
  let Registry = InstrumentationOptsReg;
  let Default = "\"\"";
  let Hidden = 1;
  let ValueDesc = "filename";
}

def INST_DisableValueProfiling : Option<
    "disable-vp",
    "Disable Value Profiling",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_ICPMaxAnnotations : Option<
    "icp-max-annotations",
    "Max number of annotations for a single indirect "
    "call callsite",
    "unsigned"> {
  let Registry = InstrumentationOptsReg;
  let Default = "3";
  let Hidden = 1;
}

def INST_MemopMaxAnnotations : Option<
    "memop-max-annotations",
    "Max number of precise value annotations for a single memop "
    "intrinsic",
    "unsigned"> {
  let Registry = InstrumentationOptsReg;
  let Default = "4";
  let Hidden = 1;
}

def INST_DoComdatRenaming : Option<
    "do-comdat-renaming",
    "Append function hash to the name of COMDAT function to avoid "
    "function hash mismatch due to the preinliner",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_PGOWarnMissing : Option<
    "pgo-warn-missing-function",
    "Use this option to turn on/off warnings about missing profile "
    "data for functions.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_NoPGOWarnMismatch : Option<
    "no-pgo-warn-mismatch",
    "Use this option to turn off/on warnings about profile cfg mismatch.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_NoPGOWarnMismatchComdatWeak : Option<
    "no-pgo-warn-mismatch-comdat-weak",
    "The option is used to turn on/off warnings about hash mismatch "
    "for comdat or weak functions.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_PGOInstrSelect : Option<
    "pgo-instr-select",
    "Use this option to turn on/off SELECT instruction instrumentation.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_PGOViewRawCounts : Option<
    "pgo-view-raw-counts",
    "A boolean option to show CFG dag or text "
    "with raw profile counts from "
    "profile data. See also option "
    "-pgo-view-counts. To limit graph "
    "display to only one function, use "
    "filtering option -view-bfi-func-name.",
    "PGOViewCountsType"> {
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_PGOInstrMemOP : Option<
    "pgo-instr-memop",
    "Use this option to turn on/off "
    "memory intrinsic size profiling.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_PGOEmitBranchProb : Option<
    "pgo-emit-branch-prob",
    "When this option is on, the annotated branch probability will be "
    "emitted as optimization remarks.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_PGOInstrumentEntry : Option<
    "pgo-instrument-entry",
    "Force to instrument function entry basicblock.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_PGOInstrumentLoopEntries : Option<
    "pgo-instrument-loop-entries",
    "Force to instrument loop entries.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_PGOFunctionEntryCoverage : Option<
    "pgo-function-entry-coverage",
    "Use this option to enable function entry coverage instrumentation.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_PGOBlockCoverage : Option<
    "pgo-block-coverage",
    "Use this option to enable basic block coverage instrumentation",
    "bool"> {
  let Registry = InstrumentationOptsReg;
}

def INST_PGOViewBlockCoverageGraph : Option<
    "pgo-view-block-coverage-graph",
    "Create a dot file of CFGs with block coverage inference information",
    "bool"> {
  let Registry = InstrumentationOptsReg;
}

def INST_PGOTemporalInstrumentation : Option<
    "pgo-temporal-instrumentation",
    "Use this option to enable temporal instrumentation",
    "bool"> {
  let Registry = InstrumentationOptsReg;
}

def INST_PGOFixEntryCount : Option<
    "pgo-fix-entry-count",
    "Fix function entry count in profile use.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_PGOVerifyHotBFI : Option<
    "pgo-verify-hot-bfi",
    "Print out the non-match BFI count if a hot raw profile count "
    "becomes non-hot, or a cold raw profile count becomes hot.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_PGOVerifyBFI : Option<
    "pgo-verify-bfi",
    "Print out mismatched BFI counts after setting profile metadata.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_PGOVerifyBFIRatio : Option<
    "pgo-verify-bfi-ratio",
    "Set the threshold for pgo-verify-bfi:  only print out "
    "mismatched BFI if the difference percentage is greater than "
    "this value (in percentage).",
    "unsigned"> {
  let Registry = InstrumentationOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def INST_PGOVerifyBFICutoff : Option<
    "pgo-verify-bfi-cutoff",
    "Set the threshold for pgo-verify-bfi: skip the counts whose "
    "profile count value is below.",
    "unsigned"> {
  let Registry = InstrumentationOptsReg;
  let Default = "5";
  let Hidden = 1;
}

def INST_PGOTraceFuncHash : Option<
    "pgo-trace-func-hash",
    "Trace the hash of the function with this name.",
    "std::string"> {
  let Registry = InstrumentationOptsReg;
  let Default = "\"-\"";
  let Hidden = 1;
  let ValueDesc = "function name";
}

def INST_PGOFunctionSizeThreshold : Option<
    "pgo-function-size-threshold",
    "Do not instrument functions smaller than this threshold.",
    "unsigned"> {
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_PGOFunctionCriticalEdgeThreshold : Option<
    "pgo-critical-edge-threshold",
    "Do not instrument functions with the number of critical edges "
    "greater than this threshold.",
    "unsigned"> {
  let Registry = InstrumentationOptsReg;
  let Default = "20000";
  let Hidden = 1;
}

def INST_PGOColdInstrumentEntryThreshold : Option<
    "pgo-cold-instrument-entry-threshold",
    "For cold function instrumentation, skip instrumenting functions "
    "whose entry count is above the given value.",
    "uint64_t"> {
  let Registry = InstrumentationOptsReg;
  let Default = "uint64_t{0}";
  let Hidden = 1;
}

def INST_PGOTreatUnknownAsCold : Option<
    "pgo-treat-unknown-as-cold",
    "For cold function instrumentation, treat count unknown(e.g. "
    "unprofiled) functions as cold.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_PGOInstrumentColdFunctionOnly : Option<
    "pgo-instrument-cold-function-only",
    "Enable cold function only instrumentation.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// ===== PGOMemOPSizeOpt.cpp =====

def INST_PGOMemopCountThreshold : Option<
    "pgo-memop-count-threshold",
    "The minimum count to optimize memory intrinsic calls",
    "unsigned"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1000";
  let Hidden = 1;
}

def INST_DisableMemopOpt : Option<
    "disable-memop-opt",
    "Disable optimize",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_PGOMemopPercentThreshold : Option<
    "pgo-memop-percent-threshold",
    "The percentage threshold for the memory intrinsic calls "
    "optimization",
    "unsigned"> {
  let Registry = InstrumentationOptsReg;
  let Default = "40";
  let Hidden = 1;
}

def INST_PGOMemopMaxVersion : Option<
    "pgo-memop-max-version",
    "The max version for the optimized memory intrinsic calls",
    "unsigned"> {
  let Registry = InstrumentationOptsReg;
  let Default = "3";
  let Hidden = 1;
}

def INST_PGOMemopScaleCount : Option<
    "pgo-memop-scale-count",
    "Scale the memop size counts using the basic block count value",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_PGOMemopOptMemcmpBcmp : Option<
    "pgo-memop-optimize-memcmp-bcmp",
    "Size-specialize memcmp and bcmp calls",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_MemopValueProfMaxOptSize : Option<
    "memop-value-prof-max-opt-size",
    "Optimize the memop size <= this value",
    "unsigned"> {
  let Registry = InstrumentationOptsReg;
  let Default = "128";
  let Hidden = 1;
}

// ===== SanitizerBinaryMetadata.cpp =====

def INST_SanitizerMetadataWeakCallbacks : Option<
    "sanitizer-metadata-weak-callbacks",
    "Declare callbacks extern weak, and only call if non-null.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_SanitizerMetadataNosanitize : Option<
    "sanitizer-metadata-nosanitize-attr",
    "Mark some metadata features uncovered in functions with "
    "disable_sanitizer_instrumentation attribute.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_SanitizerMetadataCovered : Option<
    "sanitizer-metadata-covered",
    "Emit PCs for covered functions.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_SanitizerMetadataAtomics : Option<
    "sanitizer-metadata-atomics",
    "Emit PCs for atomic operations.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_SanitizerMetadataUAR : Option<
    "sanitizer-metadata-uar",
    "Emit PCs for start of functions that are subject to "
    "use-after-return checking.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// ===== SanitizerCoverage.cpp =====

def INST_SanCovLevel : Option<
    "sanitizer-coverage-level",
    "Sanitizer Coverage. 0: none, 1: entry block, 2: all blocks, "
    "3: all blocks and critical edges",
    "int"> {
  let Default = "0";
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_SanCovTracePC : Option<
    "sanitizer-coverage-trace-pc",
    "Experimental pc tracing",
    "bool"> {
  let Default = "false";
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_SanCovTracePCEntryExit : Option<
    "sanitizer-coverage-trace-pc-entry-exit",
    "pc tracing with separate entry/exit callbacks",
    "bool"> {
  let Default = "false";
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_SanCovTracePCGuard : Option<
    "sanitizer-coverage-trace-pc-guard",
    "pc tracing with a guard",
    "bool"> {
  let Default = "false";
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_SanCovCreatePCTable : Option<
    "sanitizer-coverage-pc-table",
    "create a static PC table",
    "bool"> {
  let Default = "false";
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_SanCovInline8bitCounters : Option<
    "sanitizer-coverage-inline-8bit-counters",
    "increments 8-bit counter for every edge",
    "bool"> {
  let Default = "false";
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_SanCovDropCtors : Option<
    "sanitizer-coverage-drop-ctors",
    "do not emit module ctors for global counters",
    "bool"> {
  let Default = "false";
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_SanCovInlineBoolFlag : Option<
    "sanitizer-coverage-inline-bool-flag",
    "sets a boolean flag for every edge",
    "bool"> {
  let Default = "false";
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_SanCovTraceCompares : Option<
    "sanitizer-coverage-trace-compares",
    "Tracing of CMP and similar instructions",
    "bool"> {
  let Default = "false";
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_SanCovTraceDivs : Option<
    "sanitizer-coverage-trace-divs",
    "Tracing of DIV instructions",
    "bool"> {
  let Default = "false";
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_SanCovTraceLoads : Option<
    "sanitizer-coverage-trace-loads",
    "Tracing of load instructions",
    "bool"> {
  let Default = "false";
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_SanCovTraceStores : Option<
    "sanitizer-coverage-trace-stores",
    "Tracing of store instructions",
    "bool"> {
  let Default = "false";
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_SanCovTraceGeps : Option<
    "sanitizer-coverage-trace-geps",
    "Tracing of GEP instructions",
    "bool"> {
  let Default = "false";
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_SanCovPruneBlocks : Option<
    "sanitizer-coverage-prune-blocks",
    "Reduce the number of instrumented blocks",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_SanCovStackDepth : Option<
    "sanitizer-coverage-stack-depth",
    "max stack depth tracing",
    "bool"> {
  let Default = "false";
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_SanCovStackDepthCallbackMin : Option<
    "sanitizer-coverage-stack-depth-callback-min",
    "max stack depth tracing should use callback and only when "
    "stack depth more than specified",
    "int"> {
  let Default = "0";
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_SanCovControlFlow : Option<
    "sanitizer-coverage-control-flow",
    "collect control flow for each function",
    "bool"> {
  let Default = "false";
  let Registry = InstrumentationOptsReg;
  let Hidden = 1;
}

def INST_SanCovGatedCallbacks : Option<
    "sanitizer-coverage-gated-trace-callbacks",
    "Gate the invocation of the tracing callbacks on a global variable. "
    "Currently only supported for trace-pc-guard and trace-cmp.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// ===== ThreadSanitizer.cpp =====

def INST_TsanInstrumentMemoryAccesses : Option<
    "tsan-instrument-memory-accesses",
    "Instrument memory accesses",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_TsanInstrumentFuncEntryExit : Option<
    "tsan-instrument-func-entry-exit",
    "Instrument function entry and exit",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_TsanHandleCxxExceptions : Option<
    "tsan-handle-cxx-exceptions",
    "Handle C++ exceptions (insert cleanup blocks for unwinding)",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_TsanInstrumentAtomics : Option<
    "tsan-instrument-atomics",
    "Instrument atomics",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_TsanInstrumentMemIntrinsics : Option<
    "tsan-instrument-memintrinsics",
    "Instrument memintrinsics (memset/memcpy/memmove)",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_TsanDistinguishVolatile : Option<
    "tsan-distinguish-volatile",
    "Emit special instrumentation for accesses to volatiles",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_TsanInstrumentReadBeforeWrite : Option<
    "tsan-instrument-read-before-write",
    "Do not eliminate read instrumentation for read-before-writes",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_TsanCompoundReadBeforeWrite : Option<
    "tsan-compound-read-before-write",
    "Emit special compound instrumentation for reads-before-writes",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_TsanOmitNonCaptured : Option<
    "tsan-omit-by-pointer-capturing",
    "Omit accesses due to pointer capturing",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// ===== TypeSanitizer.cpp =====

def INST_TysanWritesAlwaysSetType : Option<
    "tysan-writes-always-set-type",
    "Writes always set the type",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def INST_TysanOutlineInstrumentation : Option<
    "tysan-outline-instrumentation",
    "Uses function calls for all TySan instrumentation, reducing "
    "ELF size",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def INST_TysanVerifyOutlinedInstrumentation : Option<
    "tysan-verify-outlined-instrumentation",
    "Check types twice with both inlined instrumentation and "
    "function calls. This verifies that they behave the same.",
    "bool"> {
  let Registry = InstrumentationOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// Apply function declaration is in InstrumentationOptionsOptInfos.h
