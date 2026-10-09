//===- MultiTargetASTExtension.h - Multi-target state in AST files -*- C++ -*-//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A host-primary multi-target compilation keeps part of what it knows about
// the AST outside of it: which target each declaration belongs to, which
// target a specialization was instantiated for, and the like. This module
// file extension carries that state, so that one precompiled header can serve
// every target of such a compilation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SERIALIZATION_MULTITARGETASTEXTENSION_H
#define LLVM_CLANG_SERIALIZATION_MULTITARGETASTEXTENSION_H

#include "clang/Basic/LLVM.h"
#include "clang/Serialization/ModuleFileExtension.h"
#include "llvm/ADT/SmallVector.h"
#include <optional>
#include <string>
#include <vector>

namespace clang {

class MultiTargetASTExtension
    : public llvm::RTTIExtends<MultiTargetASTExtension, ModuleFileExtension> {
public:
  static char ID;

  /// A target of the compilation besides the primary one.
  struct Target {
    std::string Triple;
    std::string CPU;
    std::vector<std::string> Features;
    /// A hash of the macros the target predefines, built in or from the
    /// command line. Its state at the end of a precompiled prefix grows from
    /// them, so only a target that starts out with the same ones can resume
    /// from it.
    uint64_t Predefines = 0;

    bool operator==(const Target &Other) const {
      return Triple == Other.Triple && CPU == Other.CPU &&
             Features == Other.Features && Predefines == Other.Predefines;
    }
  };

  /// What an aux target's preprocessor knows at the end of a precompiled
  /// prefix. A file only records the primary target's preprocessor, so the
  /// others are restored from this instead of preprocessing the prefix again.
  struct PrefixState {
    /// Every macro defined at the end of the prefix, in -D form:
    /// "NAME=body" or "NAME(params)=body".
    std::vector<std::string> Macros;
    /// The files the prefix included that later #includes must skip.
    std::vector<std::string> IncludeOnceFiles;
    /// The include guard of each file the prefix included that has one, as
    /// path and macro name in turn. A later #include does not enter such a
    /// file at all while its guard is defined.
    std::vector<std::string> ControllingMacros;
  };

  /// The headers a prefix consists of, and each aux target's state at its end.
  struct Prefix {
    /// The force-included headers, as spelled, and the precompiled header
    /// itself, as an absolute path.
    std::vector<std::string> Headers;
    /// One per aux target, in getAuxTargets() order.
    std::vector<PrefixState> States;
  };

  explicit MultiTargetASTExtension(std::vector<Target> AuxTargets);
  ~MultiTargetASTExtension() override;

  const std::vector<Target> &getAuxTargets() const { return AuxTargets; }

  /// The prefix to write into the precompiled file being generated.
  void setPrefixToWrite(Prefix P) { PrefixToWrite = std::move(P); }

  /// The prefix of the precompiled file this compilation loaded, if any.
  const Prefix *getLoadedPrefix() const {
    return HasLoadedPrefix ? &LoadedPrefix : nullptr;
  }

  /// For each of this compilation's aux targets, the variant of the one with
  /// the same identity among the targets a file was written for, or nothing if
  /// the file cannot serve them. A file written for more targets serves a
  /// subset of them that keeps its canonical (first) device, or a single
  /// device.
  std::optional<SmallVector<unsigned, 4>>
  mapTargets(const ModuleFileExtensionMetadata &Metadata) const;

  ModuleFileExtensionMetadata getExtensionMetadata() const override;
  void hashExtension(ExtensionHashBuilder &HBuilder) const override;
  std::unique_ptr<ModuleFileExtensionWriter>
  createExtensionWriter(ASTWriter &Writer) override;
  std::unique_ptr<ModuleFileExtensionReader>
  createExtensionReader(const ModuleFileExtensionMetadata &Metadata,
                        ASTReader &Reader, serialization::ModuleFile &Mod,
                        const llvm::BitstreamCursor &Stream) override;

private:
  class Writer;
  class Reader;

  /// The aux targets, as the metadata's user information, and back.
  std::string describeTargets() const;
  static std::vector<Target> parseTargets(llvm::StringRef UserInfo);

  std::vector<Target> AuxTargets;
  Prefix PrefixToWrite;
  Prefix LoadedPrefix;
  bool HasLoadedPrefix = false;
};

} // namespace clang

#endif // LLVM_CLANG_SERIALIZATION_MULTITARGETASTEXTENSION_H
