/*
 * Optional Vulkan compute device (P1). All entry points are resolved at runtime
 * from the platform loader — no link-time dependency on vulkan-1 / libvulkan.
 */
#ifndef YONA_RUNTIME_GPU_VULKAN_DEVICE_H
#define YONA_RUNTIME_GPU_VULKAN_DEVICE_H

#include "yona/runtime/gpu_build_config.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Negative = not built / disabled / loader error; 0 = device + compute queue ready. */
int yona_gpu_vulkan_device_try_init(void);

/** 1 after successful try_init; always 0 when compiled without Vulkan headers. */
int yona_gpu_vulkan_device_ready(void);

/** Release device and instance; safe to call multiple times. */
void yona_gpu_vulkan_device_shutdown(void);

/**
 * Status string for Std\GPU.vulkanStatus when the runtime was built with
 * YONA_COMPILE_GPU_VULKAN: refines loader-only vs device-ready. When built
 * without, the caller should report vulkan-loader / vulkan-unavailable without
 * probing the device layer.
 */
const char* yona_gpu_vulkan_device_status_name(void);

/**
 * 1 if the logical device was created with VkPhysicalDeviceFeatures::shaderInt64
 * (required for embedded int64 SSBO compute). Always 0 when Vulkan is not compiled in.
 */
int yona_gpu_vulkan_device_shader_int64(void);

/**
 * 1 if the selected physical device exposes timeline semaphores: Vulkan 1.2+
 * reports timelineSemaphore via vkGetPhysicalDeviceFeatures2/KHR with
 * VkPhysicalDeviceVulkan12Features, **or** the device enumerates extension
 * VK_KHR_timeline_semaphore (covers many 1.0/1.1 stacks where the capability is extension-only).
 * Always 0 without a Vulkan build or before successful try_init. Not yet used for submission
 * (per-fence waits today); exposed for diagnostics and future timeline batching.
 */
int yona_gpu_vulkan_device_timeline_semaphore(void);

/**
 * Short human-readable hint for the last failed device init, opt-in GPU kernel
 * attempt, or fence wait on async float compute (empty if none recorded).
 * Thread-safe only if all callers hold the same external lock as try_init; for
 * tests and CLI this is single-threaded.
 */
const char* yona_gpu_vulkan_device_last_note(void);

/** Overwrites last_note (same buffer as failures from try_init / int column paths).
 * Safe to call from the Std\GPU fence waiter thread after vkWaitForFences fails. */
void yona_gpu_vulkan_device_set_last_note(const char* msg);

/** Clears cached Std\GPU.hasGpu probe after device shutdown or reset. */
void yona_gpu_vulkan_invalidate_capability_cache(void);

#if YONA_GPU_VULKAN_ENABLED
/**
 * Opt-in GPU mapAdd (see YONA_GPU_VULKAN_MAPADD). Returns 1 and sets *out to a
 * new int array on success; otherwise 0 and *out unchanged.
 */
int yona_gpu_vulkan_try_map_add_int64(int64_t delta, int64_t* arr, int64_t** out);

/** Opt-in GPU mapMul; same env pattern as mapAdd (YONA_GPU_VULKAN_MAPMUL / _COMPUTE). */
int yona_gpu_vulkan_try_map_mul_int64(int64_t factor, int64_t* arr, int64_t** out);

/** Opt-in GPU reduceSum (block reduce + host tail); YONA_GPU_VULKAN_REDUCE / _COMPUTE. */
int yona_gpu_vulkan_try_reduce_sum_int64(int64_t* arr, int64_t* out_sum);

/**
 * Opt-in GPU filterGreaterThan (mark + inclusive GPU prefix + exclusive conversion +
 * scatter). Set YONA_GPU_VULKAN_FILTER=1 or YONA_GPU_VULKAN_COMPUTE=1. Optional
 * YONA_GPU_VULKAN_FILTER_MIN_LEN / YONA_GPU_VULKAN_MIN_LEN (same pattern as map/reduce).
 * Set YONA_GPU_VULKAN_FILTER_CPU_PREFIX=1 to force the legacy host-side exclusive prefix.
 */
int yona_gpu_vulkan_try_filter_greater_than_int64(int64_t threshold, int64_t* arr, int64_t** out);
#endif

#ifdef __cplusplus
}
#endif

#endif /* YONA_RUNTIME_GPU_VULKAN_DEVICE_H */
