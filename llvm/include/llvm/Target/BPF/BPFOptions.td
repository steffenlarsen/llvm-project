//===- BPFOptions.td - BPF target options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def BPFOptions : OptionsStruct<"llvm"> {}

// --- BPFAdjustOpt.cpp ---

defm BPF_DisableSerializeICMP : BoolField<
    "bpf-disable-serialize-icmp", "false",
    "BPF: Disable Serializing ICMP insns.",
    /*hidden=*/1>;

defm BPF_DisableAvoidSpeculation : BoolField<
    "bpf-disable-avoid-speculation", "false",
    "BPF: Disable Avoiding Speculative Code Motion.",
    /*hidden=*/1>;

// --- BPFISelLowering.cpp ---

defm BPF_ExpandMemcpyInOrder : BoolField<
    "bpf-expand-memcpy-in-order", "false",
    "Expand memcpy into load/store pairs in order",
    /*hidden=*/1>;

defm BPF_MinimumJumpTableEntries : ValueField<
    "bpf-min-jump-table-entries", "unsigned", "13",
    "Set minimum number of entries to use a jump table on BPF",
    /*hidden=*/1>;

defm BPF_AllowsLibcalls : BoolField<
    "bpf-allows-libcalls", "false",
    "Allow libcalls instead of rejecting unsupported built-in functions",
    /*hidden=*/1>;

// --- BPFMIPeephole.cpp ---

defm BPF_GotolAbsLowBound : ValueField<
    "gotol-abs-low-bound", "int", "16383",
    "Specify gotol lower bound",
    /*hidden=*/1>;

// --- BPFMISimplifyPatchable.cpp ---

defm BPF_DisableCOREOptimization : BoolField<
    "disable-bpf-core-optimization", "false",
    "Disable CORE relocation optimization",
    /*hidden=*/1>;

// --- BPFRegisterInfo.cpp ---

defm BPF_StackSize : ValueField<
    "bpf-stack-size", "int", "512",
    "Specify the BPF stack size limit">;

// --- BPFSubtarget.cpp ---

defm BPF_DisableLdsx : BoolField<
    "disable-ldsx", "false", "Disable ldsx insns",
    /*hidden=*/1>;

defm BPF_DisableMovsx : BoolField<
    "disable-movsx", "false", "Disable movsx insns",
    /*hidden=*/1>;

defm BPF_DisableBswap : BoolField<
    "disable-bswap", "false", "Disable bswap insns",
    /*hidden=*/1>;

defm BPF_DisableSdivSmod : BoolField<
    "disable-sdiv-smod", "false", "Disable sdiv/smod insns",
    /*hidden=*/1>;

defm BPF_DisableGotol : BoolField<
    "disable-gotol", "false", "Disable gotol insn",
    /*hidden=*/1>;

defm BPF_DisableStoreImm : BoolField<
    "disable-storeimm", "false", "Disable BPF_ST (immediate store) insn",
    /*hidden=*/1>;

defm BPF_DisableLoadAcqStoreRel : BoolField<
    "disable-load-acq-store-rel", "false",
    "Disable load-acquire and store-release insns",
    /*hidden=*/1>;

defm BPF_DisableGotox : BoolField<
    "disable-gotox", "false", "Disable gotox insn",
    /*hidden=*/1>;

// --- BPFSelectionDAGInfo.cpp ---

defm BPF_MaxStoresPerMemFunc : ValueField<
    "bpf-max-stores-per-memfunc", "unsigned", "192",
    "Set the maximum number of stores for inlined BPF memory intrinsics",
    /*hidden=*/1>;

// --- BPFTargetMachine.cpp ---

defm BPF_DisableTrapUnreachable : BoolField<
    "bpf-disable-trap-unreachable", "false",
    "Disable Trap Unreachable for BPF",
    /*hidden=*/1>;
