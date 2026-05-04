//===- CommandFlagsOptions.td - CLV2 option defs for CodeGen flags -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def CGOptsReg : Registry<"llvm::cgflags_opts"> {
  // Getter bodies are compiled once, in CommandFlagsOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

// =========================================================================
// External enum types
// =========================================================================

// --- Reloc::Model (CodeGen.h) ---

def RelocModelStatic : EnumValue<"static",
    "Non-relocatable code"> {
  let CppName = "Static";
}

def RelocModelPIC : EnumValue<"pic",
    "Fully relocatable, position independent code"> {
  let CppName = "PIC_";
}

def RelocModelDynamicNoPIC : EnumValue<"dynamic-no-pic",
    "Relocatable external references, non-relocatable code"> {
  let CppName = "DynamicNoPIC";
}

def RelocModelROPI : EnumValue<"ropi",
    "Code and read-only data relocatable, accessed PC-relative"> {
  let CppName = "ROPI";
}

def RelocModelRWPI : EnumValue<"rwpi",
    "Read-write data relocatable, accessed relative to static base"> {
  let CppName = "RWPI";
}

def RelocModelROPI_RWPI : EnumValue<"ropi-rwpi",
    "Combination of ropi and rwpi"> {
  let CppName = "ROPI_RWPI";
}

def RelocModelEnum : EnumType<"Reloc::Model",
    [RelocModelStatic, RelocModelPIC, RelocModelDynamicNoPIC,
     RelocModelROPI, RelocModelRWPI, RelocModelROPI_RWPI]> {
  let External = 1;
}

// --- ThreadModel::Model (CodeGen.h) ---

def ThreadModelPOSIX : EnumValue<"posix",
    "POSIX thread model"> {
  let CppName = "POSIX";
}

def ThreadModelSingle : EnumValue<"single",
    "Single thread model"> {
  let CppName = "Single";
}

def ThreadModelEnum : EnumType<"ThreadModel::Model",
    [ThreadModelPOSIX, ThreadModelSingle]> {
  let External = 1;
}

// --- CodeModel::Model (CodeGen.h) ---

def CodeModelTiny : EnumValue<"tiny",
    "Tiny code model"> {
  let CppName = "Tiny";
}

def CodeModelSmall : EnumValue<"small",
    "Small code model"> {
  let CppName = "Small";
}

def CodeModelKernel : EnumValue<"kernel",
    "Kernel code model"> {
  let CppName = "Kernel";
}

def CodeModelMedium : EnumValue<"medium",
    "Medium code model"> {
  let CppName = "Medium";
}

def CodeModelLarge : EnumValue<"large",
    "Large code model"> {
  let CppName = "Large";
}

def CodeModelEnum : EnumType<"CodeModel::Model",
    [CodeModelTiny, CodeModelSmall, CodeModelKernel,
     CodeModelMedium, CodeModelLarge]> {
  let External = 1;
}

// --- ExceptionHandling (CodeGen.h) ---
// Original C++ enum: { None=0, DwarfCFI=1, SjLj=2, ARM=3, WinEH=4, Wasm=5,
//                       AIX=6, ZOS=7 }

def ExceptionHandlingNone : EnumValue<"default",
    "default exception handling model"> {
  let CppName = "None";
  let Value = 0;
}

def ExceptionHandlingDwarfCFI : EnumValue<"dwarf",
    "DWARF-like CFI based exception handling"> {
  let CppName = "DwarfCFI";
  let Value = 1;
}

def ExceptionHandlingSjLj : EnumValue<"sjlj",
    "SjLj exception handling"> {
  let CppName = "SjLj";
  let Value = 2;
}

def ExceptionHandlingARM : EnumValue<"arm",
    "ARM EHABI exceptions"> {
  let CppName = "ARM";
  let Value = 3;
}

def ExceptionHandlingWinEH : EnumValue<"wineh",
    "Windows exception model"> {
  let CppName = "WinEH";
  let Value = 4;
}

def ExceptionHandlingWasm : EnumValue<"wasm",
    "WebAssembly exception handling"> {
  let CppName = "Wasm";
  let Value = 5;
}

def ExceptionHandlingAIX : EnumValue<"__aix",
    "AIX exception handling"> {
  let CppName = "AIX";
  let Value = 6;
}

