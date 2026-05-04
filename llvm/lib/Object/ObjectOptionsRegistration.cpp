//===- ObjectOptionsRegistration.cpp - clv2 option registration ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The single place this library's option registries are instantiated for
// registration.  See ObjectOptionsRegistration.h.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/ObjectOptionsRegistration.h"
#include "llvm/Object/ObjectOptionsOptInfos.h"
#include "llvm/Support/CommandLineV2.h"

void llvm::registerObjectOptsOptions(llvm::clv2::OptionParser &P) {
  P.add<&clv2::ObjectOptsReg>();
}
