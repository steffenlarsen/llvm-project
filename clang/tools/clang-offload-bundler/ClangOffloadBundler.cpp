//===-- clang-offload-bundler/ClangOffloadBundler.cpp ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a stand-alone clang-offload-bundler tool using the
/// OffloadBundler API.
///
//===----------------------------------------------------------------------===//

#include "clang/Basic/Cuda.h"
#include "clang/Basic/TargetID.h"
#include "clang/Basic/Version.h"
#include "clang/Driver/OffloadBundler.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/ArchiveWriter.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <forward_list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <utility>

using namespace llvm;
using namespace llvm::object;
using namespace clang;

static void PrintVersion(raw_ostream &OS) {
  OS << clang::getClangToolFullVersion("clang-offload-bundler") << '\n';
}

// Mark all our options with this category, everything else (except for
// -version and -help) will be hidden.
inline constexpr clv2::OptionCategory
    ClangOffloadBundlerCategory("clang-offload-bundler options");

// --- constexpr option descriptors ---
inline constexpr clv2::OptionInfo<bool> OBHelpOpt{
    "h", "Alias for -help", clv2::Hidden,
    clv2::cat(ClangOffloadBundlerCategory)};

inline constexpr clv2::ListOptionInfo<std::string> OBInputOpt{
    "input",
    "Input file. Can be specified multiple times "
    "for multiple input files.",
    clv2::value_desc("string"), clv2::cat(ClangOffloadBundlerCategory)};

inline constexpr clv2::ListOptionInfo<std::string> OBInputsOpt{
    "inputs", "[<input file>,...] (deprecated)", clv2::value_desc("string"),
    clv2::CommaSeparated, clv2::cat(ClangOffloadBundlerCategory)};

inline constexpr clv2::ListOptionInfo<std::string> OBOutputOpt{
    "output",
    "Output file. Can be specified multiple times "
    "for multiple output files.",
    clv2::value_desc("string"), clv2::cat(ClangOffloadBundlerCategory)};

inline constexpr clv2::ListOptionInfo<std::string> OBOutputsOpt{
    "outputs", "[<output file>,...] (deprecated)", clv2::value_desc("string"),
    clv2::CommaSeparated, clv2::cat(ClangOffloadBundlerCategory)};

inline constexpr clv2::ListOptionInfo<std::string> OBTargetsOpt{
    "targets", "[<offload kind>-<target triple>,...]",
    clv2::value_desc("string"), clv2::CommaSeparated,
    clv2::cat(ClangOffloadBundlerCategory)};

inline constexpr clv2::OptionInfo<std::string> OBTypeOpt{
    "type",
    "Type of the files to be bundled/unbundled.\n"
    "Current supported types are:\n"
    "  i    - cpp-output\n"
    "  ii   - c++-cpp-output\n"
    "  cui  - cuda-cpp-output\n"
    "  hipi - hip-cpp-output\n"
    "  d    - dependency\n"
    "  ll   - llvm\n"
    "  bc   - llvm-bc\n"
    "  s    - assembler\n"
    "  o    - object\n"
    "  a    - archive of objects\n"
    "  gch  - precompiled-header\n"
    "  ast  - clang AST file",
    clv2::Required, clv2::value_desc("string"),
    clv2::cat(ClangOffloadBundlerCategory)};

inline constexpr clv2::OptionInfo<bool> OBUnbundleOpt{
    "unbundle", "Unbundle bundled file into several output files.",
    clv2::cat(ClangOffloadBundlerCategory)};

inline constexpr clv2::OptionInfo<bool> OBListOpt{
    "list", "List bundle IDs in the bundled file.",
    clv2::cat(ClangOffloadBundlerCategory)};

inline constexpr clv2::OptionInfo<bool> OBPrintExternalCommandsOpt{
    "###",
    "Print any external commands that are to be executed "
    "instead of actually executing them - for testing purposes.",
    clv2::cat(ClangOffloadBundlerCategory)};

