//===-- OffloadTypes.td - Offload dialect type definitions -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the two opaque handle types of the offload dialect:
//   !offload.stream  — cudaStream_t / hipStream_t
//   !offload.event   — cudaEvent_t  / hipEvent_t
//
//===----------------------------------------------------------------------===//

#ifndef OFFLOAD_TYPES
#define OFFLOAD_TYPES

include "mlir/Dialect/Offload/IR/OffloadBase.td"

//===----------------------------------------------------------------------===//
// StreamType
//===----------------------------------------------------------------------===//

def OFFLOAD_StreamType : TypeDef<Offload_Dialect, "Stream"> {
  let mnemonic = "stream";
  let summary = "Opaque handle for a CUDA/HIP stream (cudaStream_t / hipStream_t)";
  let description = [{
    An opaque, reference-counted handle to a device command queue.
    Created by `offload.stream_create` and consumed by
    `offload.stream_destroy` / `offload.stream_sync`.
  }];
}

//===----------------------------------------------------------------------===//
// EventType
//===----------------------------------------------------------------------===//

def OFFLOAD_EventType : TypeDef<Offload_Dialect, "Event"> {
  let mnemonic = "event";
  let summary = "Opaque handle for a CUDA/HIP event (cudaEvent_t / hipEvent_t)";
  let description = [{
    An opaque handle to a device-timeline marker.  Events can be used to
    measure elapsed time between two points in a stream or to synchronize
    across streams.
  }];
}

#endif // OFFLOAD_TYPES
