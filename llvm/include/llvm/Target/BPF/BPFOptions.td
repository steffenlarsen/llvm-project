//===- BPFOptions.td - CLV2 option defs for BPF -----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def BPFOptsReg : Registry<"llvm::bpf_opts"> {
  // Getter bodies are compiled once, in BPFOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

// --- BPFAdjustOpt.cpp ---

def BPF_DisableSerializeICMP : Option<
    "bpf-disable-serialize-icmp",
    "BPF: Disable Serializing ICMP insns.",
    "bool"> {
  let Default = "false";
  let Registry = BPFOptsReg;
  let Hidden = 1;
}

def BPF_DisableAvoidSpeculation : Option<
    "bpf-disable-avoid-speculation",
    "BPF: Disable Avoiding Speculative Code Motion.",
    "bool"> {
  let Default = "false";
  let Registry = BPFOptsReg;
  let Hidden = 1;
}

// --- BPFISelLowering.cpp ---

def BPF_ExpandMemcpyInOrder : Option<
    "bpf-expand-memcpy-in-order",
    "Expand memcpy into load/store pairs in order",
    "bool"> {
  let Registry = BPFOptsReg;
  let Hidden = 1;
}

def BPF_MinimumJumpTableEntries : Option<
    "bpf-min-jump-table-entries",
    "Set minimum number of entries to use a jump table on BPF",
    "unsigned"> {
  let Registry = BPFOptsReg;
  let Default = "13";
  let Hidden = 1;
}

def BPF_AllowsLibcalls : Option<
    "bpf-allows-libcalls",
    "Allow libcalls instead of rejecting unsupported built-in functions",
    "bool"> {
  let Registry = BPFOptsReg;
  let Hidden = 1;
}

// --- BPFMIPeephole.cpp ---

def BPF_GotolAbsLowBound : Option<
    "gotol-abs-low-bound",
    "Specify gotol lower bound",
    "int"> {
  let Registry = BPFOptsReg;
  let Default = "16383";
  let Hidden = 1;
}

// --- BPFMISimplifyPatchable.cpp ---

def BPF_DisableCOREOptimization : Option<
    "disable-bpf-core-optimization",
    "Disable CORE relocation optimization",
    "bool"> {
  let Default = "false";
  let Registry = BPFOptsReg;
  let Hidden = 1;
}

// --- BPFRegisterInfo.cpp ---

def BPF_StackSize : Option<
    "bpf-stack-size",
    "Specify the BPF stack size limit",
    "int"> {
  let Registry = BPFOptsReg;
  let Default = "512";
}

// --- BPFSubtarget.cpp ---

def BPF_DisableLdsx : Option<
    "disable-ldsx", "Disable ldsx insns",
    "bool"> {
  let Registry = BPFOptsReg;
  let Hidden = 1;
}

def BPF_DisableMovsx : Option<
    "disable-movsx", "Disable movsx insns",
    "bool"> {
  let Registry = BPFOptsReg;
  let Hidden = 1;
}

def BPF_DisableBswap : Option<
    "disable-bswap", "Disable bswap insns",
    "bool"> {
  let Registry = BPFOptsReg;
  let Hidden = 1;
}

def BPF_DisableSdivSmod : Option<
    "disable-sdiv-smod", "Disable sdiv/smod insns",
    "bool"> {
  let Registry = BPFOptsReg;
  let Hidden = 1;
}

def BPF_DisableGotol : Option<
    "disable-gotol", "Disable gotol insn",
    "bool"> {
  let Registry = BPFOptsReg;
  let Hidden = 1;
}

def BPF_DisableStoreImm : Option<
    "disable-storeimm", "Disable BPF_ST (immediate store) insn",
    "bool"> {
  let Registry = BPFOptsReg;
  let Hidden = 1;
}

def BPF_DisableLoadAcqStoreRel : Option<
    "disable-load-acq-store-rel",
    "Disable load-acquire and store-release insns",
    "bool"> {
  let Registry = BPFOptsReg;
  let Hidden = 1;
}

def BPF_DisableGotox : Option<
    "disable-gotox", "Disable gotox insn",
    "bool"> {
  let Registry = BPFOptsReg;
  let Hidden = 1;
}

// --- BPFSelectionDAGInfo.cpp ---

def BPF_MaxStoresPerMemFunc : Option<
    "bpf-max-stores-per-memfunc",
    "Set the maximum number of stores for inlined BPF memory intrinsics",
    "unsigned"> {
  let Registry = BPFOptsReg;
  let Default = "192";
  let Hidden = 1;
}

// --- BPFTargetMachine.cpp ---

def BPF_DisableTrapUnreachable : Option<
    "bpf-disable-trap-unreachable",
    "Disable Trap Unreachable for BPF",
    "bool"> {
  let Registry = BPFOptsReg;
  let Hidden = 1;
}
