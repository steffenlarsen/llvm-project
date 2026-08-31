// Checks the recording of a compilation for several targets, which
// preprocesses once per target inside one job, aligns the streams on the
// conditional structure the preprocessor reported, and splices them into a
// single stream. Divergent regions carry every target's tokens in turn,
// bracketed by annot_target_alt_{begin,sep,end}.
//
// Each target's original token stream must be recoverable from the merged
// stream exactly.
//
// REQUIRES: amdgpu-registered-target

// Parsing the merged stream has to produce exactly the compilation that
// parsing the lexer directly does, for the host and for the device. Anything
// the merge loses, misplaces or fails to translate shows up here.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -emit-llvm -o %t.hdirect %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o %t.direct %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -emit-llvm -o %t.hmerged %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple}" \
// RUN:   -multi-target-aux-output=2:%t.merged.bc
// RUN: diff %t.hdirect %t.hmerged
// RUN: grep -v '^; ModuleID' %t.direct > %t.direct.body
// RUN: llvm-dis %t.merged.bc -o - | grep -v '^; ModuleID' > %t.merged.body
// RUN: diff %t.direct.body %t.merged.body

// The alignment itself is reported under -dump-conditional-regions.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -fsyntax-only %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple}" \
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
