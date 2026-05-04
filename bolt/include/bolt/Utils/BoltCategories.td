//===- BoltCategories.td - BOLT option categories --------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "bolt/Utils/BoltContextTypes.td"

def BoltCat         : OptionCategory<"BoltCat", "BOLT generic options">;
def BoltDiffCat     : OptionCategory<"BoltDiffCat",
                                      "BOLTDIFF generic options">;
def BoltOptCat      : OptionCategory<"BoltOptCat",
                                      "BOLT optimization options">;
def BoltRelocCat    : OptionCategory<"BoltRelocCat",
                                      "BOLT options in relocation mode">;
def BoltOutputCat   : OptionCategory<"BoltOutputCat", "Output options">;
def AggregatorCat   : OptionCategory<"AggregatorCat",
                                      "Data aggregation options">;
def BoltInstrCat    : OptionCategory<"BoltInstrCat",
                                      "BOLT instrumentation options">;
def HeatmapCat      : OptionCategory<"HeatmapCat", "Heatmap options">;
def BinaryAnalysisCat : OptionCategory<"BinaryAnalysisCat",
                                        "BinaryAnalysis options">;
