//===- ScalarOptionsRegistration.cpp - clv2 option registration ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The single place this library's option registries are instantiated for
// registration.  See ScalarOptionsRegistration.h.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/ScalarOptionsRegistration.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Transforms/Scalar/ScalarOptionsOptInfos.h"

void llvm::registerScalarOptsOptions(llvm::clv2::OptionParser &P) {
  P.add<&clv2::ScalarOptsReg>();
}
