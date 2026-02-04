//===------ OrcRTBridge.cpp - Executor functions for bootstrap -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/Shared/OrcRTBridge.h"

namespace llvm {
namespace orc {
namespace rt {

const char *const SimpleExecutorDylibManagerInstanceName =
    "__llvm_orc_SimpleExecutorDylibManager_Instance";
const char *const SimpleExecutorDylibManagerOpenWrapperName =
    "__llvm_orc_SimpleExecutorDylibManager_open_wrapper";
const char *const SimpleExecutorDylibManagerResolveWrapperName =
    "__llvm_orc_SimpleExecutorDylibManager_resolve_wrapper";

const char *const SimpleExecutorMemoryManagerInstanceName =
    "__llvm_orc_SimpleExecutorMemoryManager_Instance";
const char *const SimpleExecutorMemoryManagerReserveWrapperName =
    "__llvm_orc_SimpleExecutorMemoryManager_reserve_wrapper";
const char *const SimpleExecutorMemoryManagerInitializeWrapperName =
    "__llvm_orc_SimpleExecutorMemoryManager_initialize_wrapper";
const char *const SimpleExecutorMemoryManagerDeinitializeWrapperName =
    "__llvm_orc_SimpleExecutorMemoryManager_deinitialize_wrapper";
const char *const SimpleExecutorMemoryManagerReleaseWrapperName =
    "__llvm_orc_SimpleExecutorMemoryManager_release_wrapper";

const char *const ExecutorSharedMemoryMapperServiceInstanceName =
    "__llvm_orc_ExecutorSharedMemoryMapperService_Instance";
const char *const ExecutorSharedMemoryMapperServiceReserveWrapperName =
    "__llvm_orc_ExecutorSharedMemoryMapperService_Reserve";
const char *const ExecutorSharedMemoryMapperServiceInitializeWrapperName =
    "__llvm_orc_ExecutorSharedMemoryMapperService_Initialize";
const char *const ExecutorSharedMemoryMapperServiceDeinitializeWrapperName =
    "__llvm_orc_ExecutorSharedMemoryMapperService_Deinitialize";
const char *const ExecutorSharedMemoryMapperServiceReleaseWrapperName =
    "__llvm_orc_ExecutorSharedMemoryMapperService_Release";

const char *const MemoryWriteUInt8sWrapperName =
    "__llvm_orc_bootstrap_mem_write_uint8s_wrapper";
const char *const MemoryWriteUInt16sWrapperName =
    "__llvm_orc_bootstrap_mem_write_uint16s_wrapper";
const char *const MemoryWriteUInt32sWrapperName =
    "__llvm_orc_bootstrap_mem_write_uint32s_wrapper";
const char *const MemoryWriteUInt64sWrapperName =
    "__llvm_orc_bootstrap_mem_write_uint64s_wrapper";
const char *const MemoryWritePointersWrapperName =
    "__llvm_orc_bootstrap_mem_write_pointers_wrapper";
const char *const MemoryWriteBuffersWrapperName =
    "__llvm_orc_bootstrap_mem_write_buffers_wrapper";

const char *const MemoryReadUInt8sWrapperName =
    "__llvm_orc_bootstrap_mem_read_uint8s_wrapper";
const char *const MemoryReadUInt16sWrapperName =
    "__llvm_orc_bootstrap_mem_read_uint16s_wrapper";
const char *const MemoryReadUInt32sWrapperName =
    "__llvm_orc_bootstrap_mem_read_uint32s_wrapper";
const char *const MemoryReadUInt64sWrapperName =
    "__llvm_orc_bootstrap_mem_read_uint64s_wrapper";
const char *const MemoryReadPointersWrapperName =
    "__llvm_orc_bootstrap_mem_read_pointers_wrapper";
const char *const MemoryReadBuffersWrapperName =
    "__llvm_orc_bootstrap_mem_read_buffers_wrapper";
const char *const MemoryReadStringsWrapperName =
    "__llvm_orc_bootstrap_mem_read_strings_wrapper";

const char *const RegisterEHFrameSectionAllocActionName =
    "llvm_orc_registerEHFrameAllocAction";
const char *const DeregisterEHFrameSectionAllocActionName =
    "llvm_orc_deregisterEHFrameAllocAction";

const char *const RegisterJITLoaderGDBAllocActionName =
    "llvm_orc_registerJITLoaderGDBAllocAction";

const char *const RunAsMainWrapperName =
    "__llvm_orc_bootstrap_run_as_main_wrapper";
const char *const RunAsVoidFunctionWrapperName =
    "__llvm_orc_bootstrap_run_as_void_function_wrapper";
const char *const RunAsIntFunctionWrapperName =
    "__llvm_orc_bootstrap_run_as_int_function_wrapper";

} // end namespace rt
namespace rt_alt {
const char *const UnwindInfoManagerRegisterActionName =
    "orc_rt_alt_UnwindInfoManager_register";
const char *const UnwindInfoManagerDeregisterActionName =
    "orc_rt_alt_UnwindInfoManager_deregister";
} // end namespace rt_alt
} // end namespace orc
} // end namespace llvm
