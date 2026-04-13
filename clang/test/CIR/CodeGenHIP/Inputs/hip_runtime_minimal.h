/* Minimal HIP runtime declarations for CIR offload end-to-end testing.
 * Provides just enough to compile simple kernels with hipMalloc/hipMemcpy.
 * Avoids the full ROCm header stack which requires C++ stdlib and cuda_wrappers.
 * Does NOT include <stddef.h> so it works without -internal-isystem.
 */
#pragma once
typedef __SIZE_TYPE__ size_t;

/* GPU function qualifiers */
#define __constant__ __attribute__((constant))
#define __device__   __attribute__((device))
#define __global__   __attribute__((global))
#define __host__     __attribute__((host))
#define __shared__   __attribute__((shared))
#define __managed__  __attribute__((managed))
#define __launch_bounds__(...) __attribute__((launch_bounds(__VA_ARGS__)))

/* dim3 struct */
struct dim3 {
  unsigned int x, y, z;
  __host__ __device__ dim3(unsigned int x, unsigned int y = 1,
                           unsigned int z = 1) : x(x), y(y), z(z) {}
};

/* HIP error and stream types */
typedef struct ihipStream_t *hipStream_t;
typedef enum hipError_t {
  hipSuccess = 0,
} hipError_t;

/* Kernel launch configuration */
extern "C" hipError_t __hipPushCallConfiguration(dim3 gridDim, dim3 blockDim,
                                                  size_t sharedMem = 0,
                                                  hipStream_t stream = 0);
extern "C" hipError_t __hipPopCallConfiguration(dim3 *gridDim, dim3 *blockDim,
                                                 size_t *sharedMem,
                                                 hipStream_t *stream);

/* Device-side synchronisation intrinsics */
__device__ void __syncthreads(void);

/* Event type */
typedef struct ihipEvent_t *hipEvent_t;

/* Synchronisation */
extern "C" hipError_t hipDeviceSynchronize(void);
extern "C" hipError_t hipStreamCreate(hipStream_t *pStream);
extern "C" hipError_t hipStreamDestroy(hipStream_t stream);
extern "C" hipError_t hipStreamSynchronize(hipStream_t stream);

/* Events */
extern "C" hipError_t hipEventCreate(hipEvent_t *event);
extern "C" hipError_t hipEventDestroy(hipEvent_t event);
extern "C" hipError_t hipEventRecord(hipEvent_t event, hipStream_t stream = 0);
extern "C" hipError_t hipEventSynchronize(hipEvent_t event);
extern "C" hipError_t hipStreamWaitEvent(hipStream_t stream, hipEvent_t event,
                                         unsigned int flags);

/* Memory management */
extern "C" hipError_t hipMalloc(void **ptr, size_t size);
extern "C" hipError_t hipHostMalloc(void **ptr, size_t size, unsigned flags);
extern "C" hipError_t hipMallocManaged(void **ptr, size_t size, unsigned flags);
extern "C" hipError_t hipFree(void *ptr);
extern "C" hipError_t hipHostFree(void *ptr);

typedef enum hipMemcpyKind {
  hipMemcpyHostToHost     = 0,
  hipMemcpyHostToDevice   = 1,
  hipMemcpyDeviceToHost   = 2,
  hipMemcpyDeviceToDevice = 3,
  hipMemcpyDefault        = 4,
} hipMemcpyKind;

extern "C" hipError_t hipMemcpy(void *dst, const void *src, size_t size,
                                 hipMemcpyKind kind);
extern "C" hipError_t hipMemcpyAsync(void *dst, const void *src, size_t size,
                                      hipMemcpyKind kind, hipStream_t stream);
extern "C" hipError_t hipMemset(void *dst, int value, size_t size);

typedef unsigned long long hipDeviceptr_t;
extern "C" hipError_t hipMemsetD32Async(hipDeviceptr_t dst, unsigned int value,
                                         size_t count, hipStream_t stream);
extern "C" hipError_t hipMemcpyToSymbol(const void *symbol, const void *src,
                                         size_t count, size_t offset = 0,
                                         hipMemcpyKind kind = hipMemcpyHostToDevice);
extern "C" hipError_t hipMemcpyFromSymbol(void *dst, const void *symbol,
                                           size_t count, size_t offset = 0,
                                           hipMemcpyKind kind = hipMemcpyDeviceToHost);

/* printf — host and device */
extern "C" int printf(const char *fmt, ...);
extern "C" __device__ int printf(const char *fmt, ...);

/* GPU built-in dimension variables (threadIdx, blockIdx, blockDim, gridDim).
 * These are intrinsics with no real backing storage; CIR codegen intercepts
 * member accesses and emits gpu.thread_id / gpu.block_id / ... ops directly.
 * The declarations below are needed so the parser accepts the names. */
struct __dim3_builtin { unsigned int x, y, z; };
extern const __device__ __dim3_builtin threadIdx;
extern const __device__ __dim3_builtin blockIdx;
extern const __device__ __dim3_builtin blockDim;
extern const __device__ __dim3_builtin gridDim;

