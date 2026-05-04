//===- CodeGenOptionsRegistration.h - clv2 option registration ---*- C++
//-*-===//
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

#ifndef LLVM_CODEGEN_CODEGENOPTIONSREGISTRATION_H
#define LLVM_CODEGEN_CODEGENOPTIONSREGISTRATION_H

namespace llvm {
namespace clv2 {
class OptionParser;
} // namespace clv2

void registerCGOptsOptions(clv2::OptionParser &P);
void registerCGPassAsmPrintOptions(clv2::OptionParser &P);
void registerCGPassCore1Options(clv2::OptionParser &P);
void registerCGPassCore2Options(clv2::OptionParser &P);
void registerCGPassGISelOptions(clv2::OptionParser &P);
void registerCGPassMachine1Options(clv2::OptionParser &P);
void registerCGPassMachine2Options(clv2::OptionParser &P);
void registerCGPassAllocOptions(clv2::OptionParser &P);
void registerCGPassSched1Options(clv2::OptionParser &P);
void registerCGPassSched2Options(clv2::OptionParser &P);
void registerCGPassSelDAGOptions(clv2::OptionParser &P);

} // namespace llvm

#endif // LLVM_CODEGEN_CODEGENOPTIONSREGISTRATION_H
