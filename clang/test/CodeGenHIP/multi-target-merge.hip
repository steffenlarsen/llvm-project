// PROTOTYPE (Stages 3.3-3.4): recording, aligning and merging the token
// streams of several targets.
//
// -mllvm -multi-target-record preprocesses once per target inside one job,
// aligns the streams on the conditional structure the preprocessor reported,
// and splices them into a single stream. Divergent regions carry every target's
// tokens in turn, bracketed by annot_target_alt_{begin,sep,end}.
//
// The property that makes the merge trustworthy is that each target can be read
// back out of it exactly, so that is what this checks.
//
// REQUIRES: amdgpu-registered-target

// Parsing the merged stream has to produce exactly the compilation that
// parsing the lexer directly does. Anything the merge loses, misplaces or
// fails to translate shows up here.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o %t.direct %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o %t.merged %s -mllvm -multi-target-record
// RUN: diff %t.direct %t.merged

// The same for the host side, where the roles of the two targets swap.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -emit-llvm -o %t.hdirect %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -emit-llvm -o %t.hmerged %s -mllvm -multi-target-record
// RUN: diff %t.hdirect %t.hmerged

// The alignment itself is reported under -dump-conditional-regions.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu \
// RUN:   -fcuda-is-device -x hip -fsyntax-only %s -mllvm -multi-target-record \
// RUN:   -mllvm -dump-conditional-regions 2>&1 | FileCheck %s
// CHECK: multi-target recording:
// CHECK-DAG: amdgcn-amd-amdhsa: {{[0-9]+}} tokens
// CHECK-DAG: x86_64-unknown-linux-gnu: {{[0-9]+}} tokens
// Nothing here is divergent, so the whole stream is one shared segment.
// CHECK: segments: 1 shared, 0 alternative
// CHECK: token saving: 50.00%

__attribute__((device)) int shared_between_targets(int x) {
  return x * 2 + sizeof(int);
}
