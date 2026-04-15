// RUN: mlir-opt --split-input-file --offload-tighten-launch-bounds %s \
// RUN:   | FileCheck %s

// Tests for OffloadTightenLaunchBoundsPass.
//
// The pass clones kernels with tighter launch_bounds when all block dimensions
// at a given call site are statically known and the total fits within a
// smaller candidate bound from {warpSize, 256, 512}.  The warp size is
// inferred from the offload.target module attribute; when absent, only the
// fixed candidates {256, 512} are used.  The original kernel is always
// preserved.  Clones are named @original$maxN.

//===----------------------------------------------------------------------===//
// 1. Basic: static block=256 → clone @k$max256, original preserved
//===----------------------------------------------------------------------===//

// Original kernel must still be present.
// CHECK:      offload.func @k(
// CHECK-SAME: exec_space = #offload.exec_space<global>
// CHECK-NOT:  launch_bounds

// Clone created with tighter bound.
// CHECK:      offload.func @k$max256(
// CHECK-SAME: launch_bounds = #offload.launch_bounds<256>

// Launch redirected to clone.
// CHECK:      offload.kernel_launch @k$max256

offload.func @k(%a: memref<f32>) exec_space = #offload.exec_space<global> {
  offload.return
}

offload.func @host(%a: memref<f32>) exec_space = #offload.exec_space<host> {
  %c256 = arith.constant 256 : index
  %c1   = arith.constant 1   : index
  offload.kernel_launch @k
      grid  = (%c1, %c1, %c1)
      block = (%c256, %c1, %c1)
      args  = (%a : memref<f32>)
  offload.return
}

// -----

//===----------------------------------------------------------------------===//
// 2. Kernel has no launch_bounds initially (implicit 1024) → clone added
//===----------------------------------------------------------------------===//

// CHECK:      offload.func @nobound(
// CHECK-NOT:  launch_bounds
// CHECK:      offload.func @nobound$max256(
// CHECK-SAME: launch_bounds = #offload.launch_bounds<256>
// CHECK:      offload.kernel_launch @nobound$max256

offload.func @nobound(%x: i32) exec_space = #offload.exec_space<global> {
  offload.return
}

offload.func @host_nobound(%x: i32) exec_space = #offload.exec_space<host> {
  %c256 = arith.constant 256 : index
  %c1   = arith.constant 1   : index
  offload.kernel_launch @nobound
      grid  = (%c1, %c1, %c1)
      block = (%c256, %c1, %c1)
      args  = (%x : i32)
  offload.return
}

// -----

//===----------------------------------------------------------------------===//
// 3. Multi-bucket: block=128 and block=256 from different sites
//    → one clone (both map to bucket 256), both launches redirected
//===----------------------------------------------------------------------===//

// Original preserved.
// CHECK:      offload.func @multi(
// CHECK-NOT:  launch_bounds

// Clone for block=128 → bucket 256 (smallest candidate >= 128 in {256,512}).
// Block=256 also maps to bucket 256, so there is only one clone.
// CHECK:      offload.func @multi$max256(
// CHECK-SAME: launch_bounds = #offload.launch_bounds<256>

// Both launches redirect to the single clone.
// CHECK-COUNT-2: offload.kernel_launch @multi$max256

offload.func @multi(%x: f32) exec_space = #offload.exec_space<global> {
  offload.return
}

offload.func @host_multi(%x: f32) exec_space = #offload.exec_space<host> {
  %c128 = arith.constant 128 : index
  %c256 = arith.constant 256 : index
  %c1   = arith.constant 1   : index
  offload.kernel_launch @multi
      grid  = (%c1, %c1, %c1)
      block = (%c128, %c1, %c1)
      args  = (%x : f32)
  offload.kernel_launch @multi
      grid  = (%c1, %c1, %c1)
      block = (%c256, %c1, %c1)
      args  = (%x : f32)
  offload.return
}

// -----

//===----------------------------------------------------------------------===//
// 4. Two buckets with GFX9 target (warp size 64): block=64 and block=256
//    → two distinct clones @k4$max64 and @k4$max256
//===----------------------------------------------------------------------===//

