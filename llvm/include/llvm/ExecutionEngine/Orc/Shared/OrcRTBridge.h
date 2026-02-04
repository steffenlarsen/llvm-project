//===---- OrcRTBridge.h -- Utils for interacting with orc-rt ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares types and symbol names provided by the ORC runtime.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SHARED_ORCRTBRIDGE_H
#define LLVM_EXECUTIONENGINE_ORC_SHARED_ORCRTBRIDGE_H

#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorSymbolDef.h"
#include "llvm/ExecutionEngine/Orc/Shared/SimpleRemoteEPCUtils.h"
#include "llvm/ExecutionEngine/Orc/Shared/TargetProcessControlTypes.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace orc {
namespace rt {

LLVM_ABI extern const char *const SimpleExecutorDylibManagerInstanceName;
LLVM_ABI extern const char *const SimpleExecutorDylibManagerOpenWrapperName;
LLVM_ABI extern const char *const SimpleExecutorDylibManagerResolveWrapperName;

LLVM_ABI extern const char *const SimpleExecutorMemoryManagerInstanceName;
LLVM_ABI extern const char *const SimpleExecutorMemoryManagerReserveWrapperName;
LLVM_ABI extern const char
    *const SimpleExecutorMemoryManagerInitializeWrapperName;
LLVM_ABI extern const char
    *const SimpleExecutorMemoryManagerDeinitializeWrapperName;
LLVM_ABI extern const char *const SimpleExecutorMemoryManagerReleaseWrapperName;

LLVM_ABI extern const char *const ExecutorSharedMemoryMapperServiceInstanceName;
LLVM_ABI extern const char
    *const ExecutorSharedMemoryMapperServiceReserveWrapperName;
LLVM_ABI extern const char
    *const ExecutorSharedMemoryMapperServiceInitializeWrapperName;
LLVM_ABI extern const char
    *const ExecutorSharedMemoryMapperServiceDeinitializeWrapperName;
LLVM_ABI extern const char
    *const ExecutorSharedMemoryMapperServiceReleaseWrapperName;

LLVM_ABI extern const char *const MemoryWriteUInt8sWrapperName;
LLVM_ABI extern const char *const MemoryWriteUInt16sWrapperName;
LLVM_ABI extern const char *const MemoryWriteUInt32sWrapperName;
LLVM_ABI extern const char *const MemoryWriteUInt64sWrapperName;
LLVM_ABI extern const char *const MemoryWritePointersWrapperName;
LLVM_ABI extern const char *const MemoryWriteBuffersWrapperName;

LLVM_ABI extern const char *const MemoryReadUInt8sWrapperName;
LLVM_ABI extern const char *const MemoryReadUInt16sWrapperName;
LLVM_ABI extern const char *const MemoryReadUInt32sWrapperName;
LLVM_ABI extern const char *const MemoryReadUInt64sWrapperName;
LLVM_ABI extern const char *const MemoryReadPointersWrapperName;
LLVM_ABI extern const char *const MemoryReadBuffersWrapperName;
LLVM_ABI extern const char *const MemoryReadStringsWrapperName;

LLVM_ABI extern const char *const RegisterEHFrameSectionAllocActionName;
LLVM_ABI extern const char *const DeregisterEHFrameSectionAllocActionName;

LLVM_ABI extern const char *const RegisterJITLoaderGDBAllocActionName;

LLVM_ABI extern const char *const RunAsMainWrapperName;
LLVM_ABI extern const char *const RunAsVoidFunctionWrapperName;
LLVM_ABI extern const char *const RunAsIntFunctionWrapperName;

using SPSSimpleExecutorDylibManagerOpenSignature =
    shared::SPSExpected<shared::SPSExecutorAddr>(shared::SPSExecutorAddr,
                                                 shared::SPSString, uint64_t);

using SPSSimpleExecutorDylibManagerResolveSignature = shared::SPSExpected<
    shared::SPSSequence<shared::SPSOptional<shared::SPSExecutorSymbolDef>>>(
    shared::SPSExecutorAddr, shared::SPSRemoteSymbolLookupSet);

using SPSSimpleExecutorMemoryManagerReserveSignature =
    shared::SPSExpected<shared::SPSExecutorAddr>(shared::SPSExecutorAddr,
                                                 uint64_t);
using SPSSimpleExecutorMemoryManagerInitializeSignature =
    shared::SPSExpected<shared::SPSExecutorAddr>(shared::SPSExecutorAddr,
                                                 shared::SPSFinalizeRequest);
using SPSSimpleExecutorMemoryManagerDeinitializeSignature = shared::SPSError(
    shared::SPSExecutorAddr, shared::SPSSequence<shared::SPSExecutorAddr>);
using SPSSimpleExecutorMemoryManagerReleaseSignature = shared::SPSError(
    shared::SPSExecutorAddr, shared::SPSSequence<shared::SPSExecutorAddr>);

// ExecutorSharedMemoryMapperService
using SPSExecutorSharedMemoryMapperServiceReserveSignature =
    shared::SPSExpected<
        shared::SPSTuple<shared::SPSExecutorAddr, shared::SPSString>>(
        shared::SPSExecutorAddr, uint64_t);
using SPSExecutorSharedMemoryMapperServiceInitializeSignature =
    shared::SPSExpected<shared::SPSExecutorAddr>(
        shared::SPSExecutorAddr, shared::SPSExecutorAddr,
        shared::SPSSharedMemoryFinalizeRequest);
using SPSExecutorSharedMemoryMapperServiceDeinitializeSignature =
    shared::SPSError(shared::SPSExecutorAddr,
                     shared::SPSSequence<shared::SPSExecutorAddr>);
using SPSExecutorSharedMemoryMapperServiceReleaseSignature = shared::SPSError(
    shared::SPSExecutorAddr, shared::SPSSequence<shared::SPSExecutorAddr>);

// SimpleNativeMemoryMap APIs.
using SPSSimpleRemoteMemoryMapReserveSignature =
    shared::SPSExpected<shared::SPSExecutorAddr>(shared::SPSExecutorAddr,
                                                 uint64_t);
using SPSSimpleRemoteMemoryMapInitializeSignature =
    shared::SPSExpected<shared::SPSExecutorAddr>(shared::SPSExecutorAddr,
                                                 shared::SPSFinalizeRequest);
using SPSSimpleRemoteMemoryMapDeinitializeSignature = shared::SPSError(
    shared::SPSExecutorAddr, shared::SPSSequence<shared::SPSExecutorAddr>);
using SPSSimpleRemoteMemoryMapReleaseSignature = shared::SPSError(
    shared::SPSExecutorAddr, shared::SPSSequence<shared::SPSExecutorAddr>);

using SPSRunAsMainSignature = int64_t(shared::SPSExecutorAddr,
                                      shared::SPSSequence<shared::SPSString>);
using SPSRunAsVoidFunctionSignature = int32_t(shared::SPSExecutorAddr);
using SPSRunAsIntFunctionSignature = int32_t(shared::SPSExecutorAddr, int32_t);
} // end namespace rt

namespace rt_alt {
LLVM_ABI extern const char *const UnwindInfoManagerRegisterActionName;
LLVM_ABI extern const char *const UnwindInfoManagerDeregisterActionName;
} // end namespace rt_alt
} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_SHARED_ORCRTBRIDGE_H