def ExceptionHandlingZOS : EnumValue<"__zos",
    "z/OS MVS exception handling"> {
  let CppName = "ZOS";
  let Value = 7;
}

def ExceptionHandlingEnum : EnumType<"ExceptionHandling",
    [ExceptionHandlingNone, ExceptionHandlingDwarfCFI,
     ExceptionHandlingSjLj, ExceptionHandlingARM,
     ExceptionHandlingWinEH, ExceptionHandlingWasm,
     ExceptionHandlingAIX, ExceptionHandlingZOS]> {
  let External = 1;
}

// --- CodeGenFileType ---

def CodeGenFileTypeAssembly : EnumValue<"asm",
    "Emit an assembly ('.s') file"> {
  let CppName = "AssemblyFile";
}

def CodeGenFileTypeObject : EnumValue<"obj",
    "Emit a native object ('.o') file"> {
  let CppName = "ObjectFile";
}

def CodeGenFileTypeNull : EnumValue<"null",
    "Emit nothing, for performance testing"> {
  let CppName = "Null";
}

def CodeGenFileTypeEnum : EnumType<"CodeGenFileType",
    [CodeGenFileTypeAssembly, CodeGenFileTypeObject,
     CodeGenFileTypeNull]> {
  let External = 1;
}

// --- FramePointerKind (CodeGen.h) ---
// Original C++ enum: { None=0, NonLeaf=1, All=2, Reserved=3, NonLeafNoReserve=4 }

def FramePointerKindNone : EnumValue<"none",
    "Enable frame pointer elimination"> {
  let CppName = "None";
  let Value = 0;
}

def FramePointerKindNonLeaf : EnumValue<"non-leaf",
    "Disable frame pointer elimination for non-leaf frame but reserve "
    "the register in leaf functions"> {
  let CppName = "NonLeaf";
  let Value = 1;
}

def FramePointerKindAll : EnumValue<"all",
    "Disable frame pointer elimination"> {
  let CppName = "All";
  let Value = 2;
}

def FramePointerKindReserved : EnumValue<"reserved",
    "Enable frame pointer elimination, but reserve the frame "
    "pointer register"> {
  let CppName = "Reserved";
  let Value = 3;
}

def FramePointerKindNonLeafNoReserve : EnumValue<"non-leaf-no-reserve",
    "Disable frame pointer elimination for non-leaf frame"> {
  let CppName = "NonLeafNoReserve";
  let Value = 4;
}

def FramePointerKindEnum : EnumType<"FramePointerKind",
    [FramePointerKindNone, FramePointerKindNonLeaf,
     FramePointerKindAll, FramePointerKindReserved,
     FramePointerKindNonLeafNoReserve]> {
  let External = 1;
}

// --- DenormalMode::DenormalModeKind (FloatingPointMode.h) ---
// Original C++ enum: { Invalid=-1, IEEE=0, PreserveSign=1,
//                       PositiveZero=2, Dynamic=3 }

def DenormalModeInvalid : EnumValue<"__invalid",
    "Invalid denormal mode"> {
  let CppName = "Invalid";
  let Value = -1;
}

def DenormalModeIEEE : EnumValue<"ieee",
    "IEEE 754 denormal numbers"> {
  let CppName = "IEEE";
  let Value = 0;
}

def DenormalModePreserveSign : EnumValue<"preserve-sign",
    "the sign of a  flushed-to-zero number is preserved "
    "in the sign of 0"> {
  let CppName = "PreserveSign";
  let Value = 1;
}

def DenormalModePositiveZero : EnumValue<"positive-zero",
    "denormals are flushed to positive zero"> {
  let CppName = "PositiveZero";
  let Value = 2;
}

def DenormalModeDynamic : EnumValue<"dynamic",
    "denormals have unknown treatment"> {
  let CppName = "Dynamic";
  let Value = 3;
}

def DenormalModeKindEnum : EnumType<"DenormalMode::DenormalModeKind",
    [DenormalModeInvalid, DenormalModeIEEE, DenormalModePreserveSign,
     DenormalModePositiveZero, DenormalModeDynamic]> {
  let External = 1;
}

// --- FloatABI::ABIType ---

def FloatABIDefault : EnumValue<"default",
    "Target default float ABI type"> {
  let CppName = "Default";
}

def FloatABISoft : EnumValue<"soft",
    "Soft float ABI (implied by -soft-float)"> {
  let CppName = "Soft";
}

