//===- UtilsOptions.cpp - Option bridge for Transforms/Utils --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/UtilsOptionsOptInfos.h"
#include <cstdlib>

using namespace llvm;
using namespace llvm::clv2;