inline constexpr clv2::OptionInfo<bool> OBAllowMissingBundlesOpt{
    "allow-missing-bundles",
    "Create empty files if bundles are missing "
    "when unbundling.",
    clv2::cat(ClangOffloadBundlerCategory)};

inline constexpr clv2::OptionInfo<unsigned> OBBundleAlignOpt{
    "bundle-align", "Alignment of bundle for binary files", clv2::Init{1u},
    clv2::value_desc("uint"), clv2::cat(ClangOffloadBundlerCategory)};

inline constexpr clv2::OptionInfo<bool> OBCheckInputArchiveOpt{
    "check-input-archive",
    "Check if input heterogeneous archive is "
    "valid in terms of TargetID rules.",
    clv2::cat(ClangOffloadBundlerCategory)};

inline constexpr clv2::OptionInfo<bool> OBHipOpenmpCompatibleOpt{
    "hip-openmp-compatible",
    "Treat hip and hipv4 offload kinds as "
    "compatible with openmp kind, and vice versa.",
    clv2::cat(ClangOffloadBundlerCategory)};

inline constexpr clv2::OptionInfo<bool> OBCompressOpt{
    "compress", "Compress output file when bundling.",
    clv2::cat(ClangOffloadBundlerCategory)};

inline constexpr clv2::OptionInfo<bool> OBVerboseOpt{
    "verbose", "Print debug information.",
    clv2::cat(ClangOffloadBundlerCategory)};

inline constexpr clv2::OptionInfo<int> OBCompressionLevelOpt{
    "compression-level", "Specify the compression level (integer)",
    clv2::Init{0}, clv2::value_desc("n"),
    clv2::cat(ClangOffloadBundlerCategory)};

inline constexpr clv2::OptionsRegistry<
    &OBHelpOpt, &OBInputOpt, &OBInputsOpt, &OBOutputOpt, &OBOutputsOpt,
    &OBTargetsOpt, &OBTypeOpt, &OBUnbundleOpt, &OBListOpt,
    &OBPrintExternalCommandsOpt, &OBAllowMissingBundlesOpt, &OBBundleAlignOpt,
    &OBCheckInputArchiveOpt, &OBHipOpenmpCompatibleOpt, &OBCompressOpt,
    &OBVerboseOpt, &OBCompressionLevelOpt>
    OffloadBundlerReg;

