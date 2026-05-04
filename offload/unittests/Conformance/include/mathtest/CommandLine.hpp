//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the definition of TestConfigsArg for command-line
/// test configuration selection.
///
//===----------------------------------------------------------------------===//

#ifndef MATHTEST_COMMANDLINE_HPP
#define MATHTEST_COMMANDLINE_HPP

#include "mathtest/TestConfig.hpp"

#include "llvm/ADT/SmallVector.h"

namespace mathtest {

struct TestConfigsArg {
  enum class Mode { Default, All, Explicit } Mode = Mode::Default;
  llvm::SmallVector<TestConfig, 4> Explicit;
};

} // namespace mathtest

#endif // MATHTEST_COMMANDLINE_HPP
