//===- BoltRewriteOptions.td - CLV2 option defs ----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "bolt/Utils/BoltCategories.td"

def BoltRewriteOptsReg : Registry<"llvm::bolt::bolt_rewrite_opts"> {
  // Getter bodies are compiled once, in BoltRewriteOptionsGetters.cpp.
  let OutOfLineGetters = 1;
  let ContextTypes = [CTX_BinaryContext];
}

// ============================================================================
// Enum types
// ============================================================================

// PseudoProbeRewriter.cpp: print-pseudo-probes
def PPP_None : EnumValue<"", "no pseudo probe printing"> {
  let Value = 0;
  let CppName = "BOLTRW_PPP_None";
}
def PPP_Probes_Section_Decode : EnumValue<"decode",
    "decode probes section from binary"> {
  let Value = 1;
  let CppName = "BOLTRW_PPP_Probes_Section_Decode";
}
def PPP_Probes_Address_Conversion : EnumValue<"address_conversion",
    "update address2ProbesMap with output block address"> {
  let Value = 2;
  let CppName = "BOLTRW_PPP_Probes_Address_Conversion";
}
def PPP_Encoded_Probes : EnumValue<"encoded_probes",
    "display the encoded probes in binary section"> {
  let Value = 3;
  let CppName = "BOLTRW_PPP_Encoded_Probes";
}
def PPP_All : EnumValue<"all", "enable all debugging printout"> {
  let Value = 15;
  let CppName = "BOLTRW_PPP_All";
}

def BoltPrintPseudoProbesOptions : EnumType<
    "BoltPrintPseudoProbesOptions",
    [PPP_None, PPP_Probes_Section_Decode, PPP_Probes_Address_Conversion,
     PPP_Encoded_Probes, PPP_All]>;

// RewriteInstance.cpp: runtime-lib-init-hook
def RLIH_ENTRY_POINT : EnumValue<"entry_point",
    "use ELF Header Entry Point"> {
  let Value = 0;
  let CppName = "BOLTRW_RLIH_ENTRY_POINT";
}
def RLIH_INIT : EnumValue<"init", "use ELF DT_INIT entry"> {
  let Value = 1;
  let CppName = "BOLTRW_RLIH_INIT";
}
def RLIH_INIT_ARRAY : EnumValue<"init_array",
    "use ELF .init_array entry"> {
  let Value = 2;
  let CppName = "BOLTRW_RLIH_INIT_ARRAY";
}

def BoltRuntimeLibInitHookTarget : EnumType<
    "BoltRuntimeLibInitHookTarget",
    [RLIH_ENTRY_POINT, RLIH_INIT, RLIH_INIT_ARRAY]>;

// RewriteInstance.cpp: scanners (GadgetKindBitmask)
def GS_PTRAUTH_RETURN_TARGETS : EnumValue<"ptrauth-pac-ret",
    "Unprotected returns (pac-ret)"> {
  let CppName = "GS_PTRAUTH_RETURN_TARGETS";
}
def GS_PTRAUTH_TAIL_CALLS : EnumValue<"ptrauth-tail-calls",
    "Tail calls performed with unprotected link register"> {
  let CppName = "GS_PTRAUTH_TAIL_CALLS";
}
def GS_PTRAUTH_BRANCH_AND_CALL_TARGETS : EnumValue<"ptrauth-forward-cf",
    "Unprotected calls and branches (forward control-flow)"> {
  let CppName = "GS_PTRAUTH_BRANCH_AND_CALL_TARGETS";
}
def GS_PTRAUTH_SIGN_ORACLES : EnumValue<"ptrauth-sign-oracles",
    "Signing of untrusted pointers (signing oracles)"> {
  let CppName = "GS_PTRAUTH_SIGN_ORACLES";
}
def GS_PTRAUTH_AUTH_ORACLES : EnumValue<"ptrauth-auth-oracles",
    "Authentication oracles"> {
  let CppName = "GS_PTRAUTH_AUTH_ORACLES";
}
def GS_PTRAUTH_ALL_MASK : EnumValue<"ptrauth-all",
    "All Pointer Authentication scanners"> {
  let CppName = "GS_PTRAUTH_ALL_MASK";
}
def GS_ALL_MASK : EnumValue<"all", "All implemented scanners"> {
  let CppName = "GS_ALL_MASK";
}

def GadgetKindBitmask : EnumType<"opts::GadgetKindBitmask",
    [GS_PTRAUTH_RETURN_TARGETS, GS_PTRAUTH_TAIL_CALLS,
     GS_PTRAUTH_BRANCH_AND_CALL_TARGETS, GS_PTRAUTH_SIGN_ORACLES,
     GS_PTRAUTH_AUTH_ORACLES, GS_PTRAUTH_ALL_MASK, GS_ALL_MASK]> {
  let External = 1;
}

