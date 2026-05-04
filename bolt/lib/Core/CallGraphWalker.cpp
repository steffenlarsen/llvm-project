//===- bolt/Core/CallGraphWalker.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the CallGraphWalker class.
//
//===----------------------------------------------------------------------===//

#include "bolt/Core/CallGraphWalker.h"
#include "bolt/Core/BinaryFunctionCallGraph.h"
#include "bolt/Utils/BoltUtilsOptionsOptInfos.h"
#include "llvm/Support/Timer.h"
#include <queue>
#include <set>

using namespace llvm;

namespace llvm {
namespace bolt {

void CallGraphWalker::traverseCG() {
  auto *UtilOpts = bolt_utils_opts::getBoltUtilsOpts(*OptsCtx);
  bool TimeOpts = UtilOpts ? UtilOpts->get<&clv2::BOLT_TimeOpts>() : false;
  NamedRegionTimer T1("CG Traversal", "CG Traversal", "CG breakdown",
                      "CG breakdown", TimeOpts);
  std::queue<BinaryFunction *> Queue;
  std::set<BinaryFunction *> InQueue;

  for (BinaryFunction *Func : TopologicalCGOrder) {
    Queue.push(Func);
    InQueue.insert(Func);
  }

  while (!Queue.empty()) {
    BinaryFunction *Func = Queue.front();
    Queue.pop();
    InQueue.erase(Func);

    bool Changed = false;
    for (CallbackTy Visitor : Visitors) {
      bool CurVisit = Visitor(Func);
      Changed = Changed || CurVisit;
    }

    if (Changed) {
      for (CallGraph::NodeId CallerID : CG.predecessors(CG.getNodeId(Func))) {
        BinaryFunction *CallerFunc = CG.nodeIdToFunc(CallerID);
        if (InQueue.count(CallerFunc))
          continue;
        Queue.push(CallerFunc);
        InQueue.insert(CallerFunc);
      }
    }
  }
}

void CallGraphWalker::walk() {
  TopologicalCGOrder = CG.buildTraversalOrder(*OptsCtx);
  traverseCG();
}

} // namespace bolt
} // namespace llvm