def FloatABIHard : EnumValue<"hard",
    "Hard float ABI (uses FP registers)"> {
  let CppName = "Hard";
}

def FloatABIEnum : EnumType<"FloatABI::ABIType",
    [FloatABIDefault, FloatABISoft, FloatABIHard]> {
  let External = 1;
}

// --- FPOpFusion::FPOpFusionMode ---

def FPOpFusionFast : EnumValue<"fast",
    "Fuse FP ops whenever profitable"> {
  let CppName = "Fast";
}

def FPOpFusionStandard : EnumValue<"on",
    "Only fuse 'blessed' FP ops."> {
  let CppName = "Standard";
}

def FPOpFusionStrict : EnumValue<"off",
    "Only fuse FP ops when the result won't be affected."> {
  let CppName = "Strict";
}

def FPOpFusionModeEnum : EnumType<"FPOpFusion::FPOpFusionMode",
    [FPOpFusionFast, FPOpFusionStandard, FPOpFusionStrict]> {
  let External = 1;
}

// --- SwiftAsyncFramePointerMode ---

def SwiftAsyncFPDeploymentBased : EnumValue<"auto",
    "Determine based on deployment target"> {
  let CppName = "DeploymentBased";
}

def SwiftAsyncFPAlways : EnumValue<"always",
    "Always set the bit"> {
  let CppName = "Always";
}

def SwiftAsyncFPNever : EnumValue<"never",
    "Never set the bit"> {
  let CppName = "Never";
}

def SwiftAsyncFramePointerModeEnum : EnumType<"SwiftAsyncFramePointerMode",
    [SwiftAsyncFPDeploymentBased, SwiftAsyncFPAlways,
     SwiftAsyncFPNever]> {
  let External = 1;
}

// --- EABI (CodeGen.h) ---
// Original C++ enum: { Unknown=0, Default=1, EABI4=2, EABI5=3, GNU=4 }

def EABIUnknown : EnumValue<"__unknown",
    "Unknown EABI version"> {
  let CppName = "Unknown";
  let Value = 0;
}

def EABIDefault : EnumValue<"default",
    "Triple default EABI version"> {
  let CppName = "Default";
  let Value = 1;
}

def EABI4 : EnumValue<"4",
    "EABI version 4"> {
  let CppName = "EABI4";
  let Value = 2;
}

def EABI5 : EnumValue<"5",
    "EABI version 5"> {
  let CppName = "EABI5";
  let Value = 3;
}

def EABIGNU : EnumValue<"gnu",
    "EABI GNU"> {
  let CppName = "GNU";
  let Value = 4;
}

def EABIEnum : EnumType<"EABI",
    [EABIUnknown, EABIDefault, EABI4, EABI5, EABIGNU]> {
  let External = 1;
}

// --- DebuggerKind (TargetOptions.h) ---
// Original C++ enum: { Default=0, GDB=1, LLDB=2, SCE=3, DBX=4 }

def DebuggerKindDefault : EnumValue<"__default",
    "No specific tuning"> {
  let CppName = "Default";
  let Value = 0;
}

def DebuggerKindGDB : EnumValue<"gdb",
    "gdb"> {
  let CppName = "GDB";
  let Value = 1;
}

def DebuggerKindLLDB : EnumValue<"lldb",
    "lldb"> {
  let CppName = "LLDB";
  let Value = 2;
}

def DebuggerKindSCE : EnumValue<"sce",
    "SCE targets (e.g. PS4)"> {
  let CppName = "SCE";
  let Value = 3;
}

def DebuggerKindDBX : EnumValue<"dbx",
    "dbx"> {
  let CppName = "DBX";
  let Value = 4;
}

def DebuggerKindEnum : EnumType<"DebuggerKind",
    [DebuggerKindDefault, DebuggerKindGDB, DebuggerKindLLDB,
     DebuggerKindSCE, DebuggerKindDBX]> {
  let External = 1;
}

// --- VectorLibrary (forward-declared) ---

def VectorLibraryNone : EnumValue<"none",
    "No vector functions library"> {
  let CppName = "NoLibrary";
}

def VectorLibraryAccelerate : EnumValue<"Accelerate",
    "Accelerate framework"> {
  let CppName = "Accelerate";
}

