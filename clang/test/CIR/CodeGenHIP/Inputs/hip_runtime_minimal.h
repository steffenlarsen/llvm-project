/* Minimal HIP runtime declarations for CIR offload end-to-end testing.
 * Provides just enough to compile simple kernels with hipMalloc/hipMemcpy.
 * Avoids the full ROCm header stack which requires C++ stdlib and cuda_wrappers.
 */
#pragma once
#include <stddef.h>

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

/* Memory management */
extern "C" hipError_t hipMalloc(void **ptr, size_t size);
extern "C" hipError_t hipFree(void *ptr);

typedef enum hipMemcpyKind {
  hipMemcpyHostToHost     = 0,
  hipMemcpyHostToDevice   = 1,
  hipMemcpyDeviceToHost   = 2,
  hipMemcpyDeviceToDevice = 3,
  hipMemcpyDefault        = 4,
} hipMemcpyKind;

extern "C" hipError_t hipMemcpy(void *dst, const void *src, size_t size,
                                 hipMemcpyKind kind);

/* printf — host and device */
extern "C" int printf(const char *fmt, ...);
extern "C" __device__ int printf(const char *fmt, ...);
