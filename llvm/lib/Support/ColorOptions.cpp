//===- ColorOptions.cpp - `--color` output option ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/ColorOptions.h"
#include "llvm/ADT/StringRef.h"

using namespace llvm;

ColorOptions ColorOptions::Current;

Error ColorOptions::parse(ArrayRef<const char *> Args,
                           SmallVectorImpl<const char *> &Rest,
                           raw_ostream &Errs) {
  for (const char *Arg : Args) {
    StringRef A(Arg);
    StringRef Val;
    bool HasVal = false;
    if (!(A == "-color" || A == "--color")) {
      if (A.consume_front("-color=") || A.consume_front("--color=")) {
        Val = A;
        HasVal = true;
      } else {
        Rest.push_back(Arg);
        continue;
      }
    }

    if (!HasVal || Val.equals_insensitive("true") ||
        Val.equals_insensitive("1") || Val.equals_insensitive("yes") ||
        Val.equals_insensitive("on")) {
      Color = cl::boolOrDefault::BOU_TRUE;
      continue;
    }
    if (Val.equals_insensitive("false") || Val.equals_insensitive("0") ||
        Val.equals_insensitive("no") || Val.equals_insensitive("off")) {
      Color = cl::boolOrDefault::BOU_FALSE;
      continue;
    }
    Errs << "for the --color option: '" << Val
         << "' is invalid value for boolean argument! Try "
            "true/false/1/0/yes/no/on/off\n";
    return createStringError(inconvertibleErrorCode(),
                              "invalid value for --color option");
  }
  return Error::success();
}
