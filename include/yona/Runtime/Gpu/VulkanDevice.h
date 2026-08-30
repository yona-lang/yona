/*
 * Optional Vulkan compute device (P1). All entry points are resolved at runtime
 * from the platform loader — no link-time dependency on vulkan-1 / libvulkan.
 */
#ifndef YONA_RUNTIME_GPU_VULKANDEVICE_H
#define YONA_RUNTIME_GPU_VULKANDEVICE_H

#include "yona/Runtime/Gpu/BuildConfig.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Open the platform Vulkan loader (`vulkan-1.dll`, `libvulkan.so.1`,
 * `libvulkan.1.dylib`, or `libMoltenVK.dylib`). On macOS, searches
 * `VULKAN_SDK`, `HOMEBREW_PREFIX`, and CMake-recorded lib/ICD paths before
 * bare dylib names, and hints `VK_ICD_FILENAMES` at a discovered MoltenVK
 * ICD json when unset. Returns a LoadLibrary/dlopen handle, or NULL. Caller
 * must FreeLibrary/dlclose.
 */
void *YonaRuntimeGpuVulkanOpenLoader(void);

/** 1 if the platform loader is loadable and not disabled by
 * YONA_GPU_DISABLE_VULKAN. */
int YonaRuntimeGpuVulkanLoaderAvailable(void);

/** Negative = not built / disabled / loader error; 0 = device + compute queue
 * ready. */
int YonaRuntimeGpuVulkanDeviceTryInitialize(void);

/** 1 after successful initialization; always 0 without Vulkan headers.
 */
int YonaRuntimeGpuVulkanDeviceIsReady(void);

/**
 * Release device and instance; safe to call multiple times and
 * concurrently.
 * Shutdown waits for active integer compute operations before
 * destroying the
 * device.
 */
void YonaRuntimeGpuVulkanDeviceShutdown(void);

/**
 * Status string for Std\Gpu.vulkanStatus when the runtime was built with
 * YONA_GPU_VULKAN_ENABLED: refines loader-only vs device-ready. When built
 *
 * without, the caller should report vulkan-loader / vulkan-unavailable without
 * probing the device layer.
 */
const char *YonaRuntimeGpuVulkanDeviceStatusName(void);

/**
 * 1 if the logical device was created with
 * VkPhysicalDeviceFeatures::shaderInt64 (i64 SSBO kernels). When 0, IntArray
 * map/reduce may still run via i32 kernels if values fit. Always 0 when Vulkan
 * is not compiled in.
 */
int YonaRuntimeGpuVulkanDeviceHasShaderInt64(void);

/**
 * 1 if the selected physical device exposes timeline semaphores: Vulkan 1.2+
 * reports timelineSemaphore via vkGetPhysicalDeviceFeatures2/KHR with
 * VkPhysicalDeviceVulkan12Features, or the device enumerates extension
 * VK_KHR_timeline_semaphore (covers many 1.0/1.1 stacks where the capability is
 * extension-only). When the device is created with
 * **VK_KHR_timeline_semaphore** + **VK_KHR_synchronization2**, async float work
 * may use **vkQueueSubmit2** + timeline waits (see
 * **`src/Runtime/Gpu/Stub.c`**,
 * **`YONA_GPU_ASYNC_TIMELINE`**). Otherwise submit paths use **fences**.
 */
int YonaRuntimeGpuVulkanDeviceHasTimelineSemaphore(void);

/** 1 if the logical device was created with VK_KHR_synchronization2 enabled. */
int YonaRuntimeGpuVulkanDeviceHasSynchronization2(void);

/**
 * Short human-readable hint for the last failed device init, opt-in GPU kernel
 * attempt, or fence wait on async float compute (empty if none recorded).
 *
 * Thread-safe. The returned pointer addresses a thread-local snapshot and
 *
 * remains valid until the next call on the same thread.
 */
const char *YonaRuntimeGpuVulkanDeviceLastNote(void);

/** Overwrites the last note (same buffer as initialization and integer-column
 * paths). Safe to call from the Std\Gpu fence waiter thread after
 * vkWaitForFences fails. */
void YonaRuntimeGpuVulkanDeviceSetLastNote(const char *Msg);

/**
 * 0 = no VkResult classification yet; 1 = out-of-memory class; 2 = device lost;
 * 3 = other failure. Updated by **YonaRuntimeGpuVulkanDeviceNoteResult**
 * alongside **vulkanLastNote**.
 */
int YonaRuntimeGpuVulkanDeviceLastIssueKind(void);

/** Append a **float:** / device-init style note for a Vulkan error code
 * (updates the last issue kind). */
void YonaRuntimeGpuVulkanDeviceNoteResult(const char *Ctx, int32_t ResultCode);

/** Clears cached Std\Gpu.hasGpu probe after device shutdown or reset. */
void YonaRuntimeGpuVulkanInvalidateCapabilityCache(void);

#if YONA_GPU_VULKAN_ENABLED
/**
 * Opt-in GPU mapAdd (see YONA_GPU_VULKAN_MAPADD). Returns 1 and sets *out to a
 * new int array on success; otherwise 0 and *out unchanged.
 */
int YonaRuntimeGpuVulkanTryMapAddInt64(int64_t Delta, int64_t *Arr,
                                       int64_t **Out);

/** Opt-in GPU mapMul; same env pattern as mapAdd (YONA_GPU_VULKAN_MAPMUL /
 * _COMPUTE). */
int YonaRuntimeGpuVulkanTryMapMulInt64(int64_t Factor, int64_t *Arr,
                                       int64_t **Out);

/** Opt-in GPU mapSquare (`x * x`); same env pattern as mapMul. */
int YonaRuntimeGpuVulkanTryMapSquareInt64(int64_t *Arr, int64_t **Out);

/** Opt-in GPU reduceSum (block reduce + host tail); YONA_GPU_VULKAN_REDUCE /
 * _COMPUTE. */
int YonaRuntimeGpuVulkanTryReduceSumInt64(int64_t *Arr, int64_t *OutSum);

/**
 * Opt-in GPU filterGreaterThan (mark + inclusive GPU prefix + exclusive
 * conversion + scatter). Set YONA_GPU_VULKAN_FILTER=1 or
 * YONA_GPU_VULKAN_COMPUTE=1. Optional YONA_GPU_VULKAN_FILTER_MIN_LEN /
 * YONA_GPU_VULKAN_MIN_LEN (same pattern as map/reduce).
 */
int YonaRuntimeGpuVulkanTryFilterGreaterThanInt64(int64_t Threshold,
                                                  int64_t *Arr, int64_t **Out);

/** Opt-in GPU filterLessThan — same mark/prefix/scatter as greater-than with an
 * LT mark shader. */
int YonaRuntimeGpuVulkanTryFilterLessThanInt64(int64_t Threshold, int64_t *Arr,
                                               int64_t **Out);

/** Opt-in multi-kernel map→…→reduceSum in one command buffer (sync2 barriers +
 * timeline submit when the device was created with KHR synchronization2). */
int YonaRuntimeGpuVulkanTryMapReduceGraphInt64(int64_t *Stages, int64_t *Arr,
                                               int64_t *OutSum);
#endif

#ifdef __cplusplus
}
#endif

#endif /* YONA_RUNTIME_GPU_VULKANDEVICE_H */
