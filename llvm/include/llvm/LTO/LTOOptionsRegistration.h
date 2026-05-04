//===- LTOOptionsRegistration.h - clv2 option registration ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Registration entry points for this library's clv2 option registries.
//
// Naming a registry to register it would mean including the generated
// <X>OptionsOptInfos.h, so the translation units that register everything would
// instantiate every registry in the build -- a large amount of compiler memory
// each.  Declaring a function instead keeps each registry's machinery in the
// library that owns it, where it is instantiated once.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LTO_LTOOPTIONSREGISTRATION_H
#define LLVM_LTO_LTOOPTIONSREGISTRATION_H

namespace llvm {
namespace clv2 {
class OptionParser;
} // namespace clv2

void registerLTOOptsOptions(clv2::OptionParser &P);

} // namespace llvm

#endif // LLVM_LTO_LTOOPTIONSREGISTRATION_H
