//===- CommonLinkerContext.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Entry point for all global state in lldCommon. The objective is for LLD to be
// used "as a library" in a thread-safe manner.
//
// Instead of program-wide globals or function-local statics, we prefer
// aggregating all "global" states into a heap-based structure
// (CommonLinkerContext). This also achieves deterministic initialization &
// shutdown for all "global" states.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COMMON_COMMONLINKINGCONTEXT_H
#define LLD_COMMON_COMMONLINKINGCONTEXT_H

#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Memory.h"
#include "llvm/Support/StringSaver.h"

namespace llvm {
class raw_ostream;
} // namespace llvm

namespace lld {
struct SpecificAllocBase;
class CommonLinkerContext {
public:
  // A context is reached by reference only, so it can coexist with other links
  // running on other threads.
  CommonLinkerContext();
  virtual ~CommonLinkerContext();

  // Deprecated, and does nothing. Each driver owns its context and deletes it
  // before lldMain() returns, unless a fatal error unwound the link.
  static void destroy();

  // Creates new instances of T off a (almost) contiguous arena/object pool
  // owned by this context. The instances are destroyed with the context.
  template <typename T, typename... U> T *make(U &&...args) {
    return new (getSpecificAllocSingleton<T>(*this).Allocate())
        T(std::forward<U>(args)...);
  }

  llvm::BumpPtrAllocator bAlloc;
  llvm::StringSaver saver{bAlloc};
  llvm::UniqueStringSaver uniqueSaver{bAlloc};
  llvm::DenseMap<void *, SpecificAllocBase *> instances;

  ErrorHandler e;
};
} // namespace lld

#endif
