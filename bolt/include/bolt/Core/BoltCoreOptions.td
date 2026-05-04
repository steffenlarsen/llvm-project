//===- BoltCoreOptions.td - CLV2 option defs -------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "bolt/Utils/BoltCategories.td"

def BoltCoreOptsReg : Registry<"llvm::bolt::bolt_core_opts"> {
  // Getter bodies are compiled once, in BoltCoreOptionsGetters.cpp.
  let OutOfLineGetters = 1;
  let ContextTypes = [CTX_BinaryContext];
}

// ===== Enum types =====

def JTS_NONE       : EnumValue<"none", "do not optimize functions with jump tables"> {
  let Value = 0;
  let CppName = "JTS_NONE";
}
def JTS_BASIC      : EnumValue<"basic", "optimize functions with jump tables"> {
  let Value = 1;
  let CppName = "JTS_BASIC";
}
def JTS_MOVE       : EnumValue<"move", "move jump tables to a separate section"> {
  let Value = 2;
  let CppName = "JTS_MOVE";
}
def JTS_SPLIT      : EnumValue<"split",
    "split jump tables section into hot and cold based on "
    "function execution frequency"> {
  let Value = 3;
  let CppName = "JTS_SPLIT";
}
def JTS_AGGRESSIVE : EnumValue<"aggressive",
    "aggressively split jump tables section based on usage "
    "of the tables"> {
  let Value = 4;
  let CppName = "JTS_AGGRESSIVE";
}

def BoltCoreJumpTableSupportLevel : EnumType<
    "BoltCoreJumpTableSupportLevel",
    [JTS_NONE, JTS_BASIC, JTS_MOVE, JTS_SPLIT, JTS_AGGRESSIVE]>;

def ICP_NONE       : EnumValue<"none", "do not perform indirect call promotion"> {
  let Value = 0;
  let CppName = "ICP_NONE";
}
def ICP_CALLS      : EnumValue<"calls", "perform ICP on indirect calls"> {
  let Value = 1;
  let CppName = "ICP_CALLS";
}
def ICP_JUMP_TABLES : EnumValue<"jump-tables", "perform ICP on jump tables"> {
  let Value = 2;
  let CppName = "ICP_JUMP_TABLES";
}
def ICP_ALL        : EnumValue<"all", "perform ICP on calls and jump tables"> {
  let Value = 3;
  let CppName = "ICP_ALL";
}

def BoltCoreIndirectCallPromotionType : EnumType<
    "BoltCoreIndirectCallPromotionType",
    [ICP_NONE, ICP_CALLS, ICP_JUMP_TABLES, ICP_ALL]>;

// ===== BinaryContext.cpp options (7) =====

def BOLTCORE_NoHugePages : Option<
    "no-huge-pages",
    "use regular size pages for code alignment",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTCORE_PrintDebugInfo : Option<
    "print-debug-info",
    "print debug info when printing functions",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTCORE_PrintRelocations : Option<
    "print-relocations",
    "print relocations when printing functions/objects",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTCORE_PrintMemData : Option<
    "print-mem-data",
    "print memory data annotations when printing functions",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTCORE_CompDirOverride : Option<
    "comp-dir-override",
    "overrides DW_AT_comp_dir, and provides an alternative base "
    "location, which is used with DW_AT_dwo_name to construct a path "
    "to *.dwo files.",
    "std::string"> {
  let Registry = BoltCoreOptsReg;
  let Hidden = 1;
}

def BOLTCORE_DropDWOPageCache : Option<
    "drop-dwo-page-cache",
    "Treat .dwo files as streaming input: reclaim their page-cache pages as "
    "soon as BOLT is done reading each one. Keeps the file-backed footprint "
    "of large split-DWARF inputs bounded.",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTCORE_CloneConstantIsland : Option<
    "clone-constant-island",
    "clone constant islands",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "true";
  let Hidden = 1;
}

def BOLTCORE_FailOnInvalidPadding : Option<
    "fail-on-invalid-padding",
    "treat invalid code padding as error",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "false";
  let Hidden = 1;
}

// ===== BinaryData.cpp options (1) =====

def BOLTCORE_PrintSymbolAliases : Option<
    "print-aliases",
    "print aliases when printing objects",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "false";
  let Hidden = 1;
}

// ===== BinaryEmitter.cpp options (4 scalar + 3 list) =====

def BOLTCORE_AlignBlocks : Option<
    "align-blocks",
    "align basic blocks",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

def BOLTCORE_BreakFunctionNames : ListOption<
    "break-funcs",
    "list of functions to core dump on (debugging)",
    "std::string"> {
  let Registry = BoltCoreOptsReg;
  let ValueDesc = "func1,func2,func3,...";
  let Hidden = 1;
  let CommaSeparated = 1;
}

def BOLTCORE_FunctionPadSpec : ListOption<
    "pad-funcs",
    "list of functions to pad with amount of bytes",
    "std::string"> {
  let Registry = BoltCoreOptsReg;
  let ValueDesc = "func1:pad1,func2:pad2,func3:pad3,...";
  let Hidden = 1;
  let CommaSeparated = 1;
}

def BOLTCORE_FunctionPadBeforeSpec : ListOption<
    "pad-funcs-before",
    "list of functions to pad with amount of bytes",
    "std::string"> {
  let Registry = BoltCoreOptsReg;
  let ValueDesc = "func1:pad1,func2:pad2,func3:pad3,...";
  let Hidden = 1;
  let CommaSeparated = 1;
}

