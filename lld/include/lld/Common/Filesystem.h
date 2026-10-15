//===- Filesystem.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_FILESYSTEM_H
#define LLD_FILESYSTEM_H

#include "lld/Common/LLVM.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <system_error>

namespace lld {
class ErrorHandler;

void unlinkAsync(StringRef path);
std::error_code tryCreateFile(StringRef path);
std::unique_ptr<llvm::raw_fd_ostream> openFile(ErrorHandler &eh,
                                               StringRef file);
std::unique_ptr<llvm::raw_fd_ostream> openLTOOutputFile(ErrorHandler &eh,
                                                        StringRef file);
} // namespace lld

#endif
