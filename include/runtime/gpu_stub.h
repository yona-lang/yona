#pragma once

#include <stdint.h>

#include "runtime/async_runtime.h"

/// C ABI for `Std\GPU` — **discovery only** until the public compute API lands;
/// see docs/design-gpu-async.md (`mapGPU`, `Promise`, fences, pinned buffers).
/// Without CMake Vulkan (any platform), probes return zeros / false.

#ifdef __cplusplus
extern "C" {
#endif

/* Yona nullary externs compile as `() -> T` → one `UNIT` (i64) at the LLVM call site. */
int64_t yona_Std_GPU__available(int64_t unit);
int64_t yona_Std_GPU__apiVersion(int64_t unit);
int64_t yona_Std_GPU__physicalDeviceCount(int64_t unit);

/* Yona `extern native floatArrayMul2Async : FloatArray -> Int` — in-place multiply by 2 on GPU
 * when available (`scale = 2.0` shader push constant); promise completes with 0 on success
 * or a negative error code. */
yona_promise_t* yona_Std_GPU__floatArrayMul2Async(double* float_array);

/* Yona `extern native floatArrayScaleAsync : Float -> FloatArray -> Int`. */
yona_promise_t* yona_Std_GPU__floatArrayScaleAsync(double scale, double* float_array);

/* C-only / tests: lazy `VkDevice`, compute `VkQueue`, `VkCommandPool`, and a fixed
 * nop compute pipeline (`src/runtime/gpu_nop.comp` → embedded SPIR-V). Returns 0
 * on success; -1 no Vulkan in this build; -2 no compute GPU; -3 instance; -4 device;
 * -5 command pool; -6 shader module; -7 pipeline layout; -8 compute pipeline.
 * Idempotent while context is live. Pair with `yona_gpu_vulkan_ctx_shutdown` in
 * tests or teardown (optional for processes). */
int yona_gpu_vulkan_ctx_init(void);
void yona_gpu_vulkan_ctx_shutdown(void);

/* One-shot nop `vkCmdDispatch(1,1,1)` + `vkWaitForFences` — **not** for Yona async
 * workers (see design-gpu-async.md); tests / manual driver checks only.
 * Call after `yona_gpu_vulkan_ctx_init`. Returns 0; -1 no Vulkan build; -9 context
 * not initialized / incomplete; -10.. dispatch path failure. */
int yona_gpu_vulkan_dispatch_nop_once(void);

/* Synchronous FloatArray-style payload: `elements` points at `count` contiguous
 * doubles (Yona layout); each element is multiplied by `scale` on the GPU and written
 * back. Requires `shaderFloat64` + `yona_gpu_vulkan_ctx_init`. Returns 0; -1 no
 * Vulkan build; -9 no context; -16 null `elements`; -20 no double shader support;
 * -21 prior f64 pipeline init failed; -22 f64 pipeline setup; -30..-42 resource /
 * dispatch failures (see gpu_stub.c). */
int yona_gpu_vulkan_float64_buffer_scale_inplace(double* elements, uint32_t count, double scale);

/* Same as scale with factor 2.0. */
int yona_gpu_vulkan_float64_buffer_mul2_inplace(double* elements, uint32_t count);

/* Async scale: returns a new promise fulfilled on a dedicated GPU fence thread (not
 * the thread pool). Optional `group` for structured concurrency — registers the
 * promise when non-NULL. Returns NULL if promise allocation fails or without Vulkan. */
yona_promise_t* yona_gpu_vulkan_float64_buffer_scale_async(double* elements, uint32_t count, double scale,
                                                             yona_task_group_t* group);

/* Async mul2: returns a new promise fulfilled on a dedicated GPU fence thread (not
 * the thread pool). Optional `group` for structured concurrency — registers the
 * promise when non-NULL. Returns NULL if promise allocation fails or without Vulkan. */
yona_promise_t* yona_gpu_vulkan_float64_buffer_mul2_async(double* elements, uint32_t count,
                                                          yona_task_group_t* group);

#ifdef __cplusplus
}
#endif
