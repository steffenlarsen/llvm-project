//===- xray-stacks.h - XRay Call Stack Accounting Enums -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef TOOLS_LLVM_XRAY_XRAY_STACKS_H
#define TOOLS_LLVM_XRAY_XRAY_STACKS_H

enum StackOutputFormat { HUMAN, FLAMETOOL };

enum class AggregationType { TOTAL_TIME, INVOCATION_COUNT };

#endif // TOOLS_LLVM_XRAY_XRAY_STACKS_H