// ============================================================================
// BinaryPassManager.cpp options (46 + 1 list)
// ============================================================================

def BOLTRW_DynoStatsAll : Option<
    "dyno-stats-all",
    "print dyno stats after each stage",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_EliminateUnreachable : Option<
    "eliminate-unreachable",
    "eliminate unreachable code",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "true";
  let Category = BoltOptCat;
}

def BOLTRW_JTFootprintReduction : Option<
    "jt-footprint-reduction",
    "make jump tables size smaller at the cost of using more instructions "
    "at jump sites",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

def BOLTRW_KeepNops : Option<
    "keep-nops",
    "keep no-op instructions. By default they are removed.",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_NeverPrint : Option<
    "never-print",
    "never print",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintAfterBranchFixup : Option<
    "print-after-branch-fixup",
    "print function after fixing local branches",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintAfterLowering : Option<
    "print-after-lowering",
    "print function after instruction lowering",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintEstimateEdgeCounts : Option<
    "print-estimate-edge-counts",
    "print function after edge counts are set for no-LBR profile",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintFinalized : Option<
    "print-finalized",
    "print function after CFG is finalized",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintFOP : Option<
    "print-fop",
    "print functions after frame optimizer pass",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintICF : Option<
    "print-icf",
    "print functions after ICF optimization",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintICP : Option<
    "print-icp",
    "print functions after indirect call promotion",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintInline : Option<
    "print-inline",
    "print functions after inlining optimization",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintJTFootprintReduction : Option<
    "print-after-jt-footprint-reduction",
    "print function after jt-footprint-reduction pass",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintAArch64Relaxation : Option<
    "print-adr-ldr-relaxation",
    "print functions after ADR/LDR Relaxation pass",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintPAuthCFIAnalyzer : Option<
    "print-pointer-auth-cfi-analyzer",
    "print functions after PointerAuthCFIAnalyzer pass",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintPAuthCFIFixup : Option<
    "print-pointer-auth-cfi-fixup",
    "print functions after PointerAuthCFIFixup pass",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintLongJmp : Option<
    "print-longjmp",
    "print functions after longjmp pass",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintNormalized : Option<
    "print-normalized",
    "print functions after CFG is normalized",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintOptimizeBodyless : Option<
    "print-optimize-bodyless",
    "print functions after bodyless optimization",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintPeepholes : Option<
    "print-peepholes",
    "print functions after peephole optimization",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintPLT : Option<
    "print-plt",
    "print functions after PLT optimization",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintProfileStats : Option<
    "print-profile-stats",
    "print profile quality/bias analysis",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Category = BoltCat;
}

def BOLTRW_PrintRegReAssign : Option<
    "print-regreassign",
    "print functions after regreassign pass",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintReordered : Option<
    "print-reordered",
    "print functions after layout optimization",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintReorderedFunctions : Option<
    "print-reordered-functions",
    "print functions after clustering",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintRetpolineInsertion : Option<
    "print-retpoline-insertion",
    "print functions after retpoline insertion pass",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintSCTC : Option<
    "print-sctc",
    "print functions after conditional tail call simplification",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintSimplifyROLoads : Option<
    "print-simplify-rodata-loads",
    "print functions after simplification of RO data loads",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintSplit : Option<
    "print-split",
    "print functions after code splitting",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintStoke : Option<
    "print-stoke",
    "print functions after stoke analysis",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintFixRelaxations : Option<
    "print-fix-relaxations",
    "print functions after fix relaxations pass",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintFixRISCVCalls : Option<
    "print-fix-riscv-calls",
    "print functions after fix RISCV calls pass",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintVeneerElimination : Option<
    "print-veneer-elimination",
    "print functions after veneer elimination pass",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintUCE : Option<
    "print-uce",
    "print functions after unreachable code elimination",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_RegReAssign : Option<
    "reg-reassign",
    "reassign registers so as to avoid using REX prefixes in hot code",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

def BOLTRW_SimplifyConditionalTailCalls : Option<
    "simplify-conditional-tail-calls",
    "simplify conditional tail calls by removing unnecessary jumps",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "true";
  let Category = BoltOptCat;
}

def BOLTRW_SimplifyRODataLoads : Option<
    "simplify-rodata-loads",
    "simplify loads from read-only sections by replacing the memory "
    "operand with the constant found in the corresponding section",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

