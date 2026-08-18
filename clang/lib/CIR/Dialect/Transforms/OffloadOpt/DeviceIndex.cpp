//===- DeviceIndex.cpp - Recognise device index reads ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/Transforms/OffloadOpt/DeviceIndex.h"
#include "clang/CIR/Dialect/Transforms/CIRTransformUtils.h"

using namespace cir;

llvm::StringRef cir::getDeviceIndexKindName(DeviceIndexKind kind) {
  switch (kind) {
  case DeviceIndexKind::BlockId:
    return "blockIdx";
  case DeviceIndexKind::ThreadId:
    return "threadIdx";
  case DeviceIndexKind::BlockDim:
    return "blockDim";
  case DeviceIndexKind::GridDim:
    return "gridDim";
  }
  return "<unknown>";
}

// x/y/z suffix -> dimension index.
static std::optional<unsigned> dimFromChar(char c) {
  switch (c) {
  case 'x':
    return 0;
  case 'y':
    return 1;
  case 'z':
    return 2;
  default:
    return std::nullopt;
  }
}

static std::optional<DeviceIndexKind> kindFromTypeName(llvm::StringRef name) {
  // The record names are `blockIdx_t`, `threadIdx_t`, `blockDim_t`,
  // `gridDim_t`. Ordered so no name is a prefix of an earlier match.
  if (name.starts_with("blockIdx"))
    return DeviceIndexKind::BlockId;
  if (name.starts_with("threadIdx"))
    return DeviceIndexKind::ThreadId;
  if (name.starts_with("blockDim"))
    return DeviceIndexKind::BlockDim;
  if (name.starts_with("gridDim"))
    return DeviceIndexKind::GridDim;
  return std::nullopt;
}

// Level 1: the accessor the HIP/CUDA headers define, matched on the two stable
// substrings rather than the whole mangled name.
static std::optional<DeviceIndex> matchAccessor(llvm::StringRef callee) {
  static constexpr llvm::StringRef typeMarkers[] = {"__hip_builtin_",
                                                    "__cuda_builtin_"};
  static constexpr llvm::StringRef getMarkers[] = {"__get_",
                                                   "__fetch_builtin_"};
  for (llvm::StringRef typeMarker : typeMarkers) {
    size_t typePos = callee.find(typeMarker);
    if (typePos == llvm::StringRef::npos)
      continue;
    std::optional<DeviceIndexKind> kind =
        kindFromTypeName(callee.substr(typePos + typeMarker.size()));
    if (!kind)
      continue;
    for (llvm::StringRef getMarker : getMarkers) {
      size_t getPos = callee.find(getMarker);
      if (getPos == llvm::StringRef::npos)
        continue;
      llvm::StringRef rest = callee.substr(getPos + getMarker.size());
      if (rest.empty())
        continue;
      if (std::optional<unsigned> dim = dimFromChar(rest.front()))
        return DeviceIndex{*kind, *dim};
    }
  }
  return std::nullopt;
}

// Level 2: the ROCm device-library entry point the accessor inlines to, where
// the dimension is a constant argument rather than part of the name.
static std::optional<DeviceIndexKind> kindFromOckl(llvm::StringRef callee) {
  if (callee == "__ockl_get_group_id")
    return DeviceIndexKind::BlockId;
  if (callee == "__ockl_get_local_id")
    return DeviceIndexKind::ThreadId;
  if (callee == "__ockl_get_local_size")
    return DeviceIndexKind::BlockDim;
  if (callee == "__ockl_get_num_groups")
    return DeviceIndexKind::GridDim;
  return std::nullopt;
}

std::optional<DeviceIndex> cir::matchDeviceIndex(mlir::Operation *op) {
  auto call = mlir::dyn_cast_or_null<cir::CallOp>(op);
  if (!call)
    return std::nullopt;
  std::optional<llvm::StringRef> callee = call.getCallee();
  if (!callee)
    return std::nullopt;

  if (std::optional<DeviceIndex> index = matchAccessor(*callee))
    return index;

  std::optional<DeviceIndexKind> kind = kindFromOckl(*callee);
  if (!kind)
    return std::nullopt;
  // A non-constant dimension is a read of *some* component, but not one this
  // can name; reporting a guess would be worse than reporting nothing.
  mlir::OperandRange args = call.getArgOperands();
  if (args.size() != 1)
    return std::nullopt;
  std::optional<int64_t> dim = cir::tryResolveToConstant(args[0]);
  if (!dim || *dim < 0 || *dim > 2)
    return std::nullopt;
  return DeviceIndex{*kind, static_cast<unsigned>(*dim)};
}
