//===- InstCombineOptionsRegistration.cpp - clv2 option registration
//----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The single place this library's option registries are instantiated for
// registration.  See InstCombineOptionsRegistration.h.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/InstCombine/InstCombineOptionsRegistration.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Transforms/InstCombine/InstCombineOptionsOptInfos.h"

void llvm::registerInstCombineOptsOptions(llvm::clv2::OptionParser &P) {
  P.add<&clv2::InstCombineOptsReg>();
}
