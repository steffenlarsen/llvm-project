// REQUIRES: amdgpu-registered-target
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx90a -x hip \
// RUN: -std=c++11 -fclangir -fcuda-is-device -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx90a -x hip \
// RUN: -std=c++11 -fcuda-is-device -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

//===----------------------------------------------------------------------===//
// AMDGPU reports unaligned access as expensive, so the bitfield access-unit
// algorithm has to decide whether accumulating a span would produce an
// unnaturally-aligned unit. That decision used to be unimplemented, which both
// rejected valid code and -- when the run ended at the last field -- crashed
// while building the diagnostic out of a past-the-end field iterator.
//
// The LLVM checks are shared with the classic CodeGen run line, so they pin
// the record layout of both paths together.
//===----------------------------------------------------------------------===//

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

// A bitfield run that ends the record: this is the shape that crashed, because
// the span is closed by reaching the end of the fields rather than by a
// following non-bitfield member.
struct TrailingRun {
  int sign, leaves[4];
  unsigned truthtable : 16;
  unsigned value : 11;
  unsigned nLeaves : 4;
  bool used;
};

// LLVM-DAG: %struct.TrailingRun = type { i32, [4 x i32], i32, i8 }

// A run that has to be split: the accumulated unit would not be naturally
// aligned, so the best span so far is installed instead of growing it.
struct MixedWidths {
  unsigned char a : 3;
  unsigned char b : 5;
  unsigned int c : 20;
  unsigned int d : 12;
  unsigned short e : 9;
  unsigned char f;
  unsigned int g : 17;
};

// LLVM-DAG: %struct.MixedWidths = type { i32, i32, i8, [3 x i8] }

__global__ void k(TrailingRun *t, MixedWidths *m, unsigned *o) {
  o[0] = t->truthtable + t->value + t->nLeaves + t->used;
  t->value = o[1];
  t->nLeaves = o[2];
  o[3] = m->a + m->b + m->c + m->d + m->e + m->f + m->g;
  m->c = o[4];
}
