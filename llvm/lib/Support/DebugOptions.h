//===-- DebugOptions.h - Global Command line opt for libSupport  *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the entry point to initialize the options registered on the
// command line for libSupport, this is internal to libSupport.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_DEBUGOPTIONS_H
#define LLVM_SUPPORT_DEBUGOPTIONS_H

#include "llvm/ADT/StringRef.h"

namespace llvm {

// These are invoked internally before parsing command line options.
// This enables lazy-initialization of all the globals in libSupport, instead
// of eagerly loading everything on program startup.
void initDebugCounterOptions();
void initGraphWriterOptions();
void initSignalsOptions();
void initStatisticOptions();
void initTimerOptions();
void initWithColorOptions();
void initDebugOptions();
void initRandomSeedOptions();

// Setters for values that used to live in namespace-scope `support::` globals.
// The storage belongs to the ManagedStatic singleton that consumes it (e.g.
// TimerGlobals), which is where it lived before the clv2 migration -- keeping a
// separate global for the option value would add process-wide state on top of a
// singleton that already exists.  Called from applySupportOptions(), and only
// for options actually present on the command line, so an unused facility is
// still never constructed.
void setInfoOutputFilename(StringRef F);
void setTrackSpace(bool V);
void setSortTimers(bool V);

} // namespace llvm

#endif // LLVM_SUPPORT_DEBUGOPTIONS_H
