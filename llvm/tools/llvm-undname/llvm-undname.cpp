//===-- llvm-undname.cpp - Microsoft ABI name undecorator
//------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This utility works like the windows undname utility. It converts mangled
// Microsoft symbol names into pretty C/C++ human-readable names.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringRef.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

using namespace llvm;
using namespace llvm::clv2;

static constexpr OptionCategory UndNameCategory{"UndName Options"};

static constexpr OptionInfo<bool> DumpBackReferences{
    "backrefs", "dump backreferences", Hidden, cat(UndNameCategory)};
static constexpr OptionInfo<bool> NoAccessSpecifier{
    "no-access-specifier", "skip access specifiers", Hidden,
    cat(UndNameCategory)};
static constexpr OptionInfo<bool> NoCallingConvention{
    "no-calling-convention", "skip calling convention", Hidden,
    cat(UndNameCategory)};
static constexpr OptionInfo<bool> NoReturnType{
    "no-return-type", "skip return types", Hidden, cat(UndNameCategory)};
static constexpr OptionInfo<bool> NoMemberType{
    "no-member-type", "skip member types", Hidden, cat(UndNameCategory)};
static constexpr OptionInfo<bool> NoVariableType{
    "no-variable-type", "skip variable types", Hidden, cat(UndNameCategory)};
static constexpr OptionInfo<std::string> RawFile{"raw-file", "for fuzzer data",
                                                 Hidden, cat(UndNameCategory)};
static constexpr OptionInfo<bool> WarnTrailing{"warn-trailing",
                                               "warn on trailing characters",
                                               Hidden, cat(UndNameCategory)};
static constexpr ListOptionInfo<std::string> Symbols{
    "symbols", "<input symbols>", Positional{}, ZeroOrMore,
    cat(UndNameCategory)};

static constexpr OptionsRegistry<&DumpBackReferences, &NoAccessSpecifier,
                                 &NoCallingConvention, &NoReturnType,
                                 &NoMemberType, &NoVariableType, &RawFile,
                                 &WarnTrailing, &Symbols>
    UndNameToolReg;

struct UndNameOpts {
  bool DumpBackRefs, NoAccess, NoCallingConv, NoReturn, NoMember, NoVariable;
  bool WarnTrail;
};

static bool msDemangle(const std::string &S, const UndNameOpts &O) {
  int Status;
  MSDemangleFlags Flags = MSDF_None;
  if (O.DumpBackRefs)
    Flags = MSDemangleFlags(Flags | MSDF_DumpBackrefs);
  if (O.NoAccess)
    Flags = MSDemangleFlags(Flags | MSDF_NoAccessSpecifier);
  if (O.NoCallingConv)
    Flags = MSDemangleFlags(Flags | MSDF_NoCallingConvention);
  if (O.NoReturn)
    Flags = MSDemangleFlags(Flags | MSDF_NoReturnType);
  if (O.NoMember)
    Flags = MSDemangleFlags(Flags | MSDF_NoMemberType);
  if (O.NoVariable)
    Flags = MSDemangleFlags(Flags | MSDF_NoVariableType);

  size_t NRead;
  char *ResultBuf = microsoftDemangle(S, &NRead, &Status, Flags);
  if (Status == llvm::demangle_success) {
    outs() << ResultBuf << "\n";
    outs().flush();
    if (O.WarnTrail && NRead < S.size())
      WithColor::warning() << "trailing characters: " << S.c_str() + NRead
                           << "\n";
  } else {
    WithColor::error() << "Invalid mangled name\n";
  }
  std::free(ResultBuf);
  return Status == llvm::demangle_success;
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  clv2::OptionParser P;
  P.add<&UndNameToolReg>();
  RegisterAllLLVMOptions(P);
  P.hideUnrelatedOptions({&UndNameCategory, &getColorCategory()});
  auto OptsCtx = P.parse(argc, argv, "llvm-undname\n");
  auto *Opts = OptsCtx->getViewPtr<&UndNameToolReg>();

  UndNameOpts O{
      Opts->get<&DumpBackReferences>(),  Opts->get<&NoAccessSpecifier>(),
      Opts->get<&NoCallingConvention>(), Opts->get<&NoReturnType>(),
      Opts->get<&NoMemberType>(),        Opts->get<&NoVariableType>(),
      Opts->get<&WarnTrailing>()};

  if (!Opts->get<&RawFile>().empty()) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr =
        MemoryBuffer::getFileOrSTDIN(Opts->get<&RawFile>());
    if (std::error_code EC = FileOrErr.getError()) {
      WithColor::error() << "Could not open input file \'"
                         << Opts->get<&RawFile>() << "\': " << EC.message()
                         << '\n';
      return 1;
    }
    return msDemangle(std::string(FileOrErr->get()->getBuffer()), O) ? 0 : 1;
  }

  bool Success = true;
  const auto &SymList = Opts->get<&Symbols>();
  if (SymList.empty()) {
    while (true) {
      std::string LineStr;
      std::getline(std::cin, LineStr);
      if (std::cin.eof())
        break;

      StringRef Line(LineStr);
      Line = Line.trim();
      if (Line.empty() || Line.starts_with("#") || Line.starts_with(";"))
        continue;

      // If the user is manually typing in these decorated names, don't echo
      // them to the terminal a second time.  If they're coming from redirected
      // input, however, then we should display the input line so that the
      // mangled and demangled name can be easily correlated in the output.
      if (!sys::Process::StandardInIsUserInput()) {
        outs() << Line << "\n";
        outs().flush();
      }
      if (!msDemangle(std::string(Line), O))
        Success = false;
      outs() << "\n";
    }
  } else {
    for (StringRef S : SymList) {
      outs() << S << "\n";
      outs().flush();
      if (!msDemangle(std::string(S), O))
        Success = false;
      outs() << "\n";
    }
  }

  return Success ? 0 : 1;
}
