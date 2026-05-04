//===- OpenMPOptions.td - OpenMP target options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def OMPOptsReg : Registry<"llvm::omp_opts"> {
  // Getter bodies are compiled once, in OpenMPOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

def OMP_OptimisticAttributes : Option<
    "openmp-ir-builder-optimistic-attributes",
    "Use optimistic attributes describing 'as-if' properties of runtime calls.",
    "bool"> {
  let Registry = OMPOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def OMP_UnrollThresholdFactor : Option<
    "openmp-ir-builder-unroll-threshold-factor",
    "Factor for the unroll threshold to account for code simplifications",
    "double"> {
  let Registry = OMPOptsReg;
  let Default = "1.5";
  let Hidden = 1;
}

def OMP_UseDefaultMaxThreads : Option<
    "openmp-ir-builder-use-default-max-threads",
    "Use a default max threads if none is provided.",
    "bool"> {
  let Registry = OMPOptsReg;
  let Default = "1";
  let Hidden = 1;
}
