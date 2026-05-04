//===- DirectXOptions.h - DirectX command-line options ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Options shared by the DXIL writer and the DXContainer globals pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_DIRECTX_DIRECTXOPTIONS_H
#define LLVM_LIB_TARGET_DIRECTX_DIRECTXOPTIONS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/OptionsContext.h"

#include <string>

namespace llvm {
namespace dxil {

inline constexpr clv2::OptionInfo<std::string> OI_PdbDebugPath{
    "dx-pdb-path",
    "Write debug information to the given file, or automatically named file in "
    "directory when ending in '/'",
    clv2::value_desc("filename")};

inline constexpr clv2::OptionInfo<bool> OI_SourceInDebugModule{
    "dx-source-in-debug-module",
    "Embed source code into debug module on DirectX target"};

inline constexpr clv2::OptionInfo<bool> OI_ShaderHashDependsOnSource{
    "dx-Zss", "Compute Shader Hash considering source information"};

inline constexpr clv2::OptionInfo<bool> OI_PdbInPrivate{
    "dx-pdb-in-private", "Store PDB in private user data"};

inline constexpr clv2::OptionsRegistry<
    &OI_PdbDebugPath, &OI_SourceInDebugModule, &OI_ShaderHashDependsOnSource,
    &OI_PdbInPrivate>
    DirectXOptsReg;

inline std::string getPdbDebugPath(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOr<&DirectXOptsReg, &OI_PdbDebugPath>(Ctx,
                                                              std::string());
}

inline bool getSourceInDebugModule(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOr<&DirectXOptsReg, &OI_SourceInDebugModule>(Ctx,
                                                                     false);
}

inline bool getShaderHashDependsOnSource(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOr<&DirectXOptsReg, &OI_ShaderHashDependsOnSource>(
      Ctx, false);
}

inline bool getPdbInPrivate(const clv2::OptionsContext &Ctx) {
  return clv2::getOptValOr<&DirectXOptsReg, &OI_PdbInPrivate>(Ctx, false);
}

} // namespace dxil
} // namespace llvm

#endif // LLVM_LIB_TARGET_DIRECTX_DIRECTXOPTIONS_H
