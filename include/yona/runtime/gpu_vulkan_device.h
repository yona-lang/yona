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
 * Short human-readable hint for the last failed device init or mapAdd attempt
 * in this process (empty string if none recorded). Thread-safe only if all
 * callers hold the same external lock as try_init; for tests and CLI this is
 * single-threaded.
 */
const char* yona_gpu_vulkan_device_last_note(void);

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
#endif

#ifdef __cplusplus
}
#endif

#endif /* YONA_RUNTIME_GPU_VULKAN_DEVICE_H */