// Original preserved.
// CHECK:      offload.func @k4(
// CHECK-SAME: exec_space = #offload.exec_space<global>
// CHECK-NOT:  launch_bounds

// Two clones (DenseMap iteration order is non-deterministic, use DAG).
// CHECK-DAG:  offload.func @k4$max64(
// CHECK-DAG:  offload.func @k4$max256(

// Both launches redirected.
// CHECK:      offload.kernel_launch @k4$max
// CHECK:      offload.kernel_launch @k4$max

module attributes {
  offload.target = #offload.target<runtime = "hip", architectures = ["gfx90a"]>
} {
  offload.func @k4(%x: f32) exec_space = #offload.exec_space<global> {
    offload.return
  }

  offload.func @host_k4(%x: f32) exec_space = #offload.exec_space<host> {
    %c64  = arith.constant 64  : index
    %c256 = arith.constant 256 : index
    %c1   = arith.constant 1   : index
    offload.kernel_launch @k4
        grid  = (%c1, %c1, %c1)
        block = (%c64, %c1, %c1)
        args  = (%x : f32)
    offload.kernel_launch @k4
        grid  = (%c1, %c1, %c1)
        block = (%c256, %c1, %c1)
        args  = (%x : f32)
    offload.return
  }
}

// -----

//===----------------------------------------------------------------------===//
// 5. Dynamic launch present: static site → clone; dynamic site → original
//===----------------------------------------------------------------------===//

// Original preserved (dynamic launch still calls it).
// CHECK:      offload.func @dynmixed(
// CHECK-NOT:  launch_bounds
// CHECK:      offload.func @dynmixed$max256(
// CHECK-SAME: launch_bounds = #offload.launch_bounds<256>

// Static launch redirected; dynamic launch untouched.
// CHECK:      offload.kernel_launch @dynmixed$max256
// CHECK:      offload.kernel_launch @dynmixed

offload.func @dynmixed(%x: i32) exec_space = #offload.exec_space<global> {
  offload.return
}

offload.func @host_dynmixed(%x: i32, %bsz: index)
    exec_space = #offload.exec_space<host> {
  %c256 = arith.constant 256 : index
  %c1   = arith.constant 1   : index
  // Static launch — will be redirected to clone.
  offload.kernel_launch @dynmixed
      grid  = (%c1, %c1, %c1)
      block = (%c256, %c1, %c1)
      args  = (%x : i32)
  // Dynamic launch — block size unknown at compile time.
  offload.kernel_launch @dynmixed
      grid  = (%c1, %c1, %c1)
      block = (%bsz, %c1, %c1)
      args  = (%x : i32)
  offload.return
}

// -----

//===----------------------------------------------------------------------===//
// 6. All launches dynamic → kernel completely untouched
//===----------------------------------------------------------------------===//

// No clone should be created.
// CHECK:      offload.func @alldyn(
// CHECK-NOT:  offload.func @alldyn$max

// Both launches keep pointing at original.
// CHECK-COUNT-2: offload.kernel_launch @alldyn

offload.func @alldyn(%x: f32) exec_space = #offload.exec_space<global> {
  offload.return
}

offload.func @host_alldyn(%x: f32, %bx: index, %by: index)
    exec_space = #offload.exec_space<host> {
  %c1 = arith.constant 1 : index
  offload.kernel_launch @alldyn
      grid  = (%c1, %c1, %c1)
      block = (%bx, %c1, %c1)
      args  = (%x : f32)
  offload.kernel_launch @alldyn
      grid  = (%c1, %c1, %c1)
      block = (%bx, %by, %c1)
      args  = (%x : f32)
  offload.return
}

// -----

//===----------------------------------------------------------------------===//
// 7. No improvement: block=700 — no candidate in {256, 512} covers it
//===----------------------------------------------------------------------===//

// Kernel untouched, no clone, launch unchanged.
// CHECK:      offload.func @toobig(
// CHECK-NOT:  offload.func @toobig$max
// CHECK:      offload.kernel_launch @toobig

offload.func @toobig(%x: i32) exec_space = #offload.exec_space<global> {
  offload.return
}