def VectorLibraryDarwinLibSystemM : EnumValue<"Darwin_libsystem_m",
    "Darwin libsystem_m"> {
  let CppName = "DarwinLibSystemM";
}

def VectorLibraryLIBMVEC : EnumValue<"LIBMVEC",
    "GLIBC Vector Math library"> {
  let CppName = "LIBMVEC";
}

def VectorLibraryMASSV : EnumValue<"MASSV",
    "IBM MASS vector library"> {
  let CppName = "MASSV";
}

def VectorLibrarySVML : EnumValue<"SVML",
    "Intel SVML library"> {
  let CppName = "SVML";
}

def VectorLibrarySLEEFGNUABI : EnumValue<"sleefgnuabi",
    "SIMD Library for Evaluating Elementary Functions"> {
  let CppName = "SLEEFGNUABI";
}

def VectorLibraryArmPL : EnumValue<"ArmPL",
    "Arm Performance Libraries"> {
  let CppName = "ArmPL";
}

def VectorLibraryAMDLIBM : EnumValue<"AMDLIBM",
    "AMD vector math library"> {
  let CppName = "AMDLIBM";
}

def VectorLibraryEnum : EnumType<"VectorLibrary",
    [VectorLibraryNone, VectorLibraryAccelerate,
     VectorLibraryDarwinLibSystemM, VectorLibraryLIBMVEC,
     VectorLibraryMASSV, VectorLibrarySVML,
     VectorLibrarySLEEFGNUABI, VectorLibraryArmPL,
     VectorLibraryAMDLIBM]> {
  let External = 1;
}

// --- codegen::SaveStatsMode (CommandFlags.h) ---
// Original C++ enum: { None=0, Cwd=1, Obj=2 }

def SaveStatsModeNone : EnumValue<"__none",
    "Do not save stats"> {
  let CppName = "None";
  let Value = 0;
}

def SaveStatsModeCwd : EnumValue<"cwd",
    "Save to the current working directory"> {
  let CppName = "Cwd";
  let Value = 1;
}

def SaveStatsModeObj : EnumValue<"obj",
    "Save to the output file directory"> {
  let CppName = "Obj";
  let Value = 2;
}

def SaveStatsModeEnum : EnumType<"codegen::SaveStatsMode",
    [SaveStatsModeNone, SaveStatsModeCwd, SaveStatsModeObj]> {
  let External = 1;
}

// =========================================================================
// Options
// =========================================================================

// --- String and numeric options ---

def CG_MArch : Option<
    "march", "Architecture to generate code for (see --version)",
    "std::string"> {
  let Registry = CGOptsReg;
}

def CG_MCPU : Option<
    "mcpu", "Target a specific cpu type (-mcpu=help for details)",
    "std::string"> {
  let Registry = CGOptsReg;
  let Default = "\"\"";
  let ValueDesc = "cpu-name";
}

def CG_MTune : Option<
    "mtune",
    "Tune for a specific CPU microarchitecture (-mtune=help for details)",
    "std::string"> {
  let Registry = CGOptsReg;
  let Default = "\"\"";
  let ValueDesc = "tune-cpu-name";
}

def CG_MAttrs : ListOption<
    "mattr", "Target specific attributes (-mattr=help for details)",
    "std::string"> {
  let Registry = CGOptsReg;
  let ValueDesc = "a1,+a2,-a3,...";
  let CommaSeparated = 1;
}

// --- Enum options ---

def CG_RelocModel : Option<
    "relocation-model", "Choose relocation model",
    "Reloc::Model"> {
  let Registry = CGOptsReg;
}

def CG_ThreadModel : Option<
    "thread-model", "Choose threading model",
    "ThreadModel::Model"> {
  let Registry = CGOptsReg;
  let Default = "posix";
}

def CG_CodeModel : Option<
    "code-model", "Choose code model",
    "CodeModel::Model"> {
  let Registry = CGOptsReg;
}

def CG_ExceptionModel : Option<
    "exception-model", "exception model",
    "ExceptionHandling"> {
  let Registry = CGOptsReg;
  let Default = "default";
}

def CG_FileType : Option<
    "filetype",
    "Choose a file type (not all types are supported by all targets):",
    "CodeGenFileType"> {
  let Registry = CGOptsReg;
  let Default = "asm";
}

