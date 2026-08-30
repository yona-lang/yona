/* ===== Std\Gpu Vulkan backend scaffold =====
 *
 * Without YONA_GPU_VULKAN_ENABLED: no Vulkan headers or import library;
 * detects
 * whether a Vulkan loader is loadable (dlopen/LoadLibrary) for
 * capability reporting.
 *
 * With YONA_GPU_VULKAN_ENABLED (see
 *
 * include/yona/Runtime/Gpu/BuildConfig.h):
 * device/compute is
 * runtime-dispatched via vkGetInstanceProcAddr. Loader names are
 * platform-specific (ELF .so, Mach-O dylib / MoltenVK, Win32 vulkan-1). Search
 * dirs come from VULKAN_SDK, HOMEBREW_PREFIX, and CMake-recorded paths — never
 * hardcoded install prefixes.
 */

#include "yona/Runtime/Gpu/BuildConfig.h"
#include "yona/Runtime/Gpu/VulkanDevice.h"

#if defined(__has_include)
#if __has_include("yona/Runtime/Generated/VulkanLinkConfig.h")
#include "yona/Runtime/Generated/VulkanLinkConfig.h"
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
static void *yonaVulkanOpenDynamicLibrary(const char *Path) {
  if (!Path || !Path[0])
    return NULL;
  /* GLOBAL so a portability ICD (MoltenVK) can resolve loader symbols. */
  return dlopen(Path, RTLD_NOW | RTLD_GLOBAL);
}

static void *yonaVulkanOpenDynamicLibraryFromDirectory(const char *Directory,
                                                       const char *BaseName) {
  char Path[512];
  if (!Directory || !Directory[0] || !BaseName || !BaseName[0])
    return NULL;
  int Written = snprintf(Path, sizeof Path, "%s/%s", Directory, BaseName);
  if (Written < 0 || (size_t)Written >= sizeof Path)
    return NULL;
  return yonaVulkanOpenDynamicLibrary(Path);
}
#endif

#if defined(__APPLE__)
static const char *const YonaVulkanMacOsBaseNames[] = {
    "libvulkan.1.dylib",
    "libvulkan.dylib",
    "libMoltenVK.dylib",
};

static void yonaVulkanTrySetIcd(const char *Path) {
  if (!Path || !Path[0])
    return;
  if (access(Path, R_OK) != 0)
    return;
  setenv("VK_ICD_FILENAMES", Path, 0);
  setenv("VK_DRIVER_FILES", Path, 0);
}

/* ICD json from CMake, VULKAN_SDK, or HOMEBREW_PREFIX. Do not override the
 * user. */
static void YonaRuntimeGpuVulkanHintMacOsIcd(void) {
  if (getenv("VK_ICD_FILENAMES") || getenv("VK_DRIVER_FILES"))
    return;

#if defined(YONA_HAVE_CONFIGURED_VULKAN_ICD_JSON) &&                           \
    YONA_HAVE_CONFIGURED_VULKAN_ICD_JSON
  yonaVulkanTrySetIcd(YONA_CONFIGURED_VULKAN_ICD_JSON);
  if (getenv("VK_ICD_FILENAMES"))
    return;
#endif

  static const char *const RelativePaths[] = {
      "/etc/vulkan/icd.d/MoltenVK_icd.json",
      "/share/vulkan/icd.d/MoltenVK_icd.json",
  };
  const char *Prefixes[2];
  size_t PrefixCount = 0;
  const char *Sdk = getenv("VULKAN_SDK");
  const char *Brew = getenv("HOMEBREW_PREFIX");
  if (Sdk && Sdk[0])
    Prefixes[PrefixCount++] = Sdk;
  if (Brew && Brew[0])
    Prefixes[PrefixCount++] = Brew;

  char Buffer[512];
  for (size_t PrefixIndex = 0; PrefixIndex < PrefixCount; PrefixIndex++) {
    for (size_t RelativeIndex = 0;
         RelativeIndex < sizeof(RelativePaths) / sizeof(RelativePaths[0]);
         RelativeIndex++) {
      int Written =
          snprintf(Buffer, sizeof Buffer, "%s%s", Prefixes[PrefixIndex],
                   RelativePaths[RelativeIndex]);
      if (Written < 0 || (size_t)Written >= sizeof Buffer)
        continue;
      yonaVulkanTrySetIcd(Buffer);
      if (getenv("VK_ICD_FILENAMES"))
        return;
    }
  }
}