int main(int argc, const char **argv) {

  // Process commandline options and report errors
  sys::PrintStackTraceOnErrorSignal(argv[0]);

  clv2::OptionParser P;
  P.add<&OffloadBundlerReg>();
  RegisterAllLLVMOptions(P);
  P.hideUnrelatedOptions({&ClangOffloadBundlerCategory});
  static constexpr llvm::StringLiteral Overview =
      "A tool to bundle several input files of the specified type <type> \n"
      "referring to the same source file but different targets into a single \n"
      "one. The resulting file can also be unbundled into different files by \n"
      "this tool if -unbundle is provided.\n";
  auto OptsCtx = P.parse(argc, argv, Overview,
                         /*Errs=*/nullptr, /*VersionString=*/{},
                         /*HelpOS=*/nullptr, PrintVersion);
  auto *Opts = OptsCtx->getViewPtr<&OffloadBundlerReg>();

  // Extract parsed values.
  bool Help = Opts->get<&OBHelpOpt>();
  std::vector<std::string> InputFileNames = Opts->get<&OBInputOpt>();
  bool InputFileNamesSet = Opts->specified<&OBInputOpt>();
  std::vector<std::string> InputFileNamesDeprecatedOpt =
      Opts->get<&OBInputsOpt>();
  bool InputFileNamesDeprecatedOptSet = Opts->specified<&OBInputsOpt>();
  std::vector<std::string> OutputFileNames = Opts->get<&OBOutputOpt>();
  bool OutputFileNamesSet = Opts->specified<&OBOutputOpt>();
  std::vector<std::string> OutputFileNamesDeprecatedOpt =
      Opts->get<&OBOutputsOpt>();
  bool OutputFileNamesDeprecatedOptSet = Opts->specified<&OBOutputsOpt>();
  std::vector<std::string> TargetNames = Opts->get<&OBTargetsOpt>();
  bool TargetNamesSet = Opts->specified<&OBTargetsOpt>();
  std::string FilesType = Opts->get<&OBTypeOpt>();
  bool Unbundle = Opts->get<&OBUnbundleOpt>();
  bool ListBundleIDs = Opts->get<&OBListOpt>();
  bool PrintExternalCommands = Opts->get<&OBPrintExternalCommandsOpt>();
  bool AllowMissingBundles = Opts->get<&OBAllowMissingBundlesOpt>();
  unsigned BundleAlignment = Opts->get<&OBBundleAlignOpt>();
  bool CheckInputArchive = Opts->get<&OBCheckInputArchiveOpt>();
  bool HipOpenmpCompatible = Opts->get<&OBHipOpenmpCompatibleOpt>();
  bool Compress = Opts->get<&OBCompressOpt>();
  bool CompressSet = Opts->specified<&OBCompressOpt>();
  bool Verbose = Opts->get<&OBVerboseOpt>();
  bool VerboseSet = Opts->specified<&OBVerboseOpt>();
  int CompressionLevel = Opts->get<&OBCompressionLevelOpt>();
  bool CompressionLevelSet = Opts->specified<&OBCompressionLevelOpt>();

  if (Help) {
    P.printHelp(llvm::outs(), Overview, argv[0]);
    return 0;
  }

  /// Class to store bundler options in standard (non-cl::opt) data structures
  // Avoid using cl::opt variables after these assignments when possible
  OffloadBundlerConfig BundlerConfig;
  BundlerConfig.AllowMissingBundles = AllowMissingBundles;
  BundlerConfig.CheckInputArchive = CheckInputArchive;
  BundlerConfig.PrintExternalCommands = PrintExternalCommands;
  BundlerConfig.HipOpenmpCompatible = HipOpenmpCompatible;
  BundlerConfig.BundleAlignment = BundleAlignment;
  BundlerConfig.FilesType = FilesType;
  BundlerConfig.ObjcopyPath = "";
  // Do not override the default value Compress and Verbose in BundlerConfig.
  if (CompressSet)
    BundlerConfig.Compress = Compress;
  if (VerboseSet)
    BundlerConfig.Verbose = Verbose;
  if (CompressionLevelSet)
    BundlerConfig.CompressionLevel = CompressionLevel;

  BundlerConfig.TargetNames.assign(TargetNames.begin(), TargetNames.end());
  BundlerConfig.InputFileNames.assign(InputFileNames.begin(),
                                      InputFileNames.end());
  BundlerConfig.OutputFileNames.assign(OutputFileNames.begin(),
                                       OutputFileNames.end());

  /// The index of the host input in the list of inputs.
  BundlerConfig.HostInputIndex = ~0u;

  /// Whether not having host target is allowed.
  BundlerConfig.AllowNoHost = false;

  auto reportError = [argv](Error E) {
    logAllUnhandledErrors(std::move(E), WithColor::error(errs(), argv[0]));
    return 1;
  };

  auto doWork = [&](std::function<llvm::Error()> Work) {
    if (llvm::Error Err = Work()) {
      return reportError(std::move(Err));
    }
    return 0;
  };

  auto warningOS = [argv]() -> raw_ostream & {
    return WithColor::warning(errs(), StringRef(argv[0]));
  };

  /// Path to the current binary.
  std::string BundlerExecutable = argv[0];

  if (!llvm::sys::fs::exists(BundlerExecutable))
    BundlerExecutable =
      sys::fs::getMainExecutable(argv[0], &BundlerExecutable);

  // Find llvm-objcopy in order to create the bundle binary.
  ErrorOr<std::string> Objcopy = sys::findProgramByName(
    "llvm-objcopy",
    sys::path::parent_path(BundlerExecutable));
  if (!Objcopy)
    Objcopy = sys::findProgramByName("llvm-objcopy");
  if (!Objcopy)
    return reportError(createStringError(
        Objcopy.getError(), "unable to find 'llvm-objcopy' in path"));
  else
    BundlerConfig.ObjcopyPath = *Objcopy;

  if (InputFileNamesSet && InputFileNamesDeprecatedOptSet) {
    return reportError(createStringError(
        errc::invalid_argument,
        "-inputs and -input cannot be used together, use only -input instead"));
  }

  if (!InputFileNamesDeprecatedOpt.empty()) {
    warningOS() << "-inputs is deprecated, use -input instead\n";
    // temporary hack to support -inputs
    InputFileNames.insert(InputFileNames.end(),
                          InputFileNamesDeprecatedOpt.begin(),
                          InputFileNamesDeprecatedOpt.end());
  }
  BundlerConfig.InputFileNames.assign(InputFileNames.begin(),
                                      InputFileNames.end());

  if (OutputFileNamesSet && OutputFileNamesDeprecatedOptSet) {
    return reportError(createStringError(errc::invalid_argument,
                                         "-outputs and -output cannot be used "
                                         "together, use only -output instead"));
  }

  if (!OutputFileNamesDeprecatedOpt.empty()) {
    warningOS() << "-outputs is deprecated, use -output instead\n";
    // temporary hack to support -outputs
    OutputFileNames.insert(OutputFileNames.end(),
                           OutputFileNamesDeprecatedOpt.begin(),
                           OutputFileNamesDeprecatedOpt.end());
  }
  BundlerConfig.OutputFileNames.assign(OutputFileNames.begin(),
                                       OutputFileNames.end());

  if (ListBundleIDs) {
    if (Unbundle) {
      return reportError(
          createStringError(errc::invalid_argument,
                            "-unbundle and -list cannot be used together"));
    }
    if (InputFileNames.size() != 1) {
      return reportError(createStringError(
          errc::invalid_argument, "only one input file supported for -list"));
    }
    if (OutputFileNames.size()) {
      return reportError(createStringError(
          errc::invalid_argument, "-outputs option is invalid for -list"));
    }
    if (TargetNames.size()) {
      return reportError(createStringError(
          errc::invalid_argument, "-targets option is invalid for -list"));
    }

    return doWork([&]() {
      return OffloadBundler::ListBundleIDsInFile(InputFileNames.front(),
                                                 BundlerConfig);
    });
  }

  if (BundlerConfig.CheckInputArchive) {
    if (!Unbundle) {
      return reportError(createStringError(
          errc::invalid_argument, "-check-input-archive cannot be used while "
                                  "bundling"));
    }
    if (Unbundle && BundlerConfig.FilesType != "a") {
      return reportError(createStringError(
          errc::invalid_argument, "-check-input-archive can only be used for "
                                  "unbundling archives (-type=a)"));
    }
  }

  if (OutputFileNames.size() == 0) {
    return reportError(
        createStringError(errc::invalid_argument, "no output file specified!"));
  }

  if (!TargetNamesSet) {
    return reportError(createStringError(
        errc::invalid_argument,
        "for the --targets option: must be specified at least once!"));
  }

  if (Unbundle) {
    if (InputFileNames.size() != 1) {
      return reportError(createStringError(
          errc::invalid_argument,
          "only one input file supported in unbundling mode"));
    }
    if (OutputFileNames.size() != TargetNames.size()) {
      return reportError(createStringError(
          errc::invalid_argument, "number of output files and targets should "
                                  "match in unbundling mode"));
    }
  } else {
    if (BundlerConfig.FilesType == "a") {
      return reportError(createStringError(errc::invalid_argument,
                                           "Archive files are only supported "
                                           "for unbundling"));
    }
    if (OutputFileNames.size() != 1) {
      return reportError(
          createStringError(errc::invalid_argument,
                            "only one output file supported in bundling mode"));
    }
    if (InputFileNames.size() != TargetNames.size()) {
      return reportError(createStringError(
          errc::invalid_argument,
          "number of input files and targets should match in bundling mode"));
    }
  }

  // Verify that the offload kinds and triples are known. We also check that we
  // have exactly one host target.
  unsigned Index = 0u;
  unsigned HostTargetNum = 0u;
  bool HIPOnly = true;
  llvm::DenseSet<StringRef> ParsedTargets;
  // Map {offload-kind}-{triple} to target IDs.
  std::map<std::string, std::set<StringRef>> TargetIDs;
  // Standardize target names to include env field
  std::vector<std::string> StandardizedTargetNames;
  for (StringRef Target : TargetNames) {
    if (!ParsedTargets.insert(Target).second) {
      return reportError(createStringError(
          errc::invalid_argument, "Duplicate targets are not allowed"));
    }

    if (!checkOffloadBundleID(Target)) {
      return reportError(createStringError(
          errc::invalid_argument,
          "Targets need to follow the format '<offload kind>-<target triple>', "
          "where '<target triple>' follows the format "
          "'<kind>-<arch>-<vendor>-<os>-<env>[-<target id>[:target "
          "features]]'."));
    }

    auto OffloadInfo = OffloadTargetInfo(Target, BundlerConfig);
    bool KindIsValid = OffloadInfo.isOffloadKindValid();
    bool TripleIsValid = OffloadInfo.isTripleValid();

    StandardizedTargetNames.push_back(OffloadInfo.str());

    if (!KindIsValid || !TripleIsValid) {
      SmallVector<char, 128u> Buf;
      raw_svector_ostream Msg(Buf);
      Msg << "invalid target '" << Target << "'";
      if (!KindIsValid)
        Msg << ", unknown offloading kind '" << OffloadInfo.OffloadKind << "'";
      if (!TripleIsValid)
        Msg << ", unknown target triple '" << OffloadInfo.Triple.str() << "'";
      return reportError(createStringError(errc::invalid_argument, Msg.str()));
    }

    TargetIDs[OffloadInfo.OffloadKind.str() + "-" + OffloadInfo.Triple.str()]
        .insert(OffloadInfo.TargetID);
    if (KindIsValid && OffloadInfo.hasHostKind()) {
      ++HostTargetNum;
      // Save the index of the input that refers to the host.
      BundlerConfig.HostInputIndex = Index;
    }

    if (OffloadInfo.OffloadKind != "hip" && OffloadInfo.OffloadKind != "hipv4")
      HIPOnly = false;

    ++Index;
  }

  BundlerConfig.TargetNames.assign(StandardizedTargetNames.begin(),
                                   StandardizedTargetNames.end());

  for (const auto &TargetID : TargetIDs) {
    if (auto ConflictingTID =
            clang::getConflictTargetIDCombination(TargetID.second)) {
      SmallVector<char, 128u> Buf;
      raw_svector_ostream Msg(Buf);
      Msg << "Cannot bundle inputs with conflicting targets: '"
          << TargetID.first + "-" + ConflictingTID->first << "' and '"
          << TargetID.first + "-" + ConflictingTID->second << "'";
      return reportError(createStringError(errc::invalid_argument, Msg.str()));
    }
  }

  // HIP uses clang-offload-bundler to bundle device-only compilation results
  // for multiple GPU archs, therefore allow no host target if all entries
  // are for HIP.
  BundlerConfig.AllowNoHost = HIPOnly;

  // Host triple is not really needed for unbundling operation, so do not
  // treat missing host triple as error if we do unbundling.
  if ((Unbundle && HostTargetNum > 1) ||
      (!Unbundle && HostTargetNum != 1 && !BundlerConfig.AllowNoHost)) {
    return reportError(createStringError(
        errc::invalid_argument,
        "expecting exactly one host target but got " + Twine(HostTargetNum)));
  }

  OffloadBundler Bundler(BundlerConfig);

  return doWork([&]() {
    if (Unbundle)
      return (BundlerConfig.FilesType == "a") ? Bundler.UnbundleArchive()
                                              : Bundler.UnbundleFiles();
    return Bundler.BundleFiles();
  });
}