offload.func @host_toobig(%x: i32) exec_space = #offload.exec_space<host> {
  %c700 = arith.constant 700 : index
  %c1   = arith.constant 1   : index
  offload.kernel_launch @toobig
      grid  = (%c1, %c1, %c1)
      block = (%c700, %c1, %c1)
      args  = (%x : i32)
  offload.return
}

// -----

//===----------------------------------------------------------------------===//
// 8. Already tight: kernel has launch_bounds<256>, static block=256
//    → bucket 256 is not < currentMax 256, no clone
//===----------------------------------------------------------------------===//

// CHECK:      offload.func @tight(
// CHECK-SAME: launch_bounds = #offload.launch_bounds<256>
// CHECK-NOT:  offload.func @tight$max
// CHECK:      offload.kernel_launch @tight

offload.func @tight(%x: f32)
    exec_space = #offload.exec_space<global>
    launch_bounds = #offload.launch_bounds<256> {
  offload.return
}

offload.func @host_tight(%x: f32) exec_space = #offload.exec_space<host> {
  %c256 = arith.constant 256 : index
  %c1   = arith.constant 1   : index
  offload.kernel_launch @tight
      grid  = (%c1, %c1, %c1)
      block = (%c256, %c1, %c1)
      args  = (%x : f32)
  offload.return
}

// -----

//===----------------------------------------------------------------------===//
// 9. 3D block: blockX=8, blockY=8, blockZ=4 → total=256 → clone $max256
//===----------------------------------------------------------------------===//

// CHECK:      offload.func @threedim(
// CHECK-NOT:  launch_bounds
// CHECK:      offload.func @threedim$max256(
// CHECK-SAME: launch_bounds = #offload.launch_bounds<256>
// CHECK:      offload.kernel_launch @threedim$max256

offload.func @threedim(%x: memref<f32>) exec_space = #offload.exec_space<global> {
  offload.return
}

offload.func @host_threedim(%x: memref<f32>)
    exec_space = #offload.exec_space<host> {
  %c8 = arith.constant 8 : index
  %c4 = arith.constant 4 : index
  offload.kernel_launch @threedim
      grid  = (%c8, %c8, %c4)
      block = (%c8, %c8, %c4)
      args  = (%x : memref<f32>)
  offload.return
}

// -----

//===----------------------------------------------------------------------===//
// 10. Warp-size bucket: GFX9 target (wavefront=64), block=32 → bucket=64
//===----------------------------------------------------------------------===//

// CHECK:      offload.func @warp64(
// CHECK:      offload.func @warp64$max64(
// CHECK-SAME: launch_bounds = #offload.launch_bounds<64>
// CHECK:      offload.kernel_launch @warp64$max64

module attributes {
  offload.target = #offload.target<runtime = "hip", architectures = ["gfx90a"]>
} {
  offload.func @warp64(%x: f32) exec_space = #offload.exec_space<global> {
    offload.return
  }

  offload.func @host_warp64(%x: f32) exec_space = #offload.exec_space<host> {
    %c32 = arith.constant 32 : index
    %c1  = arith.constant 1  : index
    offload.kernel_launch @warp64
        grid  = (%c1, %c1, %c1)
        block = (%c32, %c1, %c1)
        args  = (%x : f32)
    offload.return
  }
}

// -----

//===----------------------------------------------------------------------===//
// 11. Warp-size bucket: GFX10 target (wavefront=32), block=32 → bucket=32
//===----------------------------------------------------------------------===//

// CHECK:      offload.func @warp32(
// CHECK:      offload.func @warp32$max32(
// CHECK-SAME: launch_bounds = #offload.launch_bounds<32>
// CHECK:      offload.kernel_launch @warp32$max32

module attributes {
  offload.target = #offload.target<runtime = "hip", architectures = ["gfx1030"]>
} {
  offload.func @warp32(%x: f32) exec_space = #offload.exec_space<global> {
    offload.return
  }

  offload.func @host_warp32(%x: f32) exec_space = #offload.exec_space<host> {
    %c32 = arith.constant 32 : index
    %c1  = arith.constant 1  : index
    offload.kernel_launch @warp32
        grid  = (%c1, %c1, %c1)
        block = (%c32, %c1, %c1)
        args  = (%x : f32)
    offload.return
  }
}

// -----

