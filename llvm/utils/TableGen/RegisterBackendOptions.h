//===- RegisterBackendOptions.h - Register all backend options --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares per-backend option registration functions and a single entry point
// that calls all of them.  These replace the former static-struct global
// constructors that registered runtime options.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_REGISTERBACKENDOPTIONS_H
#define LLVM_UTILS_TABLEGEN_REGISTERBACKENDOPTIONS_H

namespace llvm {
namespace clv2 {
class OptionParser;
}
} // namespace llvm

void registerSDNodeInfoEmitterOptions(llvm::clv2::OptionParser &P);
void registerRegisterInfoEmitterOptions(llvm::clv2::OptionParser &P);
void registerDecoderEmitterOptions(llvm::clv2::OptionParser &P);
void registerGlobalISelCombinerEmitterOptions(llvm::clv2::OptionParser &P);
void registerDAGISelMatcherEmitterOptions(llvm::clv2::OptionParser &P);
void registerInstrInfoEmitterOptions(llvm::clv2::OptionParser &P);
void registerAsmMatcherEmitterOptions(llvm::clv2::OptionParser &P);
void registerGlobalISelEmitterOptions(llvm::clv2::OptionParser &P);
void registerCodeGenTargetOptions(llvm::clv2::OptionParser &P);
void registerGlobalISelMatchTableExecutorEmitterOptions(
    llvm::clv2::OptionParser &P);

inline void registerAllLLVMTblgenBackendOptions(llvm::clv2::OptionParser &P) {
  registerSDNodeInfoEmitterOptions(P);
  registerRegisterInfoEmitterOptions(P);
  registerDecoderEmitterOptions(P);
  registerGlobalISelCombinerEmitterOptions(P);
  registerDAGISelMatcherEmitterOptions(P);
  registerInstrInfoEmitterOptions(P);
  registerAsmMatcherEmitterOptions(P);
  registerGlobalISelEmitterOptions(P);
  registerCodeGenTargetOptions(P);
  registerGlobalISelMatchTableExecutorEmitterOptions(P);
}

#endif // LLVM_UTILS_TABLEGEN_REGISTERBACKENDOPTIONS_H
