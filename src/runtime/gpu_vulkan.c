/* ===== Std\GPU Vulkan backend scaffold =====
 *
 * Without YONA_COMPILE_GPU_VULKAN: no Vulkan headers or import library; detects
 * whether a Vulkan loader is loadable (dlopen/LoadLibrary) for capability
 * reporting.
 *
 * With YONA_COMPILE_GPU_VULKAN (see include/yona/runtime/gpu_build_config.h):
 * device/compute is runtime-dispatched via vkGetInstanceProcAddr. Loader names
 * are platform-specific (ELF .so, Mach-O dylib / MoltenVK, Win32 vulkan-1).
 * Search dirs come from VULKAN_SDK, HOMEBREW_PREFIX, and CMake-recorded paths
 * — never hardcoded install prefixes.
 */

#include "yona/runtime/gpu_build_config.h"
#include "yona/runtime/gpu_vulkan_device.h"

#if defined(__has_include)
#if __has_include("yona_vulkan_link_cfg.h")
#include "yona_vulkan_link_cfg.h"
#endif
#endif

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>
#endif
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
static void* yona_vk_dlopen_path(const char* path) {
    if (!path || !path[0]) return NULL;
    /* GLOBAL so a portability ICD (MoltenVK) can resolve loader symbols. */
    return dlopen(path, RTLD_NOW | RTLD_GLOBAL);
}

static void* yona_vk_dlopen_join(const char* dir, const char* base) {
    char path[512];
    if (!dir || !dir[0] || !base || !base[0]) return NULL;
    int n = snprintf(path, sizeof path, "%s/%s", dir, base);
    if (n < 0 || (size_t)n >= sizeof path) return NULL;
    return yona_vk_dlopen_path(path);
}
#endif

#if defined(__APPLE__)
static const char* const yona_vk_macos_basenames[] = {
    "libvulkan.1.dylib",
    "libvulkan.dylib",
    "libMoltenVK.dylib",
};

static void yona_vk_try_set_icd(const char* path) {
    if (!path || !path[0]) return;
    if (access(path, R_OK) != 0) return;
    setenv("VK_ICD_FILENAMES", path, 0);
    setenv("VK_DRIVER_FILES", path, 0);
}

/* ICD json from CMake, VULKAN_SDK, or HOMEBREW_PREFIX. Do not override the user. */
static void yona_gpu_vulkan_hint_macos_icd(void) {
    if (getenv("VK_ICD_FILENAMES") || getenv("VK_DRIVER_FILES")) return;

#if defined(YONA_HAVE_CONFIGURED_VULKAN_ICD_JSON) && YONA_HAVE_CONFIGURED_VULKAN_ICD_JSON
    yona_vk_try_set_icd(YONA_CONFIGURED_VULKAN_ICD_JSON);
    if (getenv("VK_ICD_FILENAMES")) return;
#endif

    static const char* const rel[] = {
        "/etc/vulkan/icd.d/MoltenVK_icd.json",
        "/share/vulkan/icd.d/MoltenVK_icd.json",
    };
    const char* prefixes[2];
    size_t n = 0;
    const char* sdk = getenv("VULKAN_SDK");
    const char* brew = getenv("HOMEBREW_PREFIX");
    if (sdk && sdk[0]) prefixes[n++] = sdk;
    if (brew && brew[0]) prefixes[n++] = brew;

    char buf[512];
    for (size_t i = 0; i < n; i++) {
        for (size_t j = 0; j < sizeof(rel) / sizeof(rel[0]); j++) {
            int w = snprintf(buf, sizeof buf, "%s%s", prefixes[i], rel[j]);
            if (w < 0 || (size_t)w >= sizeof buf) continue;
            yona_vk_try_set_icd(buf);
            if (getenv("VK_ICD_FILENAMES")) return;
        }
    }
}

static int yona_vk_push_lib_dir(const char** dirs, size_t* nd, size_t cap, const char* dir) {
    if (!dir || !dir[0] || *nd >= cap) return 0;
    dirs[(*nd)++] = dir;
    return 1;
}

static void* yona_gpu_vulkan_open_loader_macos(void) {
    yona_gpu_vulkan_hint_macos_icd();

    char sdk_lib[512];
    char brew_lib[512];
    const char* dirs[4];
    size_t nd = 0;

#if defined(YONA_HAVE_CONFIGURED_VULKAN_LIB_DIR) && YONA_HAVE_CONFIGURED_VULKAN_LIB_DIR
    yona_vk_push_lib_dir(dirs, &nd, 4, YONA_CONFIGURED_VULKAN_LIB_DIR);
#endif
    const char* sdk = getenv("VULKAN_SDK");
    const char* brew = getenv("HOMEBREW_PREFIX");
    if (sdk && sdk[0]) {
        int w = snprintf(sdk_lib, sizeof sdk_lib, "%s/lib", sdk);
        if (w > 0 && (size_t)w < sizeof sdk_lib)
            yona_vk_push_lib_dir(dirs, &nd, 4, sdk_lib);
    }
    if (brew && brew[0]) {
        int w = snprintf(brew_lib, sizeof brew_lib, "%s/lib", brew);
        if (w > 0 && (size_t)w < sizeof brew_lib)
            yona_vk_push_lib_dir(dirs, &nd, 4, brew_lib);
    }

    for (size_t d = 0; d < nd; d++) {
        for (size_t i = 0; i < sizeof(yona_vk_macos_basenames) / sizeof(yona_vk_macos_basenames[0]);
             i++) {
            void* lib = yona_vk_dlopen_join(dirs[d], yona_vk_macos_basenames[i]);
            if (lib) return lib;
        }
    }
    for (size_t i = 0; i < sizeof(yona_vk_macos_basenames) / sizeof(yona_vk_macos_basenames[0]); i++) {
        void* lib = yona_vk_dlopen_path(yona_vk_macos_basenames[i]);
        if (lib) return lib;
    }
    return NULL;
}
#endif

void* yona_gpu_vulkan_open_loader(void) {
#if defined(_WIN32)
    return (void*)LoadLibraryA("vulkan-1.dll");
#elif defined(__APPLE__)
    return yona_gpu_vulkan_open_loader_macos();
#else
    void* lib = yona_vk_dlopen_path("libvulkan.so.1");
    if (!lib) lib = yona_vk_dlopen_path("libvulkan.so");
    return lib;
#endif
}

int yona_gpu_vulkan_loader_available(void) {
    const char* disabled = getenv("YONA_GPU_DISABLE_VULKAN");
    if (disabled && disabled[0] && strcmp(disabled, "0") != 0)
        return 0;

    void* lib = yona_gpu_vulkan_open_loader();
    if (!lib) return 0;
#if defined(_WIN32)
    FreeLibrary((HMODULE)lib);
#else
    dlclose(lib);
#endif
    return 1;
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
