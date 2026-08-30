#ifndef YONA_RUNTIME_GPU_API_H
#define YONA_RUNTIME_GPU_API_H

#include "yona/Runtime/Concurrency/Async.h"

#include <stdint.h>

/// C ABI for `Std\Gpu` — discovery, columnar ops, `mapGpu` / graph / pinned
/// buffers; see docs/design-gpu-async.md and docs/gpu-architecture.md.
/// Without CMake Vulkan (any platform), probes return zeros / false.
///
/// Ownership: pointer arguments are borrowed for the duration stated by each
/// operation. Returned task handles follow the Async.h ownership contract;
/// mapped-buffer handles are owned until explicitly freed. Failure modes are
/// reported by null handles or the documented status codes and never transfer
/// ownership of caller buffers. Discovery, submission, and buffer operations
/// are thread-safe. Context initialization is idempotent, but initialization
/// and shutdown must be serialized with each other; shutdown must run only
/// after all GPU operations and borrowed buffers have completed.

#ifdef __cplusplus
extern "C" {
#endif

/* Yona nullary externs compile as `() -> T` → one `UNIT` (i64) at the LLVM call
 * site. */
int64_t YonaStdGpuAvailable(int64_t Unit);
int64_t YonaStdGpuPhysicalDeviceCount(int64_t Unit);

/* Yona `extern native floatArrayMul2Async : FloatArray -> Int` — in-place
 * multiply by 2 on GPU when available (`scale = 2.0` shader push constant);
 * promise completes with 0 on success or a negative error code. Codegen
 * supplies the hidden mandatory ResultType descriptor, which the task copies.
 * Returns null for a null descriptor or allocation/submission failure. The
 * operation is thread-safe and its result follows the task await contract. */
YonaTaskRef YonaStdGpuFloatArrayMul2Async(double *FloatArray,
                                          const YonaTypeDescriptor *ResultType);

/* Yona `extern native floatArrayScaleAsync : Float -> FloatArray -> Int`.
 * Ownership, failures, and thread-safety match the multiply operation. */
YonaTaskRef
YonaStdGpuFloatArrayScaleAsync(double Scale, double *FloatArray,
                               const YonaTypeDescriptor *ResultType);

/* C-only / tests: lazy `VkDevice`, compute `VkQueue`, `VkCommandPool`, and a
 *
 * fixed nop compute pipeline (`src/Runtime/Generated/Nop.comp` → embedded
 *
 * SPIR-V). Returns 0
 * on success; -1 no Vulkan in this build; -2 no compute
 * GPU; -3 instance; -4 device; -5 command pool; -6 shader module; -7 pipeline
 * layout; -8 compute pipeline. Idempotent while context is live. Pair with
 * `YonaRuntimeGpuVulkanContextShutdown` in tests or teardown (optional for
 * processes). */
int YonaRuntimeGpuVulkanContextInitialize(void);
void YonaRuntimeGpuVulkanContextShutdown(void);

/* One-shot nop `vkCmdDispatch(1,1,1)` + `vkWaitForFences` — **not** for Yona
 * async workers (see design-gpu-async.md); tests / manual driver checks only.
 * Call after `YonaRuntimeGpuVulkanContextInitialize`. Returns 0; -1 no Vulkan
 * build; -9 context not initialized / incomplete; -10.. dispatch path failure.
 */
int YonaRuntimeGpuVulkanDispatchNopOnce(void);

/* Synchronous FloatArray-style payload: `elements` points at `count` contiguous
 * doubles (Yona layout); each element is multiplied by `scale` on the GPU and
 * written back. Uses `shaderFloat64` when present; otherwise narrows to f32,
 * runs the embedded f32 scale kernel, and widens back. Requires
 * `YonaRuntimeGpuVulkanContextInitialize`. Returns 0; -1 no Vulkan build; -9 no
 * context; -16 null `elements`; -20 no float shader support; -21 prior scale
 * pipeline init failed; -22 pipeline setup; -30..-42 resource / dispatch
 * failures (see src/Runtime/Gpu/Stub.c). */
int YonaRuntimeGpuVulkanFloat64BufferScaleInPlace(double *Elements,
                                                  uint32_t Count, double Scale);

/* Block-reduce sum of `count` doubles into `*out_sum` (f64 shader or f32
 * narrow). Same device as scale. Returns 0 on success; negative codes match
 * scale_inplace. */
int YonaRuntimeGpuVulkanFloat64BufferReduceSum(const double *Elements,
                                               uint32_t Count, double *OutSum);

/* Same as scale with factor 2.0. */
int YonaRuntimeGpuVulkanFloat64BufferMultiply2InPlace(double *Elements,
                                                      uint32_t Count);

/* Async scale: returns a new promise fulfilled on a dedicated GPU fence thread
 * (not the thread pool). Optional `group` for structured concurrency —
 * registers the promise when non-null. ResultType is mandatory and copied.
 * Returns null if descriptor validation, group registration, allocation, or
 * Vulkan submission fails. The caller must keep Elements alive through
 * completion. Submission and completion are thread-safe. */
YonaTaskRef YonaRuntimeGpuVulkanFloat64BufferScaleAsync(
    double *Elements, uint32_t Count, double Scale,
    const YonaTypeDescriptor *ResultType, YonaTaskGroupRef Group);

/* Async mul2: returns a new promise fulfilled on a dedicated GPU fence thread
 * (not the thread pool). Optional `group` for structured concurrency —
 * registers the promise when non-null. Ownership, failures, and thread-safety
 * match the scale operation. */
YonaTaskRef YonaRuntimeGpuVulkanFloat64BufferMultiply2Async(
    double *Elements, uint32_t Count, const YonaTypeDescriptor *ResultType,
    YonaTaskGroupRef Group);

/* Host-visible Vulkan-mapped float staging (persistent map). On success returns
 * 0, sets *out_host to the mapped double[count] (zeroed), and *out_opaque to a
 * handle for YonaRuntimeGpuVulkanFreePinnedFloats. Returns -1 when Vulkan is
 * unavailable or ctx_init fails (caller should fall back to malloc). Set
 * YONA_GPU_PINNED_HOST_MALLOC=1 in the Std\Gpu allocator to skip this path. */
int YonaRuntimeGpuVulkanAllocatePinnedFloats(int64_t Count, double **OutHost,
                                             void **OutOpaque);

/* Release a handle from YonaRuntimeGpuVulkanAllocatePinnedFloats (no-op on
 * NULL). */
void YonaRuntimeGpuVulkanFreePinnedFloats(void *Opaque);

#ifdef __cplusplus
}
#endif

#endif /* YONA_RUNTIME_GPU_API_H */