//===----------------------------------------------------------------------===//
// 12. Mixed targets: gfx90a (warp=64) + gfx1030 (warp=32) → max = 64
//     block=32 → bucket=64, not 32
//===----------------------------------------------------------------------===//

// CHECK:      offload.func @warpmix(
// CHECK:      offload.func @warpmix$max64(
// CHECK-SAME: launch_bounds = #offload.launch_bounds<64>
// CHECK-NOT:  offload.func @warpmix$max32
// CHECK:      offload.kernel_launch @warpmix$max64

module attributes {
  offload.target = #offload.target<runtime = "hip",
                                   architectures = ["gfx90a", "gfx1030"]>
} {
  offload.func @warpmix(%x: f32) exec_space = #offload.exec_space<global> {
    offload.return
  }

  offload.func @host_warpmix(%x: f32) exec_space = #offload.exec_space<host> {
    %c32 = arith.constant 32 : index
    %c1  = arith.constant 1  : index
    offload.kernel_launch @warpmix
        grid  = (%c1, %c1, %c1)
        block = (%c32, %c1, %c1)
        args  = (%x : f32)
    offload.return
  }
}

// -----

//===----------------------------------------------------------------------===//
// 13. CUDA target (sm_80, warp=32): block=32 → bucket=32
//===----------------------------------------------------------------------===//

// CHECK:      offload.func @cudawarp(
// CHECK:      offload.func @cudawarp$max32(
// CHECK-SAME: launch_bounds = #offload.launch_bounds<32>
// CHECK:      offload.kernel_launch @cudawarp$max32

module attributes {
  offload.target = #offload.target<runtime = "cuda", architectures = ["sm_80"]>
} {
  offload.func @cudawarp(%x: f32) exec_space = #offload.exec_space<global> {
    offload.return
  }

  offload.func @host_cudawarp(%x: f32) exec_space = #offload.exec_space<host> {
    %c32 = arith.constant 32 : index
    %c1  = arith.constant 1  : index
    offload.kernel_launch @cudawarp
        grid  = (%c1, %c1, %c1)
        block = (%c32, %c1, %c1)
        args  = (%x : f32)
    offload.return
  }
}

// -----

//===----------------------------------------------------------------------===//
// 14. No target attribute: warp-size candidate omitted, block=64 → bucket=256
//===----------------------------------------------------------------------===//

// Without a target attribute only {256, 512} are candidates.
// block=64 → smallest candidate >= 64 is 256.
// CHECK:      offload.func @notarget(
// CHECK:      offload.func @notarget$max256(
// CHECK-SAME: launch_bounds = #offload.launch_bounds<256>
// CHECK-NOT:  offload.func @notarget$max64
// CHECK:      offload.kernel_launch @notarget$max256

offload.func @notarget(%x: f32) exec_space = #offload.exec_space<global> {
  offload.return
}

offload.func @host_notarget(%x: f32) exec_space = #offload.exec_space<host> {
  %c64 = arith.constant 64 : index
  %c1  = arith.constant 1  : index
  offload.kernel_launch @notarget
      grid  = (%c1, %c1, %c1)
      block = (%c64, %c1, %c1)
      args  = (%x : f32)
  offload.return
}

// -----

//===----------------------------------------------------------------------===//
// 15. minBlocksPerSM is preserved on clone
//===----------------------------------------------------------------------===//

// CHECK:      offload.func @withminblocks(
// CHECK-SAME: launch_bounds = #offload.launch_bounds<1024, 2>
// CHECK:      offload.func @withminblocks$max256(
// CHECK-SAME: launch_bounds = #offload.launch_bounds<256, 2>
// CHECK:      offload.kernel_launch @withminblocks$max256

offload.func @withminblocks(%x: i32)
    exec_space = #offload.exec_space<global>
    launch_bounds = #offload.launch_bounds<1024, 2> {
  offload.return
}

offload.func @host_withminblocks(%x: i32)
    exec_space = #offload.exec_space<host> {
  %c256 = arith.constant 256 : index
  %c1   = arith.constant 1   : index
  offload.kernel_launch @withminblocks
      grid  = (%c1, %c1, %c1)
      block = (%c256, %c1, %c1)
      args  = (%x : i32)
  offload.return
}
