//===- yaml2obj - Convert YAML to a binary object file --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This program takes a YAML description of an object file and outputs the
// binary equivalent.
//
// This is used for writing tests that require binary files.
//
//===----------------------------------------------------------------------===//

#include "llvm/ObjectYAML/yaml2obj.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ObjectYAML/ObjectYAML.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>
#include <system_error>

using namespace llvm;
using namespace llvm::clv2;

static constexpr OptionCategory Cat{"yaml2obj Options"};

static constexpr OptionInfo<std::string> Input{
    "input", "<input file>", Positional{}, Init{"-"}, cat(Cat)};

static constexpr ListOptionInfo<std::string> D{
    "D",
    "Defined the specified macros to their specified "
    "definition. The syntax is <macro>=<definition>",
    PrefixFormat, cat(Cat)};

static constexpr OptionInfo<bool> PreprocessOnly{
    "E", "Just print the preprocessed file", cat(Cat)};

static constexpr OptionInfo<unsigned> DocNum{
    "docnum", "Read specified document from input (default = 1)", Init{1u},
    cat(Cat)};

static constexpr OptionInfo<uint64_t> MaxSize{
    "max-size",
    "Sets the maximum allowed output size (0 means no limit) [ELF and COFF "
    "only]",
    Init{uint64_t(10 * 1024 * 1024)}, cat(Cat)};

static constexpr OptionInfo<std::string> OutputFilename{
    "o",       "Output filename", value_desc("filename"),
    Init{"-"}, PrefixFormat,      cat(Cat)};

static constexpr OptionsRegistry<&Input, &D, &PreprocessOnly, &DocNum, &MaxSize,
                                 &OutputFilename>
    Yaml2ObjToolReg;

static std::optional<std::string>
preprocess(StringRef Buf, const std::vector<std::string> &Defines,
           yaml::ErrorHandler ErrHandler) {
  DenseMap<StringRef, StringRef> DefinesMap;
  for (StringRef Define : Defines) {
    StringRef Macro, Definition;
    std::tie(Macro, Definition) = Define.split('=');
    if (!Define.count('=') || Macro.empty()) {
      ErrHandler("invalid syntax for -D: " + Define);
      return {};
    }
    if (!DefinesMap.try_emplace(Macro, Definition).second) {
      ErrHandler("'" + Macro + "'" + " redefined");
      return {};
    }
  }

  std::string Preprocessed;
  while (!Buf.empty()) {
    if (Buf.starts_with("[[")) {
      size_t I = Buf.find_first_of("[]", 2);
      if (Buf.substr(I).starts_with("]]")) {
        StringRef MacroExpr = Buf.substr(2, I - 2);
        StringRef Macro;
        StringRef Default;
        std::tie(Macro, Default) = MacroExpr.split('=');

        // When the -D option is requested, we use the provided value.
        // Otherwise we use a default macro value if present.
        auto It = DefinesMap.find(Macro);
        std::optional<StringRef> Value;
        if (It != DefinesMap.end())
          Value = It->second;
        else if (!Default.empty() || MacroExpr.ends_with("="))
          Value = Default;

        if (Value) {
          Preprocessed += *Value;
          Buf = Buf.substr(I + 2);
          continue;
        }
      }
    }

    Preprocessed += Buf[0];
    Buf = Buf.substr(1);
  }

  return Preprocessed;
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  clv2::OptionParser P;
  P.add<&Yaml2ObjToolReg>();
  RegisterAllLLVMOptions(P);
  P.hideUnrelatedOptions({&Cat});
  auto OptsCtx =
      P.parse(argc, argv, "Create an object file from a YAML description");
  auto *Opts = OptsCtx->getViewPtr<&Yaml2ObjToolReg>();

  constexpr StringRef ProgName = "yaml2obj";
  auto ErrHandler = [&](const Twine &Msg) {
    WithColor::error(errs(), ProgName) << Msg << "\n";
  };

  const std::string &OutFile = Opts->get<&OutputFilename>();
  std::error_code EC;
  std::unique_ptr<ToolOutputFile> Out(
      new ToolOutputFile(OutFile, EC, sys::fs::OF_None));
  if (EC) {
    ErrHandler("failed to open '" + OutFile + "': " + EC.message());
    return 1;
  }

  const std::string &InFile = Opts->get<&Input>();
  ErrorOr<std::unique_ptr<MemoryBuffer>> Buf =
      MemoryBuffer::getFileOrSTDIN(InFile, /*IsText=*/true);
  if (std::error_code EC = Buf.getError()) {
    WithColor::error(errs(), ProgName)
        << InFile << ": " << EC.message() << '\n';
    return 1;
  }

  std::optional<std::string> Buffer =
      preprocess(Buf.get()->getBuffer(), Opts->get<&D>(), ErrHandler);
  if (!Buffer)
    return 1;

  uint64_t MaxSzVal = Opts->get<&MaxSize>();
  if (Opts->get<&PreprocessOnly>()) {
    Out->os() << Buffer;
  } else {
    yaml::Input YIn(*Buffer);

    if (!convertYAML(YIn, Out->os(), ErrHandler, Opts->get<&DocNum>(),
                     MaxSzVal == 0 ? UINT64_MAX : MaxSzVal))
      return 1;
  }

  Out->keep();
  Out->os().flush();
  return 0;
}