def CG_FramePointer : Option<
    "frame-pointer", "Specify frame pointer elimination optimization",
    "FramePointerKind"> {
  let Registry = CGOptsReg;
  let Default = "none";
}

def CG_DenormalFPMath : Option<
    "denormal-fp-math",
    "Select which denormal numbers the code is permitted to require",
    "DenormalMode::DenormalModeKind"> {
  let Registry = CGOptsReg;
  let Default = "ieee";
}

def CG_DenormalFP32Math : Option<
    "denormal-fp-math-f32",
    "Select which denormal numbers the code is permitted to require for float",
    "DenormalMode::DenormalModeKind"> {
  let Registry = CGOptsReg;
  let Default = "__invalid";
}

def CG_FloatABIForCalls : Option<
    "float-abi", "Choose float ABI type",
    "FloatABI::ABIType"> {
  let Registry = CGOptsReg;
  let Default = "default";
}

def CG_FuseFPOps : Option<
    "fp-contract", "Enable aggressive formation of fused FP ops",
    "FPOpFusion::FPOpFusionMode"> {
  let Registry = CGOptsReg;
  let Default = "on";
}

def CG_SwiftAsyncFramePointer : Option<
    "swift-async-fp",
    "Determine when the Swift async frame pointer should be set",
    "SwiftAsyncFramePointerMode"> {
  let Registry = CGOptsReg;
  let Default = "always";
}

def CG_EABIVersion : Option<
    "meabi", "Set EABI type (default depends on triple):",
    "EABI"> {
  let Registry = CGOptsReg;
  let Default = "default";
}

def CG_DebuggerTuning : Option<
    "debugger-tune", "Tune debug info for a particular debugger",
    "DebuggerKind"> {
  let Registry = CGOptsReg;
  let Default = "__default";
}

def CG_VectorLibrary : Option<
    "vector-library", "Vector functions library",
    "VectorLibrary"> {
  let Registry = CGOptsReg;
  let Default = "none";
  let Hidden = 1;
}

def CG_SaveStats : Option<
    "save-stats",
    "Save LLVM statistics to a file in the current directory "
    "(`-save-stats`/`-save-stats=cwd`) or the directory of the output "
    "file (`-save-stats=obj`). (default: cwd)",
    "codegen::SaveStatsMode"> {
  let Registry = CGOptsReg;
  let Default = "__none";
  let ValueOptional = 1;
}

// --- Numeric options ---

def CG_LargeDataThreshold : Option<
    "large-data-threshold",
    "Choose large data threshold for x86_64 medium code model",
    "uint64_t"> {
  let Registry = CGOptsReg;
  let Default = "uint64_t{0}";
}

def CG_TLSSize : Option<
    "tls-size", "Bit size of immediate TLS offsets",
    "unsigned"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_AlignLoops : Option<
    "align-loops", "Default alignment for loops",
    "unsigned"> {
  let Registry = CGOptsReg;
}

// --- Boolean options ---

def CG_EnableNoTrappingFPMath : Option<
    "enable-no-trapping-fp-math",
    "Enable setting the FP exceptions build "
    "attribute not to use exceptions",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_EnableAIXExtendedAltivecABI : Option<
    "vec-extabi", "Enable the AIX Extended Altivec ABI.",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_EnableHonorSignDependentRoundingFPMath : Option<
    "enable-sign-dependent-rounding-fp-math",
    "Force codegen to assume rounding mode can change dynamically",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def CG_DontPlaceZerosInBSS : Option<
    "nozero-initialized-in-bss",
    "Don't place zero-initialized symbols into bss section",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_EnableGuaranteedTailCallOpt : Option<
    "tailcallopt",
    "Turn fastcc calls into tail calls by (potentially) changing ABI.",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_DisableTailCalls : Option<
    "disable-tail-calls", "Never emit tail calls",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_StackSymbolOrdering : Option<
    "stack-symbol-ordering", "Order local stack symbols.",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "1";
}

def CG_StackRealign : Option<
    "stackrealign",
    "Force align the stack to the minimum alignment",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_TrapFuncName : Option<
    "trap-func",
    "Emit a call to trap function rather than a trap instruction",
    "std::string"> {
  let Registry = CGOptsReg;
  let Default = "\"\"";
  let Hidden = 1;
}

