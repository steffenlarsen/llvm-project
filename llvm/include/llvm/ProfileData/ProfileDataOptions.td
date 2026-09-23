//===- ProfileDataOptions.td - Options ------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def ProfileDataOptions : OptionsStruct<"llvm"> {}

// --- InstrProf.cpp ---

defm PD_StaticFuncFullModulePrefix : BoolField<
    "static-func-full-module-prefix", "true",
    "Use full module build paths in the profile counter names for static "
    "functions.",
    /*hidden=*/1>;

defm PD_StaticFuncStripDirNamePrefix : ValueField<
    "static-func-strip-dirname-prefix", "unsigned", "0",
    "Strip specified level of directory name from source path in the "
    "profile counter name for static functions.",
    /*hidden=*/1>;

defm PD_EnableNameCompression : BoolField<
    "enable-name-compression", "true",
    "Enable name/filename string compression">;

defm PD_EnableVTableValueProfiling : BoolField<
    "enable-vtable-value-profiling", "false",
    "If true, the virtual table address will be instrumented to know "
    "the types of a C++ pointer. The information is used in indirect "
    "call promotion to do selective vtable-based comparison.">;

defm PD_EnableVTableProfileUse : BoolField<
    "enable-vtable-profile-use", "false",
    "If ThinLTO and WPD is enabled and this option is true, vtable "
    "profiles will be used by ICP pass for more efficient indirect "
    "call sequence. If false, type profiles won't be used.">;

// --- MemProfCommon.cpp ---

defm PD_MemProfLifetimeAccessDensityColdThreshold : ValueField<
    "memprof-lifetime-access-density-cold-threshold", "float", "0.05f",
    "The threshold the lifetime access density must be under to "
    "consider an allocation cold",
    /*hidden=*/1>;

defm PD_MemProfAveLifetimeColdThreshold : ValueField<
    "memprof-ave-lifetime-cold-threshold", "unsigned", "200",
    "The average lifetime (s) for an allocation to be considered cold",
    /*hidden=*/1>;

defm PD_MemProfMinAveLifetimeAccessDensityHotThreshold : ValueField<
    "memprof-min-ave-lifetime-access-density-hot-threshold", "unsigned",
    "1000",
    "The minimum TotalLifetimeAccessDensity / AllocCount for an "
    "allocation to be considered hot",
    /*hidden=*/1>;

defm PD_MemProfUseHotHints : BoolField<
    "memprof-use-hot-hints", "false",
    "Enable use of hot hints (only supported for unambigously hot "
    "allocations)",
    /*hidden=*/1>;

// --- PGOCtxProfWriter.cpp ---

// std::optional<bool>: the old clv2 option used wasOptSpecified() to
// distinguish "not given" (the ctor's IncludeEmpty parameter wins) from an
// explicit value (always honored), see PGOCtxProfileWriter::PGOCtxProfileWriter()
// in PGOCtxProfWriter.cpp. Hand-written (rather than BoolField<>) to keep the
// original "-ctx-prof-include-empty" / "-no-ctx-prof-include-empty" /
// "-ctx-prof-include-empty=bool" trio, mirroring BoolField's shape.
def PD_CtxProfIncludeEmpty : Flag<["-", "--"], "ctx-prof-include-empty">,
    HelpText<"Also write profiles with all-zero counters. Intended for "
             "testing/debugging.">,
    Flags<[HelpHidden]>,
    Field<"std::optional<bool>", "PD_CtxProfIncludeEmpty", "std::nullopt">,
    Assign<"PD_CtxProfIncludeEmpty", "true">;
def PD_CtxProfIncludeEmpty_no :
    Flag<["-", "--"], "no-ctx-prof-include-empty">,
    Flags<[HelpHidden]>, Assign<"PD_CtxProfIncludeEmpty", "false">;
def PD_CtxProfIncludeEmpty_eq :
    Joined<["-", "--"], "ctx-prof-include-empty=">,
    Flags<[HelpHidden]>, BoolAssign<"PD_CtxProfIncludeEmpty">;

// --- ProfileSummaryBuilder.cpp ---

defm PD_ProfileSummaryCutoffHot : ValueField<
    "profile-summary-cutoff-hot", "int", "990000",
    "A count is hot if it exceeds the minimum count to reach this "
    "percentile of total counts.",
    /*hidden=*/1>;

defm PD_ProfileSummaryCutoffCold : ValueField<
    "profile-summary-cutoff-cold", "int", "999999",
    "A count is cold if it is below the minimum count to reach this "
    "percentile of total counts.",
    /*hidden=*/1>;

defm PD_ProfileSummaryHugeWorkingSetSizeThreshold : ValueField<
    "profile-summary-huge-working-set-size-threshold", "unsigned", "15000",
    "The code working set size is considered huge if the number of blocks "
    "required to reach the -profile-summary-cutoff-hot percentile exceeds "
    "this count.",
    /*hidden=*/1>;

defm PD_ProfileSummaryLargeWorkingSetSizeThreshold : ValueField<
    "profile-summary-large-working-set-size-threshold", "unsigned", "12500",
    "The code working set size is considered large if the number of blocks "
    "required to reach the -profile-summary-cutoff-hot percentile exceeds "
    "this count.",
    /*hidden=*/1>;