def BOLTRW_SpecializeMemcpy1 : ListOption<
    "memcpy1-spec",
    "list of functions with call sites for which to specialize "
    "memcpy() for size 1",
    "std::string"> {
  let Registry = BoltRewriteOptsReg;
  let Category = BoltOptCat;
  let ValueDesc = "func1,func2:cs1:cs2,func3:cs1,...";
}

def BOLTRW_Stoke : Option<
    "stoke",
    "turn on the stoke analysis",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

def BOLTRW_StringOps : Option<
    "inline-memcpy",
    "inline memcpy using size-specific optimized instructions "
    "(X86: 'rep movsb', AArch64: width-optimized register operations)",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

def BOLTRW_StripRepRet : Option<
    "strip-rep-ret",
    "strip 'repz' prefix from 'repz retq' sequence (on by default)",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "true";
  let Category = BoltOptCat;
}

def BOLTRW_VerifyCFG : Option<
    "verify-cfg",
    "verify the CFG after every pass",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_ThreeWayBranch : Option<
    "three-way-branch",
    "reorder three way branches",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_CMOVConversion : Option<
    "cmov-conversion",
    "fold jcc+mov into cmov",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_ShortenInstructions : Option<
    "shorten-instructions",
    "shorten instructions",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "true";
  let Category = BoltOptCat;
}

def BOLTRW_UpdateBranchProtection : Option<
    "update-branch-protection",
    "Rewrites pac-ret DWARF CFI instructions (AArch64-only, on by default)",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "true";
  let Hidden = 1;
}

// ============================================================================
// BoltDiff.cpp options (10)
// ============================================================================

def BOLTRW_IgnoreLTOSuffix : Option<
    "ignore-lto-suffix",
    "ignore lto_priv or const suffixes when matching functions",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "true";
}

def BOLTRW_PrintUnmapped : Option<
    "print-unmapped",
    "print functions of binary 2 that were not matched to any function "
    "in binary 1",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
}

def BOLTRW_PrintProfiledUnmapped : Option<
    "print-profiled-unmapped",
    "print functions that have profile in binary 1 but do not in binary 2",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
}

def BOLTRW_PrintDiffCFG : Option<
    "print-diff-cfg",
    "print the CFG of important functions that changed in binary 2",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
}

def BOLTRW_PrintDiffBBs : Option<
    "print-diff-bbs",
    "print the basic blocks showed in top differences",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
}

def BOLTRW_MatchByHash : Option<
    "match-by-hash",
    "match functions in binary 2 to binary 1 if they have the same hash "
    "of a function in binary 1",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
}

def BOLTRW_IgnoreUnchanged : Option<
    "ignore-unchanged",
    "do not diff functions whose contents have not been changed from "
    "one binary to another",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
}

def BOLTRW_DisplayCount : Option<
    "display-count",
    "number of functions to display when printing the top largest "
    "differences in function activity",
    "unsigned"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "10u";
  let ValueDesc = "uint";
}

def BOLTRW_NormalizeByBin1 : Option<
    "normalize-by-bin1",
    "show execution count of functions in binary 2 as a ratio of the "
    "total samples in binary 1 - make sure both profiles have equal "
    "collection time and sampling rate for this to make sense",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
}

def BOLTRW_SkipNonSimple : Option<
    "skip-non-simple",
    "skip non-simple functions in reporting",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

// ============================================================================
// DWARFRewriter.cpp options (8)
// ============================================================================

def BOLTRW_KeepARanges : Option<
    "keep-aranges",
    "keep or generate .debug_aranges section if .gdb_index is written",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_DebugThreadCount : Option<
    "debug-thread-count",
    "specifies thread count for the multithreading for updating DWO "
    "debug info",
    "unsigned"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "32u";
  let Category = BoltCat;
}

def BOLTRW_DwarfOutputPath : Option<
    "dwarf-output-path",
    "Path to where .dwo files will be written out to.",
    "std::string"> {
  let Registry = BoltRewriteOptsReg;
  let Category = BoltCat;
}

def BOLTRW_CreateDebugNames : Option<
    "create-debug-names-section",
    "Creates .debug_names section, if the input binary doesn't have it "
    "already, for DWARF5 CU/TUs.",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Category = BoltCat;
}

def BOLTRW_DebugSkeletonCu : Option<
    "debug-skeleton-cu",
    "prints out offsets for abbrev and debug_info of Skeleton CUs that "
    "get patched.",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_BatchSize : Option<
    "cu-processing-batch-size",
    "Specifies the size of batches for processing CUs. Higher number "
    "has better performance, but more memory usage. Default value is 1.",
    "unsigned"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "1u";
  let Hidden = 1;
}

