/* ===== Std\GPU Vulkan backend scaffold =====
 *
 * Without YONA_COMPILE_GPU_VULKAN: no Vulkan headers or import library; detects
 * whether a Vulkan loader is loadable (dlopen/LoadLibrary) for capability
 * reporting.
 *
 * With YONA_COMPILE_GPU_VULKAN (see include/yona/runtime/gpu_build_config.h):
 * links the platform Vulkan loader and may reference loader entry points. Actual
 * device/compute is implemented in follow-up work.
 */

#include "yona/runtime/gpu_build_config.h"
#include "yona/runtime/gpu_vulkan_device.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <stdlib.h>
#include <string.h>

int yona_gpu_vulkan_loader_available(void) {
    const char* disabled = getenv("YONA_GPU_DISABLE_VULKAN");
    if (disabled && disabled[0] && strcmp(disabled, "0") != 0)
        return 0;

#if defined(_WIN32)
    HMODULE lib = LoadLibraryA("vulkan-1.dll");
    if (!lib) return 0;
    FreeLibrary(lib);
    return 1;
#else
    void* lib = dlopen("libvulkan.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (!lib) lib = dlopen("libvulkan.so", RTLD_LAZY | RTLD_LOCAL);
    if (!lib) return 0;
    dlclose(lib);
    return 1;
#endif
}

const char* yona_gpu_vulkan_status_name(void) {
    const char* disabled = getenv("YONA_GPU_DISABLE_VULKAN");
    if (disabled && disabled[0] && strcmp(disabled, "0") != 0)
        return "vulkan-unavailable";
    if (!yona_gpu_vulkan_loader_available())
        return "vulkan-unavailable";
#if YONA_GPU_VULKAN_ENABLED
    return yona_gpu_vulkan_device_status_name();
#else
    return "vulkan-loader";
#endif
}