def CG_UseCtors : Option<
    "use-ctors", "Use .ctors instead of .init_array.",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_DisableIntegratedAS : Option<
    "no-integrated-as", "Disable integrated assembler",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_DataSections : Option<
    "data-sections", "Emit data into separate sections",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_FunctionSections : Option<
    "function-sections", "Emit functions into separate sections",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_IgnoreXCOFFVisibility : Option<
    "ignore-xcoff-visibility",
    "Not emit the visibility attribute for asm in AIX OS or give "
    "all symbols 'unspecified' visibility in XCOFF object file",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_XCOFFTracebackTable : Option<
    "xcoff-traceback-table", "Emit the XCOFF traceback table",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "1";
}

def CG_EnableBBAddrMap : Option<
    "basic-block-address-map",
    "Emit the basic block address map section",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_BBSections : Option<
    "basic-block-sections",
    "Emit basic blocks into separate sections",
    "std::string"> {
  let Registry = CGOptsReg;
  let Default = "\"none\"";
  let ValueDesc = "all | <function list (file)> | labels | none";
}

def CG_EmulatedTLS : Option<
    "emulated-tls", "Use emulated TLS model",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_EnableTLSDESC : Option<
    "enable-tlsdesc", "Enable the use of TLS Descriptors",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_UniqueSectionNames : Option<
    "unique-section-names", "Give unique names to every section",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "1";
}

def CG_UniqueBasicBlockSectionNames : Option<
    "unique-basic-block-section-names",
    "Give unique names to every basic block section",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_SeparateNamedSections : Option<
    "separate-named-sections",
    "Use separate unique sections for named sections",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_EnableStackSizeSection : Option<
    "stack-size-section",
    "Emit a section containing stack size metadata",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_EnableAddrsig : Option<
    "addrsig", "Emit an address-significance table",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_EnableCallGraphSection : Option<
    "call-graph-section", "Emit a call graph section",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_EmitCallSiteInfo : Option<
    "emit-call-site-info",
    "Emit call site debug information, if debug information is enabled.",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_EnableMachineFunctionSplitter : Option<
    "split-machine-functions",
    "Split out cold basic blocks from machine functions based on "
    "profile information",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_EnableStaticDataPartitioning : Option<
    "partition-static-data-sections",
    "Partition data sections using profile information.",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_EnableDebugEntryValues : Option<
    "debug-entry-values",
    "Enable debug info for the debug entry values.",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_ForceDwarfFrameSection : Option<
    "force-dwarf-frame-section",
    "Always emit a debug frame section.",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_XRayFunctionIndex : Option<
    "xray-function-index", "Emit xray_fn_idx section",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "1";
}

def CG_DebugStrictDwarf : Option<
    "strict-dwarf", "use strict dwarf",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_JMCInstrument : Option<
    "enable-jmc-instrument",
    "Instrument functions with a call to __CheckForDebuggerJustMyCode",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

def CG_XCOFFReadOnlyPointers : Option<
    "mxcoff-roptr",
    "When set to true, const objects with relocatable address "
    "values are put into the RO data section.",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
}

// --- Options from MachineScheduler.cpp ---

def CG_VerifyMisched : Option<
    "verify-misched",
    "Verify machine instrs before and after machine scheduling",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def CG_MischedPrintDags : Option<
    "misched-print-dags",
    "Print schedule DAGs",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// --- Options from MachinePipeliner.cpp ---

def CG_PipelinerEnableCopytophi : Option<
    "pipeliner-enable-copytophi",
    "Enable CopyToPhi DAG Mutation",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def CG_PipelinerForceIssueWidth : Option<
    "pipeliner-force-issue-width",
    "Force pipeliner to use specified issue width.",
    "int"> {
  let Registry = CGOptsReg;
  let Default = "-1";
  let Hidden = 1;
}

// --- Options from MIRFSDiscriminator.cpp ---

def CG_ImprovedFsDiscriminator : Option<
    "improved-fs-discriminator",
    "New FS discriminators encoding (incompatible with the original "
    "encoding)",
    "bool"> {
  let Registry = CGOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// --- Options from BasicBlockSections.cpp ---

def CG_BbsectionsColdTextPrefix : Option<
    "bbsections-cold-text-prefix",
    "The text prefix to use for cold basic block clusters",
    "std::string"> {
  let Registry = CGOptsReg;
  let Default = "\".text.split.\"";
  let Hidden = 1;
}
