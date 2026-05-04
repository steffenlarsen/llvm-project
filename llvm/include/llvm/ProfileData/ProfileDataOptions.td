//===- ProfileDataOptions.td - CLV2 option defs for ProfileData -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def ProfileDataOptsReg : Registry<"llvm::pd_opts"> {
  // Getter bodies are compiled once, in ProfileDataOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

// --- InstrProf.cpp ---

def PD_StaticFuncFullModulePrefix : Option<
    "static-func-full-module-prefix",
    "Use full module build paths in the profile counter names for static "
    "functions.",
    "bool"> {
  let Registry = ProfileDataOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def PD_StaticFuncStripDirNamePrefix : Option<
    "static-func-strip-dirname-prefix",
    "Strip specified level of directory name from source path in the "
    "profile counter name for static functions.",
    "unsigned"> {
  let Registry = ProfileDataOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def PD_EnableNameCompression : Option<
    "enable-name-compression",
    "Enable name/filename string compression",
    "bool"> {
  let Registry = ProfileDataOptsReg;
  let Default = "1";
}

def PD_EnableVTableValueProfiling : Option<
    "enable-vtable-value-profiling",
    "If true, the virtual table address will be instrumented to know "
    "the types of a C++ pointer. The information is used in indirect "
    "call promotion to do selective vtable-based comparison.",
    "bool"> {
  let Registry = ProfileDataOptsReg;
  let Default = "0";
}

def PD_EnableVTableProfileUse : Option<
    "enable-vtable-profile-use",
    "If ThinLTO and WPD is enabled and this option is true, vtable "
    "profiles will be used by ICP pass for more efficient indirect "
    "call sequence. If false, type profiles won't be used.",
    "bool"> {
  let Registry = ProfileDataOptsReg;
  let Default = "0";
}

// --- MemProfCommon.cpp ---

def PD_MemProfLifetimeAccessDensityColdThreshold : Option<
    "memprof-lifetime-access-density-cold-threshold",
    "The threshold the lifetime access density must be under to "
    "consider an allocation cold",
    "float"> {
  let Registry = ProfileDataOptsReg;
  let Default = "0.05f";
  let Hidden = 1;
}

def PD_MemProfAveLifetimeColdThreshold : Option<
    "memprof-ave-lifetime-cold-threshold",
    "The average lifetime (s) for an allocation to be considered cold",
    "unsigned"> {
  let Registry = ProfileDataOptsReg;
  let Default = "200";
  let Hidden = 1;
}

def PD_MemProfMinAveLifetimeAccessDensityHotThreshold : Option<
    "memprof-min-ave-lifetime-access-density-hot-threshold",
    "The minimum TotalLifetimeAccessDensity / AllocCount for an "
    "allocation to be considered hot",
    "unsigned"> {
  let Registry = ProfileDataOptsReg;
  let Default = "1000";
  let Hidden = 1;
}

def PD_MemProfUseHotHints : Option<
    "memprof-use-hot-hints",
    "Enable use of hot hints (only supported for unambigously hot "
    "allocations)",
    "bool"> {
  let Registry = ProfileDataOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// --- PGOCtxProfWriter.cpp ---

def PD_CtxProfIncludeEmpty : Option<
    "ctx-prof-include-empty",
    "Also write profiles with all-zero counters. Intended for testing/debugging.",
    "bool"> {
  let Registry = ProfileDataOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// --- ProfileSummaryBuilder.cpp ---

def PD_ProfileSummaryCutoffHot : Option<
    "profile-summary-cutoff-hot",
    "A count is hot if it exceeds the minimum count to reach this "
    "percentile of total counts.",
    "int"> {
  let Registry = ProfileDataOptsReg;
  let Default = "990000";
  let Hidden = 1;
}

def PD_ProfileSummaryCutoffCold : Option<
    "profile-summary-cutoff-cold",
    "A count is cold if it is below the minimum count to reach this "
    "percentile of total counts.",
    "int"> {
  let Registry = ProfileDataOptsReg;
  let Default = "999999";
  let Hidden = 1;
}

def PD_ProfileSummaryHugeWorkingSetSizeThreshold : Option<
    "profile-summary-huge-working-set-size-threshold",
    "The code working set size is considered huge if the number of blocks "
    "required to reach the -profile-summary-cutoff-hot percentile exceeds "
    "this count.",
    "unsigned"> {
  let Registry = ProfileDataOptsReg;
  let Default = "15000";
  let Hidden = 1;
}

def PD_ProfileSummaryLargeWorkingSetSizeThreshold : Option<
    "profile-summary-large-working-set-size-threshold",
    "The code working set size is considered large if the number of blocks "
    "required to reach the -profile-summary-cutoff-hot percentile exceeds "
    "this count.",
    "unsigned"> {
  let Registry = ProfileDataOptsReg;
  let Default = "12500";
  let Hidden = 1;
}

def PD_ProfileSummaryContextless : Option<
    "profile-summary-contextless",
    "Merge context profiles before calculating thresholds.",
    "bool"> {
  let Registry = ProfileDataOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def PD_ProfileSummaryHotCount : Option<
    "profile-summary-hot-count",
    "A fixed hot count that overrides the count derived from "
    "profile-summary-cutoff-hot",
    "uint64_t"> {
  let Registry = ProfileDataOptsReg;
  let Default = "uint64_t{0}";
  let Hidden = 1;
}

def PD_ProfileSummaryColdCount : Option<
    "profile-summary-cold-count",
    "A fixed cold count that overrides the count derived from "
    "profile-summary-cutoff-cold",
    "uint64_t"> {
  let Registry = ProfileDataOptsReg;
  let Default = "uint64_t{0}";
  let Hidden = 1;
}

// --- SampleProf.cpp ---

def PD_ProfileSymbolListCutOff : Option<
    "profile-symbol-list-cutoff",
    "Cutoff value about how many symbols in profile symbol list will be "
    "used.",
    "uint64_t"> {
  let Registry = ProfileDataOptsReg;
  let Default = "(uint64_t)-1";
  let Hidden = 1;
}

def PD_GenerateMergedBaseProfiles : Option<
    "generate-merged-base-profiles",
    "When generating nested context-sensitive profiles, always generate "
    "extra base profile for function with all its context profiles merged "
    "into it.",
    "bool"> {
  let Registry = ProfileDataOptsReg;
  let Default = "0";
}

// --- SampleProfReader.cpp ---

def PD_ProfileIsFS : Option<
    "profile-isfs",
    "Profile uses flow sensitive discriminators",
    "bool"> {
  let Registry = ProfileDataOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// --- SampleProfWriter.cpp ---

def PD_ExtBinaryWriteVTableTypeProf : Option<
    "extbinary-write-vtable-type-prof",
    "Write vtable type profile in ext-binary sample profile writer",
    "bool"> {
  let Registry = ProfileDataOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def PD_RequestedVersion : Option<
    "sample-profile-format-version",
    "Format version to write for extensible binary profiles",
    "uint64_t"> {
  let Registry = ProfileDataOptsReg;
  let Default = "sampleprof::DefaultVersion";
  let Hidden = 1;
}

def PD_WriteMD5ProfSymList : Option<
    "md5-prof-sym-list",
    "Write ProfileSymbolList (Cold Symbols) as 64-bit MD5 hashes in "
    "Eytzinger layout",
    "bool"> {
  let Registry = ProfileDataOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def PD_WriteEytzingerNameTables : Option<
    "sample-profile-write-eytzinger-name-tables",
    "Write Eytzinger 3-span layout for NameTable",
    "bool"> {
  let Registry = ProfileDataOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def PD_LazyLoadNameTable : Option<
    "sample-profile-lazy-load-name-table",
    "Lazy load the name table from the profile.",
    "bool"> {
  let Registry = ProfileDataOptsReg;
  let Default = "true";
  let Hidden = 1;
}