def BOLTCORE_MarkFuncs : Option<
    "mark-funcs",
    "mark function boundaries with break instruction to make "
    "sure we accidentally don't cross them",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTCORE_PrintJumpTables : Option<
    "print-jump-tables",
    "print jump tables",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTCORE_X86AlignBranchBoundaryHotOnly : Option<
    "x86-align-branch-boundary-hot-only",
    "only apply branch boundary alignment in hot code",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "true";
  let Category = BoltOptCat;
}

// ===== BinaryFunction.cpp options (9 + 1 enum) =====

def BOLTCORE_CheckEncoding : Option<
    "check-encoding",
    "perform verification of LLVM instruction encoding/decoding. "
    "Every instruction in the input is decoded and re-encoded. "
    "If the resulting bytes do not match the input, a warning message "
    "is printed.",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTCORE_DotToolTipCode : Option<
    "dot-tooltip-code",
    "add basic block instructions as tool tips on nodes",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTCORE_JumpTables : Option<
    "jump-tables",
    "jump tables support (default=basic)",
    "BoltCoreJumpTableSupportLevel"> {
  let Registry = BoltCoreOptsReg;
  let Default = "basic";
  let Category = BoltOptCat;
}

def BOLTCORE_NoScan : Option<
    "no-scan",
    "do not scan cold functions for external references (may result in "
    "slower binary)",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTCORE_PreserveBlocksAlignment : Option<
    "preserve-blocks-alignment",
    "try to preserve basic block alignment",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

def BOLTCORE_PrintOutputAddressRange : Option<
    "print-output-address-range",
    "print output address range for each basic block in the function when"
    "BinaryFunction::print is called",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTCORE_PrintDynoStats : Option<
    "dyno-stats",
    "print execution info based on profile",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "false";
  let Category = BoltCat;
}

def BOLTCORE_PrintDynoStatsOnly : Option<
    "print-dyno-stats-only",
    "while printing functions output dyno-stats and skip instructions",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTCORE_TimeBuild : Option<
    "time-build",
    "print time spent constructing binary functions",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTCORE_TrapOnAVX512 : Option<
    "trap-avx512",
    "in relocation mode trap upon entry to any function that uses "
    "AVX-512 instructions",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "false";
  let Hidden = 1;
}

// ===== BinaryFunctionCallGraph.cpp options (1) =====

def BOLTCORE_DumpCGDot : Option<
    "dump-cg",
    "dump callgraph to the given file",
    "std::string"> {
  let Registry = BoltCoreOptsReg;
  let Category = BoltCat;
}

// ===== BinaryFunctionProfile.cpp options (3 + 2 enum) =====

def BOLTCORE_ICP : Option<
    "indirect-call-promotion",
    "indirect call promotion",
    "BoltCoreIndirectCallPromotionType"> {
  let Registry = BoltCoreOptsReg;
  let Default = "none";
  let Category = BoltOptCat;
}

def BOLTCORE_ICPAlias : Option<
    "icp",
    "Alias for --indirect-call-promotion",
    "BoltCoreIndirectCallPromotionType"> {
  let Registry = BoltCoreOptsReg;
  let Default = "none";
  let Hidden = 1;
}

def BOLTCORE_FixFuncCounts : Option<
    "fix-func-counts",
    "adjust function counts based on basic blocks execution count",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTCORE_FixBlockCounts : Option<
    "fix-block-counts",
    "adjust block counts based on outgoing branch counts",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "true";
  let Hidden = 1;
}

def BOLTCORE_InferFallThroughs : Option<
    "infer-fall-throughs",
    "infer execution count for fall-through blocks",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "false";
  let Hidden = 1;
}

// ===== DynoStats.cpp options (2) =====

def BOLTCORE_DynoStatsScale : Option<
    "dyno-stats-scale",
    "scale to be applied while reporting dyno stats",
    "unsigned"> {
  let Registry = BoltCoreOptsReg;
  let Default = "1u";
  let Hidden = 1;
}

def BOLTCORE_PrintDynoOpcodeStat : Option<
    "print-dyno-opcode-stats",
    "print per instruction opcode dyno stats and the function"
    "names:BB offsets of the nth highest execution counts",
    "unsigned"> {
  let Registry = BoltCoreOptsReg;
  let Default = "0u";
  let Hidden = 1;
}

// ===== Exceptions.cpp options (1) =====

def BOLTCORE_PrintExceptions : Option<
    "print-exceptions",
    "print exception handling data",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "false";
  let Hidden = 1;
}

// ===== MCPlusBuilder.cpp options (2) =====

def BOLTCORE_TerminalHLT : Option<
    "terminal-x86-hlt",
    "Assume that execution stops at x86 HLT instruction",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "true";
  let Hidden = 1;
}

def BOLTCORE_TerminalTrap : Option<
    "terminal-trap",
    "Assume that execution stops at trap instruction",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "true";
  let Hidden = 1;
}

// ===== ParallelUtilities.cpp options (3) =====

def BOLTCORE_ThreadCount : Option<
    "thread-count",
    "number of threads",
    "unsigned"> {
  let Registry = BoltCoreOptsReg;
  let Default = "0u";
  let Category = BoltCat;
}

def BOLTCORE_NoThreads : Option<
    "no-threads",
    "disable multithreading",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "false";
  let Category = BoltCat;
}

def BOLTCORE_TaskCount : Option<
    "tasks-per-thread",
    "number of tasks to be created per thread",
    "unsigned"> {
  let Registry = BoltCoreOptsReg;
  let Default = "20u";
  let Category = BoltCat;
}

// ===== AArch64MCPlusBuilder.cpp options (1) =====

def BOLTCORE_NoLSEAtomics : Option<
    "no-lse-atomics",
    "Use LL/SC instead of LSE atomics (AArch64)",
    "bool"> {
  let Registry = BoltCoreOptsReg;
  let Default = "false";
  let Category = BoltInstrCat;
}