def BOLTRW_AlwaysConvertToRanges : Option<
    "always-convert-to-ranges",
    "This option is for testing purposes only. It forces BOLT to "
    "convert low_pc/high_pc to ranges always.",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PreserveOrder : Option<
    "dwarf-preserve-order",
    "Process DWARF CUs in order and produce deterministic output",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "true";
  let Category = BoltCat;
}

// ============================================================================
// LinuxKernelRewriter.cpp options (12)
// ============================================================================

def BOLTRW_AltInstHasPadLen : Option<
    "alt-inst-has-padlen",
    "specify that .altinstructions has padlen field",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_AltInstFeatureSize : Option<
    "alt-inst-feature-size",
    "size of feature field in .altinstructions",
    "uint32_t"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "uint32_t{2}";
  let Hidden = 1;
}

def BOLTRW_DumpAltInstructions : Option<
    "dump-alt-instructions",
    "dump Linux alternative instructions info",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_DumpExceptions : Option<
    "dump-linux-exceptions",
    "dump Linux kernel exception table",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_DumpORC : Option<
    "dump-orc",
    "dump raw ORC unwind information (sorted)",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_DumpParavirtualPatchSites : Option<
    "dump-para-sites",
    "dump Linux kernel paravitual patch sites",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_DumpPCIFixups : Option<
    "dump-pci-fixups",
    "dump Linux kernel PCI fixup table",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_DumpSMPLocks : Option<
    "dump-smp-locks",
    "dump Linux kernel SMP locks",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_DumpStaticCalls : Option<
    "dump-static-calls",
    "dump Linux kernel static calls",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_DumpStaticKeys : Option<
    "dump-static-keys",
    "dump Linux kernel static keys jump table",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_LongJumpLabels : Option<
    "long-jump-labels",
    "always use long jumps/nops for Linux kernel static keys",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintORC : Option<
    "print-orc",
    "print ORC unwind information for instructions",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "true";
  let Hidden = 1;
}

// ============================================================================
// PseudoProbeRewriter.cpp options (1 enum)
// ============================================================================

def BOLTRW_PrintPseudoProbes : Option<
    "print-pseudo-probes",
    "print pseudo probe info",
    "BoltPrintPseudoProbesOptions"> {
  let Registry = BoltRewriteOptsReg;
  let Hidden = 1;
}

// ============================================================================
// RewriteInstance.cpp options (25 scalar + 1 enum + 5 list)
// ============================================================================

def BOLTRW_AllowStripped : Option<
    "allow-stripped",
    "allow processing of stripped binaries",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_ForceToDataRelocations : Option<
    "force-data-relocations",
    "force relocations to data sections to always be processed",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_MergeTextSections : Option<
    "merge-text-sections",
    "emit new hot and cold code under a single .text section header "
    "instead of separate .text/.text.cold sections (relocation mode)",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Category = BoltCat;
}

def BOLTRW_BoltID : Option<
    "bolt-id",
    "add any string to tag this execution in the output binary via "
    "bolt info section",
    "std::string"> {
  let Registry = BoltRewriteOptsReg;
  let Category = BoltCat;
}

def BOLTRW_DumpDotAll : Option<
    "dump-dot-all",
    "dump function CFGs to graphviz format after each stage;"
    "enable '-print-loops' for color-coded blocks",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_DumpDotFunc : ListOption<
    "dump-dot-func",
    "dump function CFGs to graphviz format for specified functions only;"
    "takes function name patterns (regex supported)",
    "std::string"> {
  let Registry = BoltRewriteOptsReg;
  let Hidden = 1;
  let CommaSeparated = 1;
}

def BOLTRW_ForceFunctionNames : ListOption<
    "funcs",
    "limit optimizations to functions from the list",
    "std::string"> {
  let Registry = BoltRewriteOptsReg;
  let Hidden = 1;
  let CommaSeparated = 1;
}

def BOLTRW_FunctionNamesFile : Option<
    "funcs-file",
    "file with list of functions to optimize",
    "std::string"> {
  let Registry = BoltRewriteOptsReg;
  let Hidden = 1;
}

def BOLTRW_ForceFunctionNamesNR : ListOption<
    "funcs-no-regex",
    "limit optimizations to functions from the list (non-regex)",
    "std::string"> {
  let Registry = BoltRewriteOptsReg;
  let Hidden = 1;
  let CommaSeparated = 1;
}

def BOLTRW_FunctionNamesFileNR : Option<
    "funcs-file-no-regex",
    "file with list of functions to optimize (non-regex)",
    "std::string"> {
  let Registry = BoltRewriteOptsReg;
  let Hidden = 1;
}