static int yonaVulkanPushLibraryDirectory(const char **Directories,
                                          size_t *DirectoryCount,
                                          size_t Capacity,
                                          const char *Directory) {
  if (!Directory || !Directory[0] || *DirectoryCount >= Capacity)
    return 0;
  Directories[(*DirectoryCount)++] = Directory;
  return 1;
}

static void *YonaRuntimeGpuVulkanOpenLoaderMacOs(void) {
  YonaRuntimeGpuVulkanHintMacOsIcd();

  char SdkLibraryPath[512];
  char BrewLibraryPath[512];
  const char *Directories[4];
  size_t DirectoryCount = 0;

#if defined(YONA_HAVE_CONFIGURED_VULKAN_LIB_DIR) &&                            \
    YONA_HAVE_CONFIGURED_VULKAN_LIB_DIR
  yonaVulkanPushLibraryDirectory(Directories, &DirectoryCount, 4,
                                 YONA_CONFIGURED_VULKAN_LIB_DIR);
#endif
  const char *Sdk = getenv("VULKAN_SDK");
  const char *Brew = getenv("HOMEBREW_PREFIX");
  if (Sdk && Sdk[0]) {
    int Written =
        snprintf(SdkLibraryPath, sizeof SdkLibraryPath, "%s/lib", Sdk);
    if (Written > 0 && (size_t)Written < sizeof SdkLibraryPath)
      yonaVulkanPushLibraryDirectory(Directories, &DirectoryCount, 4,
                                     SdkLibraryPath);
  }
  if (Brew && Brew[0]) {
    int Written =
        snprintf(BrewLibraryPath, sizeof BrewLibraryPath, "%s/lib", Brew);
    if (Written > 0 && (size_t)Written < sizeof BrewLibraryPath)
      yonaVulkanPushLibraryDirectory(Directories, &DirectoryCount, 4,
                                     BrewLibraryPath);
  }

  for (size_t DirectoryIndex = 0; DirectoryIndex < DirectoryCount;
       DirectoryIndex++) {
    for (size_t BaseNameIndex = 0;
         BaseNameIndex <
         sizeof(YonaVulkanMacOsBaseNames) / sizeof(YonaVulkanMacOsBaseNames[0]);
         BaseNameIndex++) {
      void *Library = yonaVulkanOpenDynamicLibraryFromDirectory(
          Directories[DirectoryIndex], YonaVulkanMacOsBaseNames[BaseNameIndex]);
      if (Library)
        return Library;
    }
  }
  for (size_t BaseNameIndex = 0;
       BaseNameIndex <
       sizeof(YonaVulkanMacOsBaseNames) / sizeof(YonaVulkanMacOsBaseNames[0]);
       BaseNameIndex++) {
    void *Library =
        yonaVulkanOpenDynamicLibrary(YonaVulkanMacOsBaseNames[BaseNameIndex]);
    if (Library)
      return Library;
  }
  return NULL;
}
#endif

void *YonaRuntimeGpuVulkanOpenLoader(void) {
#if defined(_WIN32)
  return (void *)LoadLibraryA("vulkan-1.dll");
#elif defined(__APPLE__)
  return YonaRuntimeGpuVulkanOpenLoaderMacOs();
#else
  void *Library = yonaVulkanOpenDynamicLibrary("libvulkan.so.1");
  if (!Library)
    Library = yonaVulkanOpenDynamicLibrary("libvulkan.so");
  return Library;
#endif
}

int YonaRuntimeGpuVulkanLoaderAvailable(void) {
  const char *Disabled = getenv("YONA_GPU_DISABLE_VULKAN");
  if (Disabled && Disabled[0] && strcmp(Disabled, "0") != 0)
    return 0;

  void *Lib = YonaRuntimeGpuVulkanOpenLoader();
  if (!Lib)
    return 0;
#if defined(_WIN32)
  FreeLibrary((HMODULE)Lib);
#else
  dlclose(Lib);
#endif
  return 1;
}

const char *YonaRuntimeGpuVulkanStatusName(void) {
  const char *Disabled = getenv("YONA_GPU_DISABLE_VULKAN");
  if (Disabled && Disabled[0] && strcmp(Disabled, "0") != 0)
    return "vulkan-unavailable";
  if (!YonaRuntimeGpuVulkanLoaderAvailable())
    return "vulkan-unavailable";
#if YONA_GPU_VULKAN_ENABLED
  return YonaRuntimeGpuVulkanDeviceStatusName();
#else
  return "vulkan-loader";
#endif
}
