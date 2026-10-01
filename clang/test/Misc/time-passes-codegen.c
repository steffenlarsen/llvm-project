// Check that -ftime-report also reports the timers of the code generator: the
// legacy pass manager's passes and the regions timed by SelectionDAG.
// REQUIRES: x86-registered-target
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-obj -O1 \
// RUN:     -ftime-report %s -o /dev/null 2>&1 | FileCheck %s

// CHECK-DAG: Pass execution timing report
// CHECK-DAG: X86 DAG->DAG Instruction Selection
// CHECK-DAG: Instruction Selection and Scheduling
// CHECK-DAG: DAG Combining 1

int foo(int x, int y) { return x * y + 3; }
