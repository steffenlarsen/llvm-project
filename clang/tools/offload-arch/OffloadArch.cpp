//===- OffloadArch.cpp - list available GPUs ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/Version.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/RegisterLLVMOptions.h"

using namespace llvm;

static cl::OptionCategory OffloadArchCategory("offload-arch options");

extern cl::OptionCategory AMDGPUArchByHIPCategory;

// Defined in AMDGPUArchByHIP.cpp; declared here as an opaque scoped enum
// (a complete type even without its enumerators) purely to thread the
// parsed value back out of configureAMDGPUArchByHIP.
enum class HipApiVersion;

void configureAMDGPUArchByHIP(clv2::OptionParser &P, HipApiVersion &HipApi);

enum VendorName {
  all,
  amdgpu,
  nvptx,
  intel,
};

// --- constexpr option descriptors ---
inline constexpr clv2::OptionInfo<bool> OAHelpOpt{"h", "Alias for -help",
                                                  clv2::Hidden};

inline constexpr clv2::EnumVal<VendorName> OAOnlyVals[] = {
    {"all", VendorName::all, "Print all GPUs (default)"},
    {"amdgpu", VendorName::amdgpu, "Only print AMD GPUs"},
    {"nvptx", VendorName::nvptx, "Only print NVIDIA GPUs"},
    {"intel", VendorName::intel, "Only print Intel GPUs"},
};

inline constexpr auto OAOnlyOpt = clv2::makeEnumOption<VendorName>(
    "only", "Restrict to vendor:", OAOnlyVals, clv2::Init{VendorName::all});

inline constexpr clv2::OptionInfo<bool> OAVerboseOpt{"verbose",
                                                     "Enable verbose output"};

inline constexpr clv2::OptionsRegistry<&OAHelpOpt, &OAOnlyOpt, &OAVerboseOpt>
    OffloadArchReg;

struct OffloadArchOptions {
  bool Help = false;
  VendorName Only = all;
  bool Verbose = false;
};

static void
applyOffloadArchOpts(const decltype(OffloadArchReg)::ParsedOptionsT &Opts,
                     OffloadArchOptions &ToolOpts) {
  ToolOpts.Help = Opts.get<&OAHelpOpt>();
  ToolOpts.Only = Opts.get<&OAOnlyOpt>();
  ToolOpts.Verbose = Opts.get<&OAVerboseOpt>();
}

static void PrintVersion(raw_ostream &OS) {
  OS << clang::getClangToolFullVersion("offload-arch") << '\n';
}

int printGPUsByKFD();
int printGPUsByHIP(bool Verbose, HipApiVersion HipApi);
int printGPUsByCUDA(bool Verbose);
int printGPUsByLevelZero(bool Verbose);

static int printAMD(bool Verbose, HipApiVersion HipApi) {
#ifndef _WIN32
  if (!printGPUsByKFD())
    return 0;
#endif

  return printGPUsByHIP(Verbose, HipApi);
}

static int printNVIDIA(bool Verbose) { return printGPUsByCUDA(Verbose); }
static int printIntel(bool Verbose) { return printGPUsByLevelZero(Verbose); }

int main(int argc, char *argv[]) {
  clv2::OptionParser P;
  OffloadArchOptions ToolOpts;
  {
    using ParsedT = decltype(OffloadArchReg)::ParsedOptionsT;
    auto *Storage = new ParsedT();
    decltype(OffloadArchReg)::applyDefaultsTo(*Storage);
    std::vector<clv2::detail::OptionEntry> Entries;
    std::vector<clv2::detail::AliasEntry> Aliases;
    std::vector<clv2::detail::SubCommandSpec> SubSpecs;
    decltype(OffloadArchReg)::staticBuildInto(*Storage, Entries, Aliases,
                                              SubSpecs);
    for (auto &E : Entries) {
      if (!E.Cat)
        E.Cat = &OffloadArchCategory;
      P.addDynamicEntry(std::move(E));
    }
    clv2::registerDynamicPostParseCallback(
        [Storage, &ToolOpts]() { applyOffloadArchOpts(*Storage, ToolOpts); });
  }
  HipApiVersion HipApi{};
  configureAMDGPUArchByHIP(P, HipApi);
  RegisterCoreLLVMOptions(P);
  P.hideUnrelatedOptions({&OffloadArchCategory, &AMDGPUArchByHIPCategory});
  P.parse(
      argc, argv,
      "A tool to detect the presence of offloading devices on the system. \n\n"
      "The tool will output each detected GPU architecture separated by a\n"
      "newline character. If multiple GPUs of the same architecture are found\n"
      "a string will be printed for each\n",
      /*Errs=*/nullptr, /*VersionString=*/{},
      /*HelpOS=*/nullptr, PrintVersion);

  if (ToolOpts.Help) {
    cl::PrintHelpMessage();
    return 0;
  }

  // Support legacy binaries.
  if (sys::path::stem(argv[0]).starts_with("amdgpu-arch"))
    ToolOpts.Only = VendorName::amdgpu;
  if (sys::path::stem(argv[0]).starts_with("nvptx-arch"))
    ToolOpts.Only = VendorName::nvptx;

  // Named so each closure outlives the function_ref that points at it;
  // function_ref does not extend the lifetime of its callable.
  auto DoAMD = [&] { return printAMD(ToolOpts.Verbose, HipApi); };
  auto DoNVIDIA = [&] { return printNVIDIA(ToolOpts.Verbose); };
  auto DoIntel = [&] { return printIntel(ToolOpts.Verbose); };
  const std::array<std::pair<VendorName, function_ref<int()>>, 3> VendorTable{
      {{VendorName::amdgpu, DoAMD},
       {VendorName::nvptx, DoNVIDIA},
       {VendorName::intel, DoIntel}}};

  int Result = 1;
  for (auto [Name, Func] : VendorTable) {
    if (ToolOpts.Only == VendorName::all || ToolOpts.Only == Name)
      Result &= Func();
  }

  return Result;
}
