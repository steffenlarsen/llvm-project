//===- GenInfo.cpp - Generator info -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/TableGen/GenInfo.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/ManagedStatic.h"

using namespace mlir;

static llvm::ManagedStatic<std::vector<GenInfo>> generatorRegistry;

GenRegistration::GenRegistration(StringRef arg, StringRef description,
                                 const GenFunction &function) {
  generatorRegistry->emplace_back(arg, description, function);
}

ArrayRef<GenInfo> mlir::getRegisteredGenerators() { return *generatorRegistry; }
