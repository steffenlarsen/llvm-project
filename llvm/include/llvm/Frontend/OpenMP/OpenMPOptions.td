//===- OpenMPOptions.td - OpenMP target options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def OpenMPOptions : OptionsStruct<"llvm"> {}

defm OMP_OptimisticAttributes : BoolField<
    "openmp-ir-builder-optimistic-attributes", "false",
    "Use optimistic attributes describing 'as-if' properties of runtime calls.",
    /*hidden=*/1>;

defm OMP_UnrollThresholdFactor : ValueField<
    "openmp-ir-builder-unroll-threshold-factor", "double", "1.5",
    "Factor for the unroll threshold to account for code simplifications",
    /*hidden=*/1>;

defm OMP_UseDefaultMaxThreads : BoolField<
    "openmp-ir-builder-use-default-max-threads", "true",
    "Use a default max threads if none is provided.",
    /*hidden=*/1>;
