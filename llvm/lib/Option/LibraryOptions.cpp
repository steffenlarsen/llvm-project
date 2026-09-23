//===- LibraryOptions.cpp - Per-library TableGen options struct ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Option/LibraryOptions.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/TypeSize.h"
#include <atomic>

using namespace llvm;
using namespace llvm::opt;

bool llvm::opt::parseFieldValue(StringRef Arg, std::string &Out) {
  Out = Arg.str();
  return true;
}

bool llvm::opt::parseFieldValue(StringRef Arg, bool &Out) {
  if (Arg == "true" || Arg == "1") {
    Out = true;
    return true;
  }
  if (Arg == "false" || Arg == "0") {
    Out = false;
    return true;
  }
  return false;
}

bool llvm::opt::parseFieldValue(StringRef Arg, int &Out) {
  return !Arg.getAsInteger(0, Out);
}

bool llvm::opt::parseFieldValue(StringRef Arg, unsigned &Out) {
  return !Arg.getAsInteger(0, Out);
}

bool llvm::opt::parseFieldValue(StringRef Arg, uint64_t &Out) {
  return !Arg.getAsInteger(0, Out);
}

bool llvm::opt::parseFieldValue(StringRef Arg, int64_t &Out) {
  return !Arg.getAsInteger(0, Out);
}

bool llvm::opt::parseFieldValue(StringRef Arg, float &Out) {
  return llvm::to_float(Arg, Out);
}

bool llvm::opt::parseFieldValue(StringRef Arg, double &Out) {
  return llvm::to_float(Arg, Out);
}

bool llvm::opt::parseFieldValue(StringRef Arg, ElementCount &Out) {
  StringRef Val = Arg.trim();
  unsigned MinValue;
  if (!Val.getAsInteger(0, MinValue)) {
    Out = ElementCount::getFixed(MinValue);
    return true;
  }
  StringRef Remainder = Val;
  if (!Remainder.consume_front("vscale"))
    return false;
  Remainder = Remainder.ltrim();
  if (!Remainder.consume_front("x"))
    return false;
  Remainder = Remainder.ltrim();
  if (Remainder.getAsInteger(0, MinValue))
    return false;
  Out = ElementCount::getScalable(MinValue);
  return true;
}

unsigned llvm::opt::allocateOptionsSlot() {
  static std::atomic<unsigned> NextSlot{0};
  return NextSlot.fetch_add(1, std::memory_order_relaxed);
}