// std::optional<bool>: the old clv2 option used wasOptSpecified() to
// distinguish "not given" (default to merging context profiles only when
// FunctionSamples::ProfileIsCS is set, see
// SampleProfileSummaryBuilder::computeSummaryForProfiles() in
// ProfileSummaryBuilder.cpp) from an explicit value (always honored).
// Hand-written (rather than BoolField<>) to keep the original
// "-profile-summary-contextless" / "-no-profile-summary-contextless" /
// "-profile-summary-contextless=bool" trio, mirroring BoolField's shape.
def PD_ProfileSummaryContextless : Flag<["-", "--"], "profile-summary-contextless">,
    HelpText<"Merge context profiles before calculating thresholds.">,
    Flags<[HelpHidden]>,
    Field<"std::optional<bool>", "PD_ProfileSummaryContextless", "std::nullopt">,
    Assign<"PD_ProfileSummaryContextless", "true">;
def PD_ProfileSummaryContextless_no :
    Flag<["-", "--"], "no-profile-summary-contextless">,
    Flags<[HelpHidden]>, Assign<"PD_ProfileSummaryContextless", "false">;
def PD_ProfileSummaryContextless_eq :
    Joined<["-", "--"], "profile-summary-contextless=">,
    Flags<[HelpHidden]>, BoolAssign<"PD_ProfileSummaryContextless">;

// std::optional<uint64_t>: the old clv2 option used wasOptSpecified() to
// distinguish "not given" (the hot threshold is derived from
// -profile-summary-cutoff-hot) from an explicit value that overrides it, see
// ProfileSummaryBuilder::getHotCountThreshold() in ProfileSummaryBuilder.cpp.
// Hand-written (rather than OptionalValueField<>) to keep the original
// Joined ("-profile-summary-hot-count=N") + Separate
// ("-profile-summary-hot-count N") trio, mirroring ValueField's shape; the
// original never had a bare flag.
def PD_ProfileSummaryHotCount :
    Joined<["-", "--"], "profile-summary-hot-count=">,
    HelpText<"A fixed hot count that overrides the count derived from "
             "profile-summary-cutoff-hot">,
    Flags<[HelpHidden]>,
    Field<"std::optional<uint64_t>", "PD_ProfileSummaryHotCount",
          "std::nullopt">;
def PD_ProfileSummaryHotCount_sep :
    Separate<["-", "--"], "profile-summary-hot-count">,
    Flags<[HelpHidden]>, Alias<PD_ProfileSummaryHotCount>;

// std::optional<uint64_t>: same wasOptSpecified() rationale as
// PD_ProfileSummaryHotCount above, see
// ProfileSummaryBuilder::getColdCountThreshold().
def PD_ProfileSummaryColdCount :
    Joined<["-", "--"], "profile-summary-cold-count=">,
    HelpText<"A fixed cold count that overrides the count derived from "
             "profile-summary-cutoff-cold">,
    Flags<[HelpHidden]>,
    Field<"std::optional<uint64_t>", "PD_ProfileSummaryColdCount",
          "std::nullopt">;
def PD_ProfileSummaryColdCount_sep :
    Separate<["-", "--"], "profile-summary-cold-count">,
    Flags<[HelpHidden]>, Alias<PD_ProfileSummaryColdCount>;

// --- SampleProf.cpp ---

defm PD_ProfileSymbolListCutOff : ValueField<
    "profile-symbol-list-cutoff", "uint64_t", "(uint64_t)-1",
    "Cutoff value about how many symbols in profile symbol list will be "
    "used.",
    /*hidden=*/1>;

defm PD_GenerateMergedBaseProfiles : BoolField<
    "generate-merged-base-profiles", "false",
    "When generating nested context-sensitive profiles, always generate "
    "extra base profile for function with all its context profiles merged "
    "into it.">;

// --- SampleProfReader.cpp ---

defm PD_ProfileIsFS : BoolField<
    "profile-isfs", "false",
    "Profile uses flow sensitive discriminators",
    /*hidden=*/1>;

// --- SampleProfWriter.cpp ---

defm PD_ExtBinaryWriteVTableTypeProf : BoolField<
    "extbinary-write-vtable-type-prof", "false",
    "Write vtable type profile in ext-binary sample profile writer",
    /*hidden=*/1>;

// std::optional<uint64_t>: the old clv2 option used wasOptSpecified() to
// distinguish "not given" (SampleProfileWriter::create() picks the format
// version itself, e.g. CompositeProfileVersion when
// -extbinary-composite-prof is set) from an explicit value (always honored,
// see SampleProfWriter.cpp). Hand-written (rather than OptionalValueField<>)
// to keep the original Joined ("-sample-profile-format-version=N") +
// Separate ("-sample-profile-format-version N") trio, mirroring ValueField's
// shape; the original never had a bare flag.
def PD_RequestedVersion : Joined<["-", "--"], "sample-profile-format-version=">,
    HelpText<"Format version to write for extensible binary profiles">,
    Flags<[HelpHidden]>,
    Field<"std::optional<uint64_t>", "PD_RequestedVersion", "std::nullopt">;
def PD_RequestedVersion_sep :
    Separate<["-", "--"], "sample-profile-format-version">,
    Flags<[HelpHidden]>, Alias<PD_RequestedVersion>;

defm PD_ExtBinaryCompositeProf : BoolField<
    "extbinary-composite-prof", "false",
    "Use the composite profile format",
    /*hidden=*/1>;

defm PD_WriteMD5ProfSymList : BoolField<
    "md5-prof-sym-list", "false",
    "Write ProfileSymbolList (Cold Symbols) as 64-bit MD5 hashes in "
    "Eytzinger layout",
    /*hidden=*/1>;

defm PD_WriteEytzingerNameTables : BoolField<
    "sample-profile-write-eytzinger-name-tables", "false",
    "Write Eytzinger 3-span layout for NameTable",
    /*hidden=*/1>;

defm PD_LazyLoadNameTable : BoolField<
    "sample-profile-lazy-load-name-table", "true",
    "Lazy load the name table from the profile.",
    /*hidden=*/1>;
