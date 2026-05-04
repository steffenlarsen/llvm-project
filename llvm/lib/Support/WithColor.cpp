//===- WithColor.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/WithColor.h"

#include "DebugOptions.h"

#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/SupportOptions.h"
#include "llvm/Support/SupportOptionsOptInfos.h"

using namespace llvm;

cl::OptionCategory &llvm::getColorCategory() {
  return const_cast<cl::OptionCategory &>(clv2::ColorOptionsCategory);
}

cl::boolOrDefault UseColorVal = cl::boolOrDefault::BOU_UNSET;

static constexpr clv2::OptionsRegistry<&clv2::SUP_Color> ColorOptsReg;
static void applyColorOpts(const decltype(ColorOptsReg)::ParsedOptionsT &Opts) {
  UseColorVal = Opts.get<&clv2::SUP_Color>();
}

static cl::boolOrDefault getUseColor() { return UseColorVal; }

// NOTE: --color is intentionally mirrored into a process-wide global rather
// than read from an OptionsContext at each use.  WithColor's API is built
// around a bare raw_ostream with no context in scope, and colorization is a
// property of the shared output stream: two concurrent in-process jobs writing
// to the same terminal cannot meaningfully disagree about it.  Threading a
// context here would mean changing every WithColor call site for no behavioural
// gain.  See also the ORC debug-print filters in ExecutionEngine/Orc/
// DebugUtils.cpp, which are global for the same reason.
void llvm::initWithColorOptions() {
  // runParser calls this on every parse, including from several threads at
  // once.  A function-local static gives thread-safe exactly-once
  // initialisation: concurrent callers block until the first has finished, so
  // the registration is published before any of them go on to read the
  // registration list.  A plain `static bool Registered` would let two threads
  // both push into that list — and the second push can reallocate it while the
  // first thread is already iterating it.
  static const int Registered = [] {
    clv2::registerEssentialDynamicRegistry<&ColorOptsReg>(applyColorOpts);
    return 0;
  }();
  (void)Registered;
}

static bool DefaultAutoDetectFunction(const raw_ostream &OS) {
  cl::boolOrDefault Val = getUseColor();
  return Val == cl::boolOrDefault::BOU_UNSET
             ? OS.has_colors()
             : Val == cl::boolOrDefault::BOU_TRUE;
}

WithColor::AutoDetectFunctionType WithColor::AutoDetectFunction =
    DefaultAutoDetectFunction;

WithColor::WithColor(raw_ostream &OS, HighlightColor Color, ColorMode Mode)
    : OS(OS), Mode(Mode) {
  // Detect color from terminal type unless the user passed the --color option.
  if (colorsEnabled()) {
    switch (Color) {
    case HighlightColor::Address:
      OS.changeColor(raw_ostream::YELLOW);
      break;
    case HighlightColor::String:
      OS.changeColor(raw_ostream::GREEN);
      break;
    case HighlightColor::Tag:
      OS.changeColor(raw_ostream::BLUE);
      break;
    case HighlightColor::Attribute:
      OS.changeColor(raw_ostream::CYAN);
      break;
    case HighlightColor::Enumerator:
      OS.changeColor(raw_ostream::MAGENTA);
      break;
    case HighlightColor::Macro:
      OS.changeColor(raw_ostream::RED);
      break;
    case HighlightColor::Error:
      OS.changeColor(raw_ostream::RED, true);
      break;
    case HighlightColor::Warning:
      OS.changeColor(raw_ostream::MAGENTA, true);
      break;
    case HighlightColor::Note:
      OS.changeColor(raw_ostream::BLACK, true);
      break;
    case HighlightColor::Remark:
      OS.changeColor(raw_ostream::BLUE, true);
      break;
    }
  }
}

raw_ostream &WithColor::error() { return error(errs()); }

raw_ostream &WithColor::warning() { return warning(errs()); }

raw_ostream &WithColor::note() { return note(errs()); }

raw_ostream &WithColor::remark() { return remark(errs()); }

raw_ostream &WithColor::error(raw_ostream &OS, StringRef Prefix,
                              bool DisableColors) {
  if (!Prefix.empty())
    OS << Prefix << ": ";
  return WithColor(OS, HighlightColor::Error,
                   DisableColors ? ColorMode::Disable : ColorMode::Auto)
             .get()
         << "error: ";
}

raw_ostream &WithColor::warning(raw_ostream &OS, StringRef Prefix,
                                bool DisableColors) {
  if (!Prefix.empty())
    OS << Prefix << ": ";
  return WithColor(OS, HighlightColor::Warning,
                   DisableColors ? ColorMode::Disable : ColorMode::Auto)
             .get()
         << "warning: ";
}

raw_ostream &WithColor::note(raw_ostream &OS, StringRef Prefix,
                             bool DisableColors) {
  if (!Prefix.empty())
    OS << Prefix << ": ";
  return WithColor(OS, HighlightColor::Note,
                   DisableColors ? ColorMode::Disable : ColorMode::Auto)
             .get()
         << "note: ";
}

raw_ostream &WithColor::remark(raw_ostream &OS, StringRef Prefix,
                               bool DisableColors) {
  if (!Prefix.empty())
    OS << Prefix << ": ";
  return WithColor(OS, HighlightColor::Remark,
                   DisableColors ? ColorMode::Disable : ColorMode::Auto)
             .get()
         << "remark: ";
}

bool WithColor::colorsEnabled() {
  switch (Mode) {
  case ColorMode::Enable:
    return true;
  case ColorMode::Disable:
    return false;
  case ColorMode::Auto:
    return AutoDetectFunction(OS);
  }
  llvm_unreachable("All cases handled above.");
}

WithColor &WithColor::changeColor(raw_ostream::Colors Color, bool Bold,
                                  bool BG) {
  if (colorsEnabled())
    OS.changeColor(Color, Bold, BG);
  return *this;
}

WithColor &WithColor::resetColor() {
  if (colorsEnabled())
    OS.resetColor();
  return *this;
}

WithColor::~WithColor() { resetColor(); }

void WithColor::defaultErrorHandler(Error Err) {
  handleAllErrors(std::move(Err), [](ErrorInfoBase &Info) {
    WithColor::error() << Info.message() << '\n';
  });
}

void WithColor::defaultWarningHandler(Error Warning) {
  handleAllErrors(std::move(Warning), [](ErrorInfoBase &Info) {
    WithColor::warning() << Info.message() << '\n';
  });
}

WithColor::AutoDetectFunctionType WithColor::defaultAutoDetectFunction() {
  return DefaultAutoDetectFunction;
}

void WithColor::setAutoDetectFunction(
    AutoDetectFunctionType NewAutoDetectFunction) {
  AutoDetectFunction = NewAutoDetectFunction;
}