def BOLTRW_KeepTmp : Option<
    "keep-tmp",
    "preserve intermediate .o file",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_LiteThresholdPct : Option<
    "lite-threshold-pct",
    "threshold (in percent) for selecting functions to process in lite "
    "mode. Higher threshold means fewer functions to process. E.g "
    "threshold of 90 means only top 10 percent of functions with "
    "profile will be processed.",
    "unsigned"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "0u";
  let Hidden = 1;
}

def BOLTRW_LiteThresholdCount : Option<
    "lite-threshold-count",
    "similar to '-lite-threshold-pct' but specify threshold using "
    "absolute function call count. I.e. limit processing to functions "
    "executed at least the specified number of times.",
    "unsigned"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "0u";
  let Hidden = 1;
}

def BOLTRW_MaxFunctions : Option<
    "max-funcs",
    "maximum number of functions to process",
    "unsigned"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "0u";
  let Hidden = 1;
}

def BOLTRW_MaxDataRelocations : Option<
    "max-data-relocations",
    "maximum number of data relocations to process",
    "unsigned"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "0u";
  let Hidden = 1;
}

def BOLTRW_PrintAll : Option<
    "print-all",
    "print functions after each stage",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintProfile : Option<
    "print-profile",
    "print functions after attaching profile",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintCFG : Option<
    "print-cfg",
    "print functions after CFG construction",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintDisasm : Option<
    "print-disasm",
    "print function after disassembly",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintGlobals : Option<
    "print-globals",
    "print global symbols after disassembly",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_PrintLoopInfo : Option<
    "print-loops",
    "print loop related information",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_SkipFunctionNames : ListOption<
    "skip-funcs",
    "list of functions to skip",
    "std::string"> {
  let Registry = BoltRewriteOptsReg;
  let Hidden = 1;
  let CommaSeparated = 1;
}

def BOLTRW_SkipFunctionNamesFile : Option<
    "skip-funcs-file",
    "file with list of functions to skip",
    "std::string"> {
  let Registry = BoltRewriteOptsReg;
  let Hidden = 1;
}

def BOLTRW_TrapOldCode : Option<
    "trap-old-code",
    "insert traps in old function bodies (relocation mode)",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTRW_DWPPathName : Option<
    "dwp",
    "Path and name to DWP file.",
    "std::string"> {
  let Registry = BoltRewriteOptsReg;
  let Hidden = 1;
}

def BOLTRW_UseGnuStack : Option<
    "use-gnu-stack",
    "use GNU_STACK program header for new segment (workaround for "
    "issues with strip/objcopy)",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Category = BoltCat;
}

def BOLTRW_CustomAllocationVMA : Option<
    "custom-allocation-vma",
    "use a custom address at which new code will be put, bypassing "
    "BOLT's logic to detect where to put code",
    "uint64_t"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "uint64_t{0}";
  let Hidden = 1;
}

def BOLTRW_SequentialDisassembly : Option<
    "sequential-disassembly",
    "performs disassembly sequentially",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

def BOLTRW_WriteBoltInfoSection : Option<
    "bolt-info",
    "write bolt info section in the output binary",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "true";
  let Hidden = 1;
}

def BOLTRW_RuntimeLibInitHook : Option<
    "runtime-lib-init-hook",
    "Primary target for hooking runtime library initialization, "
    "used in fallback order of availabiliy in input binary "
    "(entry_point -> init -> init_array) (default: entry_point)",
    "BoltRuntimeLibInitHookTarget"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "entry_point";
  let Hidden = 1;
}

// ============================================================================
// Additional RewriteInstance.cpp options
// ============================================================================

def BOLTRW_AccurateDebugRanges : Option<
    "accurate-debug-ranges",
    "gather debug scope boundary information for accurate ranges",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "true";
  let Hidden = 1;
}

def BOLTRW_Relocs : Option<
    "relocs",
    "use relocations in the binary (default=autodetect)",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Category = BoltCat;
}

def BOLTRW_GadgetScannersToRun : ListOption<
    "scanners",
    "Which gadget scanners to run",
    "opts::GadgetKindBitmask"> {
  let Registry = BoltRewriteOptsReg;
  let CommaSeparated = 1;
  let Category = BinaryAnalysisCat;
}

// ============================================================================
// SDTRewriter.cpp options (1)
// ============================================================================

def BOLTRW_PrintSDTMarkers : Option<
    "print-sdt",
    "print all SDT markers",
    "bool"> {
  let Registry = BoltRewriteOptsReg;
  let Default = "false";
  let Hidden = 1;
}
