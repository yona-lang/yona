/* ===== Vulkan device (compute queue) — runtime-loaded entry points =====
 *
 * No link dependency on the Vulkan loader: vulkan-1.dll / libvulkan.so.1 /
 * libvulkan.1.dylib / libMoltenVK.dylib is opened with LoadLibrary/dlopen
 * and all used symbols are resolved dynamically.
 * When YONA_GPU_VULKAN_ENABLED is 0, this TU provides no-op stubs so default
 * packages and CI builds stay free of Khronos headers.
 */

#include "yona/Runtime/Gpu/VulkanDevice.h"

#include "yona/Runtime/Gpu/BuildConfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#include <pthread.h>
#endif

#if !YONA_GPU_VULKAN_ENABLED

int YonaRuntimeGpuVulkanDeviceTryInitialize(void) { return -1; }

int YonaRuntimeGpuVulkanDeviceIsReady(void) { return 0; }

void YonaRuntimeGpuVulkanDeviceShutdown(void) {
  YonaRuntimeGpuVulkanInvalidateCapabilityCache();
}

const char *YonaRuntimeGpuVulkanDeviceStatusName(void) {
  return "vulkan-loader";
}

int YonaRuntimeGpuVulkanDeviceHasShaderInt64(void) { return 0; }

const char *YonaRuntimeGpuVulkanDeviceLastNote(void) { return ""; }

void YonaRuntimeGpuVulkanDeviceSetLastNote(const char *Msg) { (void)Msg; }

int YonaRuntimeGpuVulkanDeviceHasTimelineSemaphore(void) { return 0; }

int YonaRuntimeGpuVulkanDeviceHasSynchronization2(void) { return 0; }

int YonaRuntimeGpuVulkanDeviceLastIssueKind(void) { return 0; }

void YonaRuntimeGpuVulkanDeviceNoteResult(const char *Ctx, int32_t ResultCode) {
  (void)Ctx;
  (void)ResultCode;
}

#else /* YONA_GPU_VULKAN_ENABLED */

#include "Runtime/Gpu/VulkanInternal.h"

#ifndef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
#define VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME                          \
  "VK_KHR_portability_enumeration"
#endif
#ifndef VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
#define YONA_VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR 0x00000001u
#endif
#ifndef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
#define YONA_VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME                          \
  "VK_KHR_portability_subset"
#endif

#if defined(_WIN32)
static SRWLOCK YonaVulkanDeviceLock = SRWLOCK_INIT;
static CONDITION_VARIABLE YonaVulkanDeviceIdle = CONDITION_VARIABLE_INIT;
static SRWLOCK YonaVulkanNoteLock = SRWLOCK_INIT;

#define YONA_VKDEV_LOCK() AcquireSRWLockExclusive(&YonaVulkanDeviceLock)
#define YONA_VKDEV_UNLOCK() ReleaseSRWLockExclusive(&YonaVulkanDeviceLock)
#define YONA_VKDEV_WAIT()                                                      \
  SleepConditionVariableSRW(&YonaVulkanDeviceIdle, &YonaVulkanDeviceLock,      \
                            INFINITE, 0)
#define YONA_VKDEV_WAKE_ALL() WakeAllConditionVariable(&YonaVulkanDeviceIdle)
#define YONA_VK_NOTE_LOCK() AcquireSRWLockExclusive(&YonaVulkanNoteLock)
#define YONA_VK_NOTE_UNLOCK() ReleaseSRWLockExclusive(&YonaVulkanNoteLock)
#define YONA_VK_THREAD_LOCAL __declspec(thread)

#else /* !_WIN32 */

static pthread_mutex_t YonaVulkanDeviceMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t YonaVulkanDeviceIdle = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t YonaVulkanNoteMutex = PTHREAD_MUTEX_INITIALIZER;
#define YONA_VKDEV_LOCK() pthread_mutex_lock(&YonaVulkanDeviceMutex)
#define YONA_VKDEV_UNLOCK() pthread_mutex_unlock(&YonaVulkanDeviceMutex)
#define YONA_VKDEV_WAIT()                                                      \
  pthread_cond_wait(&YonaVulkanDeviceIdle, &YonaVulkanDeviceMutex)
#define YONA_VKDEV_WAKE_ALL() pthread_cond_broadcast(&YonaVulkanDeviceIdle)
#define YONA_VK_NOTE_LOCK() pthread_mutex_lock(&YonaVulkanNoteMutex)
#define YONA_VK_NOTE_UNLOCK() pthread_mutex_unlock(&YonaVulkanNoteMutex)
#define YONA_VK_THREAD_LOCAL _Thread_local

#endif

enum YonaVulkanDeviceLifecycleKind {
  YonaVulkanDeviceUntried = 0,
  YonaVulkanDeviceReady = 1,
  YonaVulkanDeviceFailed = 2,
};

static enum YonaVulkanDeviceLifecycleKind YonaVulkanDeviceLifecycle =
    YonaVulkanDeviceUntried;
static size_t YonaVulkanDeviceActiveOperations;
static int YonaVulkanDeviceShuttingDown;

#if defined(_WIN32)
static HMODULE YonaVulkanDynamicLibrary;
#else
static void *YonaVulkanDynamicLibrary;
#endif

static PFN_vkGetInstanceProcAddr YonaVulkanGetInstanceProcAddress;
static PFN_vkCreateInstance YonaVulkanCreateInstance;
static PFN_vkDestroyInstance YonaVulkanDestroyInstance;
static PFN_vkEnumeratePhysicalDevices YonaVulkanEnumeratePhysicalDevices;
static PFN_vkGetPhysicalDeviceQueueFamilyProperties
    YonaVulkanGetPhysicalDeviceQueueFamilyProperties;
static PFN_vkCreateDevice YonaVulkanCreateDevice;
static PFN_vkDestroyDevice YonaVulkanDestroyDevice;
static PFN_vkGetDeviceQueue YonaVulkanGetDeviceQueue;
PFN_vkGetDeviceProcAddr YonaVulkanGetDeviceProcAddress;
static PFN_vkGetPhysicalDeviceFeatures YonaVulkanGetPhysicalDeviceFeatures;
PFN_vkGetPhysicalDeviceMemoryProperties
    YonaVulkanGetPhysicalDeviceMemoryProperties;
static PFN_vkGetPhysicalDeviceProperties YonaVulkanGetPhysicalDeviceProperties;

static VkInstance YonaVulkanInstance = VK_NULL_HANDLE;
VkPhysicalDevice YonaVulkanPhysicalDevice = VK_NULL_HANDLE;
VkDevice YonaVulkanDevice = VK_NULL_HANDLE;
VkQueue YonaVulkanQueue = VK_NULL_HANDLE;
uint32_t YonaVulkanQueueFamily = (uint32_t)-1;
static int YonaVulkanShaderInt64Enabled;
static int YonaVulkanTimelineSemaphoreSupported;
int YonaVulkanSynchronization2Enabled;

char YonaVulkanLastNote[256];
static int YonaVulkanLastIssueKind;

static void yonaVulkanNoteClearUnlocked(void) {
  YonaVulkanLastNote[0] = 0;
  YonaVulkanLastIssueKind = 0;
}

static void yonaVulkanNoteCopyUnlocked(const char *S) {
  if (!S || !S[0]) {
    yonaVulkanNoteClearUnlocked();
    return;
  }
  size_t N = strlen(S);
  if (N >= sizeof(YonaVulkanLastNote))
    N = sizeof(YonaVulkanLastNote) - 1;
  memcpy(YonaVulkanLastNote, S, N);
  YonaVulkanLastNote[N] = 0;
}

void yonaVulkanNoteClear(void) {
  YONA_VK_NOTE_LOCK();
  yonaVulkanNoteClearUnlocked();
  YONA_VK_NOTE_UNLOCK();
}

void yonaVulkanNoteCopy(const char *S) {
  YONA_VK_NOTE_LOCK();
  yonaVulkanNoteCopyUnlocked(S);
  YONA_VK_NOTE_UNLOCK();
}

static int yonaVulkanNoteIsEmpty(void) {
  int IsEmpty;
  YONA_VK_NOTE_LOCK();
  IsEmpty = YonaVulkanLastNote[0] == 0;
  YONA_VK_NOTE_UNLOCK();
  return IsEmpty;
}

void YonaRuntimeGpuVulkanDeviceSetLastNote(const char *Msg) {
  yonaVulkanNoteCopy(Msg);
}

int YonaRuntimeGpuVulkanDeviceLastIssueKind(void) {
  int O;
  YONA_VK_NOTE_LOCK();
  O = YonaVulkanLastIssueKind;
  YONA_VK_NOTE_UNLOCK();
  return O;
}

void YonaRuntimeGpuVulkanDeviceNoteResult(const char *Ctx, int32_t ResultCode) {
  VkResult Vr = (VkResult)ResultCode;
  if (Vr == VK_SUCCESS)
    return;
  int Kind = 3;
  if (Vr == VK_ERROR_OUT_OF_HOST_MEMORY || Vr == VK_ERROR_OUT_OF_DEVICE_MEMORY)
    Kind = 1;
  else if (Vr == VK_ERROR_DEVICE_LOST)
    Kind = 2;
  const char *Hint = "";
  if (Kind == 1)
    Hint = " (OOM)";
  else if (Kind == 2)
    Hint = " (device lost)";
  char Buf[256];
  snprintf(Buf, sizeof(Buf), "%s VkResult %d%s", Ctx ? Ctx : "vk", (int)Vr,
           Hint);
  YONA_VK_NOTE_LOCK();
  YonaVulkanLastIssueKind = Kind;
  yonaVulkanNoteCopyUnlocked(Buf);
  YONA_VK_NOTE_UNLOCK();
}

static void YonaVulkanDynamicLibrary_close(void) {
#if defined(_WIN32)
  if (YonaVulkanDynamicLibrary) {
    FreeLibrary(YonaVulkanDynamicLibrary);
    YonaVulkanDynamicLibrary = NULL;
  }
#else
  if (YonaVulkanDynamicLibrary) {
    dlclose(YonaVulkanDynamicLibrary);
    YonaVulkanDynamicLibrary = NULL;
  }
#endif
}

static void *yonaVulkanLoaderDynamicSymbol(const char *Name) {
#if defined(_WIN32)
  if (YonaVulkanDynamicLibrary)
    return (void *)GetProcAddress((HMODULE)YonaVulkanDynamicLibrary, Name);
  return NULL;
#else
  void *p = NULL;
  if (YonaVulkanDynamicLibrary)
    p = dlsym(YonaVulkanDynamicLibrary, Name);
  if (!p)
    p = dlsym(RTLD_DEFAULT, Name);
  return p;
#endif
}

/* Resolve a loader-level entry (instance == NULL). GIPA can return NULL when
 *
 * the process already linked a different loader image through Vulkan::Vulkan;

 * * fall back to dlsym on the handle and the process namespace.
 */
static void *yonaVulkanResolveLoaderSym(const char *Name) {
  void *P = NULL;
  if (YonaVulkanGetInstanceProcAddress)
    P = (void *)YonaVulkanGetInstanceProcAddress(VK_NULL_HANDLE, Name);
  if (!P)
    P = yonaVulkanLoaderDynamicSymbol(Name);
  return P;
}

static int YonaVulkanDynamicLibrary_open(void) {
#if defined(YONA_HAS_VULKAN) && !defined(__ANDROID__)
  /* The GPU component links the loader used by the capability probe, so this

   * * GIPA shares ICD state with float compute. Still dlopen so shutdown is

   * * uniform. */
  YonaVulkanGetInstanceProcAddress = vkGetInstanceProcAddr;
#endif
#if defined(_WIN32)
  YonaVulkanDynamicLibrary = (HMODULE)YonaRuntimeGpuVulkanOpenLoader();
#else
  YonaVulkanDynamicLibrary = YonaRuntimeGpuVulkanOpenLoader();
#endif
  if (!YonaVulkanGetInstanceProcAddress) {
    if (!YonaVulkanDynamicLibrary)
      return -2;
    YonaVulkanGetInstanceProcAddress =
        (PFN_vkGetInstanceProcAddr)yonaVulkanLoaderDynamicSymbol(
            "vkGetInstanceProcAddr");
  }
  if (!YonaVulkanGetInstanceProcAddress)
    return -2;
  return 0;
}

static void yonaVulkanClearFnPtrs(void) {
  YonaVulkanGetInstanceProcAddress = NULL;
  YonaVulkanCreateInstance = NULL;
  YonaVulkanDestroyInstance = NULL;
  YonaVulkanEnumeratePhysicalDevices = NULL;
  YonaVulkanGetPhysicalDeviceQueueFamilyProperties = NULL;
  YonaVulkanCreateDevice = NULL;
  YonaVulkanDestroyDevice = NULL;
  YonaVulkanGetDeviceQueue = NULL;
  YonaVulkanGetDeviceProcAddress = NULL;
  YonaVulkanGetPhysicalDeviceFeatures = NULL;
  YonaVulkanGetPhysicalDeviceMemoryProperties = NULL;
  YonaVulkanGetPhysicalDeviceProperties = NULL;
}

static int yonaVulkanLoadLoaderSymbols(void) {
  YonaVulkanCreateInstance =
      (PFN_vkCreateInstance)yonaVulkanResolveLoaderSym("vkCreateInstance");
  if (!YonaVulkanCreateInstance)
    return -2;
  YonaVulkanEnumeratePhysicalDevices =
      (PFN_vkEnumeratePhysicalDevices)yonaVulkanResolveLoaderSym(
          "vkEnumeratePhysicalDevices");
  YonaVulkanDestroyInstance =
      (PFN_vkDestroyInstance)yonaVulkanResolveLoaderSym("vkDestroyInstance");
  if (!YonaVulkanEnumeratePhysicalDevices || !YonaVulkanDestroyInstance)
    return -2;
  return 0;
}

static int yonaVulkanLoadInstanceSymbols(VkInstance Inst) {
  PFN_vkGetInstanceProcAddr Gipa = YonaVulkanGetInstanceProcAddress;
  YonaVulkanGetPhysicalDeviceQueueFamilyProperties =
      (PFN_vkGetPhysicalDeviceQueueFamilyProperties)(void *)Gipa(
          Inst, "vkGetPhysicalDeviceQueueFamilyProperties");
  YonaVulkanCreateDevice =
      (PFN_vkCreateDevice)(void *)Gipa(Inst, "vkCreateDevice");
  if (!YonaVulkanGetPhysicalDeviceQueueFamilyProperties ||
      !YonaVulkanCreateDevice)
    return -3;
  YonaVulkanGetDeviceProcAddress =
      (PFN_vkGetDeviceProcAddr)(void *)Gipa(Inst, "vkGetDeviceProcAddr");
  YonaVulkanGetPhysicalDeviceFeatures =
      (PFN_vkGetPhysicalDeviceFeatures)(void *)Gipa(
          Inst, "vkGetPhysicalDeviceFeatures");
  if (!YonaVulkanGetDeviceProcAddress || !YonaVulkanGetPhysicalDeviceFeatures)
    return -3;
  YonaVulkanGetPhysicalDeviceMemoryProperties =
      (PFN_vkGetPhysicalDeviceMemoryProperties)(void *)Gipa(
          Inst, "vkGetPhysicalDeviceMemoryProperties");
  if (!YonaVulkanGetPhysicalDeviceMemoryProperties)
    return -3;
  YonaVulkanGetPhysicalDeviceProperties =
      (PFN_vkGetPhysicalDeviceProperties)(void *)Gipa(
          Inst, "vkGetPhysicalDeviceProperties");
  if (!YonaVulkanGetPhysicalDeviceProperties)
    return -3;
  return 0;
}

static int yonaVulkanLoadDeviceSymbols(VkInstance Inst, VkDevice Dev) {
  PFN_vkGetInstanceProcAddr Gipa = YonaVulkanGetInstanceProcAddress;
  (void)Inst;
  YonaVulkanDestroyDevice =
      (PFN_vkDestroyDevice)(void *)Gipa(Inst, "vkDestroyDevice");
  YonaVulkanGetDeviceQueue =
      (PFN_vkGetDeviceQueue)(void *)Gipa(Inst, "vkGetDeviceQueue");
  if (!YonaVulkanDestroyDevice || !YonaVulkanGetDeviceQueue)
    return -3;
  (void)Dev;
  return 0;
}

static void yonaVulkanDestroyAll(void) {
  if (YonaVulkanDevice != VK_NULL_HANDLE && YonaVulkanGetDeviceProcAddress) {
    PFN_vkDeviceWaitIdle PfnWait =
        (PFN_vkDeviceWaitIdle)(void *)YonaVulkanGetDeviceProcAddress(
            YonaVulkanDevice, "vkDeviceWaitIdle");
    if (PfnWait)
      PfnWait(YonaVulkanDevice);
  }
  yonaVulkanComputeDestroyCachedPipelines();
  if (YonaVulkanDevice != VK_NULL_HANDLE && YonaVulkanDestroyDevice)
    YonaVulkanDestroyDevice(YonaVulkanDevice, NULL);
  YonaVulkanDevice = VK_NULL_HANDLE;
  YonaVulkanQueue = VK_NULL_HANDLE;
  YonaVulkanPhysicalDevice = VK_NULL_HANDLE;
  YonaVulkanQueueFamily = (uint32_t)-1;
  YonaVulkanShaderInt64Enabled = 0;
  YonaVulkanTimelineSemaphoreSupported = 0;
  YonaVulkanSynchronization2Enabled = 0;

  if (YonaVulkanInstance != VK_NULL_HANDLE && YonaVulkanDestroyInstance)
    YonaVulkanDestroyInstance(YonaVulkanInstance, NULL);
  YonaVulkanInstance = VK_NULL_HANDLE;

  yonaVulkanClearFnPtrs();
  YonaVulkanDynamicLibrary_close();
}

static int yonaVulkanPickComputeQueue(VkPhysicalDevice Phys,
                                      uint32_t *OutFamily) {
  uint32_t N = 0;
  YonaVulkanGetPhysicalDeviceQueueFamilyProperties(Phys, &N, NULL);
  if (N == 0)
    return -4;
  VkQueueFamilyProperties *Props = (VkQueueFamilyProperties *)malloc(
      (size_t)N * sizeof(VkQueueFamilyProperties));
  if (!Props)
    return -5;
  YonaVulkanGetPhysicalDeviceQueueFamilyProperties(Phys, &N, Props);
  uint32_t Chosen = (uint32_t)-1;
  for (uint32_t I = 0; I < N; I++) {
    if (Props[I].queueCount > 0 &&
        (Props[I].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
      Chosen = I;
      break;
    }
  }
  free(Props);
  if (Chosen == (uint32_t)-1)
    return -4;
  *OutFamily = Chosen;
  return 0;
}

static void yonaVulkanProbeTimelineSemSupported(void) {
  YonaVulkanTimelineSemaphoreSupported = 0;
  if (YonaVulkanPhysicalDevice == VK_NULL_HANDLE ||
      YonaVulkanInstance == VK_NULL_HANDLE)
    return;

  PFN_vkGetPhysicalDeviceFeatures2 PfnGpdf2 =
      (PFN_vkGetPhysicalDeviceFeatures2)(void *)
          YonaVulkanGetInstanceProcAddress(YonaVulkanInstance,
                                           "vkGetPhysicalDeviceFeatures2");
  if (!PfnGpdf2)
    PfnGpdf2 = (PFN_vkGetPhysicalDeviceFeatures2)(void *)
        YonaVulkanGetInstanceProcAddress(YonaVulkanInstance,
                                         "vkGetPhysicalDeviceFeatures2KHR");
  VkPhysicalDeviceProperties Props;
  YonaVulkanGetPhysicalDeviceProperties(YonaVulkanPhysicalDevice, &Props);
  if (PfnGpdf2 && Props.apiVersion >= VK_API_VERSION_1_2) {
    VkPhysicalDeviceVulkan12Features Feats12 = {0};
    Feats12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceFeatures2 Feats2 = {0};
    Feats2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    Feats2.pNext = &Feats12;
    PfnGpdf2(YonaVulkanPhysicalDevice, &Feats2);
    if (Feats12.timelineSemaphore == VK_TRUE) {
      YonaVulkanTimelineSemaphoreSupported = 1;
      return;
    }
  }

  PFN_vkEnumerateDeviceExtensionProperties PfnEnum =
      (PFN_vkEnumerateDeviceExtensionProperties)(void *)
          YonaVulkanGetInstanceProcAddress(
              YonaVulkanInstance, "vkEnumerateDeviceExtensionProperties");
  if (!PfnEnum)
    return;

  uint32_t N = 0;
  VkResult Rr = PfnEnum(YonaVulkanPhysicalDevice, NULL, &N, NULL);
  if (Rr != VK_SUCCESS || N == 0)
    return;
  VkExtensionProperties *Exts =
      (VkExtensionProperties *)calloc((size_t)N, sizeof(VkExtensionProperties));
  if (!Exts)
    return;
  Rr = PfnEnum(YonaVulkanPhysicalDevice, NULL, &N, Exts);
  if (Rr == VK_SUCCESS) {
    for (uint32_t I = 0; I < N; I++) {
      if (strcmp(Exts[I].extensionName,
                 VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME) == 0) {
        YonaVulkanTimelineSemaphoreSupported = 1;
        break;
      }
    }
  }
  free(Exts);
}

static int YonaVulkanPhysicalDevice_has_extension(const char *Needle) {
  PFN_vkEnumerateDeviceExtensionProperties PfnEnum =
      (PFN_vkEnumerateDeviceExtensionProperties)(void *)
          YonaVulkanGetInstanceProcAddress(
              YonaVulkanInstance, "vkEnumerateDeviceExtensionProperties");
  if (!PfnEnum || YonaVulkanPhysicalDevice == VK_NULL_HANDLE)
    return 0;
  uint32_t N = 0;
  VkResult Rr = PfnEnum(YonaVulkanPhysicalDevice, NULL, &N, NULL);
  if (Rr != VK_SUCCESS || N == 0)
    return 0;
  VkExtensionProperties *Exts =
      (VkExtensionProperties *)calloc((size_t)N, sizeof(VkExtensionProperties));
  if (!Exts)
    return 0;
  Rr = PfnEnum(YonaVulkanPhysicalDevice, NULL, &N, Exts);
  int Found = 0;
  if (Rr == VK_SUCCESS) {
    for (uint32_t I = 0; I < N; I++) {
      if (strcmp(Exts[I].extensionName, Needle) == 0) {
        Found = 1;
        break;
      }
    }
  }
  free(Exts);
  return Found;
}

static VkPhysicalDeviceFeatures2 YonaVulkanFeatures2;
static VkPhysicalDeviceTimelineSemaphoreFeatures YonaVulkanTimelineFeatures;
static VkPhysicalDeviceSynchronization2Features
    YonaVulkanSynchronization2Features;
static const char *YonaVulkanDeviceExtensions[12];
static uint32_t YonaVulkanDeviceExtensionCount;

static int YonaVulkanInstance_has_extension(const char *Needle) {
  PFN_vkEnumerateInstanceExtensionProperties Pfn =
      (PFN_vkEnumerateInstanceExtensionProperties)(void *)
          YonaVulkanGetInstanceProcAddress(
              NULL, "vkEnumerateInstanceExtensionProperties");
  if (!Pfn || !Needle)
    return 0;
  uint32_t N = 0;
  if (Pfn(NULL, &N, NULL) != VK_SUCCESS || N == 0)
    return 0;
  VkExtensionProperties *Exts =
      (VkExtensionProperties *)calloc((size_t)N, sizeof(VkExtensionProperties));
  if (!Exts)
    return 0;
  VkResult Rr = Pfn(NULL, &N, Exts);
  int Found = 0;
  if (Rr == VK_SUCCESS) {
    for (uint32_t I = 0; I < N; I++) {
      if (strcmp(Exts[I].extensionName, Needle) == 0) {
        Found = 1;
        break;
      }
    }
  }
  free(Exts);
  return Found;
}

static void yonaVulkanDciResetExts(void) { YonaVulkanDeviceExtensionCount = 0; }

static void yonaVulkanDciAddExt(const char *Name) {
  uint32_t Cap = (uint32_t)(sizeof(YonaVulkanDeviceExtensions) /
                            sizeof(YonaVulkanDeviceExtensions[0]));
  if (!Name || YonaVulkanDeviceExtensionCount >= Cap)
    return;
  YonaVulkanDeviceExtensions[YonaVulkanDeviceExtensionCount++] = Name;
}

static void yonaVulkanDciAddPortabilitySubset(void) {
  if (YonaVulkanPhysicalDevice_has_extension(
          YONA_VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
    yonaVulkanDciAddExt(YONA_VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
}

static void yonaVulkanDciApplyExts(VkDeviceCreateInfo *Dci) {
  Dci->enabledExtensionCount = YonaVulkanDeviceExtensionCount;
  Dci->ppEnabledExtensionNames =
      YonaVulkanDeviceExtensionCount ? YonaVulkanDeviceExtensions : NULL;
}

static int yonaVulkanDoTryInitUnlocked(void) {
  yonaVulkanNoteClear();

  if (!YonaRuntimeGpuVulkanLoaderAvailable()) {
    yonaVulkanNoteCopy("Vulkan loader not loadable");
    return -2;
  }

  if (YonaVulkanDynamicLibrary_open() != 0) {
    yonaVulkanNoteCopy("dlopen/vkGetInstanceProcAddr failed");
    return -2;
  }
  if (yonaVulkanLoadLoaderSymbols() != 0) {
    yonaVulkanNoteCopy("vkGetInstanceProcAddr missing vkCreateInstance");
    yonaVulkanDestroyAll();
    return -2;
  }

  VkApplicationInfo App = {0};
  App.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  App.pApplicationName = "Yona";
  App.applicationVersion = 0;
  App.pEngineName = "Yona";
  App.engineVersion = 0;
  App.apiVersion = VK_API_VERSION_1_0;

  VkInstanceCreateInfo Ici = {0};
  Ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  Ici.pApplicationInfo = &App;
  Ici.enabledLayerCount = 0;

  /* MoltenVK (and any VK_KHR_portability_enumeration loader) hides portability
   * physical devices unless the instance opts in. Query before create. */
  static const char *InstExts[4];
  uint32_t InstN = 0;
  if (YonaVulkanInstance_has_extension(
          VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
    InstExts[InstN++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    Ici.flags |= YONA_VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  }
  if (YonaVulkanInstance_has_extension(
          VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
    InstExts[InstN++] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
  Ici.enabledExtensionCount = InstN;
  Ici.ppEnabledExtensionNames = InstN ? InstExts : NULL;

  VkResult R = YonaVulkanCreateInstance(&Ici, NULL, &YonaVulkanInstance);
  if (R != VK_SUCCESS || YonaVulkanInstance == VK_NULL_HANDLE) {
    char B[200];
    snprintf(B, sizeof B, "vkCreateInstance failed VkResult=%d", (int)R);
    yonaVulkanNoteCopy(B);
    yonaVulkanDestroyAll();
    return -3;
  }

  if (yonaVulkanLoadInstanceSymbols(YonaVulkanInstance) != 0) {
    yonaVulkanNoteCopy(
        "vkGetInstanceProcAddr missing required instance symbols");
    yonaVulkanDestroyAll();
    return -3;
  }

  uint32_t Ndev = 0;
  R = YonaVulkanEnumeratePhysicalDevices(YonaVulkanInstance, &Ndev, NULL);
  if (R != VK_SUCCESS || Ndev == 0) {
    char B[200];
    snprintf(B, sizeof B,
             "vkEnumeratePhysicalDevices failed VkResult=%d count=%u", (int)R,
             Ndev);
    yonaVulkanNoteCopy(B);
    yonaVulkanDestroyAll();
    return -4;
  }

  VkPhysicalDevice *Devs =
      (VkPhysicalDevice *)malloc((size_t)Ndev * sizeof(VkPhysicalDevice));
  if (!Devs) {
    yonaVulkanNoteCopy("malloc failed for physical device list");
    yonaVulkanDestroyAll();
    return -5;
  }
  R = YonaVulkanEnumeratePhysicalDevices(YonaVulkanInstance, &Ndev, Devs);
  if (R != VK_SUCCESS) {
    char B[200];
    snprintf(B, sizeof B, "vkEnumeratePhysicalDevices (2nd) VkResult=%d",
             (int)R);
    yonaVulkanNoteCopy(B);
    free(Devs);
    yonaVulkanDestroyAll();
    return -3;
  }

  int UsedForcedIndex = 0;
  const char *IdxEnv = getenv("YONA_GPU_VULKAN_PHYSICAL_DEVICE_INDEX");
  if (IdxEnv && IdxEnv[0]) {
    unsigned long Fi = strtoul(IdxEnv, NULL, 10);
    if (Fi < (unsigned long)Ndev) {
      uint32_t Qf = 0;
      if (yonaVulkanPickComputeQueue(Devs[Fi], &Qf) == 0) {
        YonaVulkanPhysicalDevice = Devs[Fi];
        YonaVulkanQueueFamily = Qf;
        UsedForcedIndex = 1;
      } else {
        yonaVulkanNoteCopy("YONA_GPU_VULKAN_PHYSICAL_DEVICE_INDEX: no compute "
                           "queue on that device; "
                           "auto-selecting");
      }
    } else {
      yonaVulkanNoteCopy("YONA_GPU_VULKAN_PHYSICAL_DEVICE_INDEX: out of range; "
                         "auto-selecting");
    }
  }

  if (!UsedForcedIndex) {
    int64_t BestScore = (int64_t)-1;
    VkPhysicalDevice BestPhys = VK_NULL_HANDLE;
    uint32_t BestQf = (uint32_t)-1;

    for (uint32_t Di = 0; Di < Ndev; Di++) {
      uint32_t Qf = 0;
      if (yonaVulkanPickComputeQueue(Devs[Di], &Qf) != 0)
        continue;

      VkPhysicalDeviceProperties Props;
      YonaVulkanGetPhysicalDeviceProperties(Devs[Di], &Props);
      VkPhysicalDeviceFeatures Feats;
      YonaVulkanGetPhysicalDeviceFeatures(Devs[Di], &Feats);

      int64_t TypeRank;
      switch (Props.deviceType) {
      case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        TypeRank = 5;
        break;
      case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        TypeRank = 4;
        break;
      case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        TypeRank = 3;
        break;
      case VK_PHYSICAL_DEVICE_TYPE_CPU:
        TypeRank = 2;
        break;
      default:
        TypeRank = 1;
        break;
      }
      int64_t Score = TypeRank * (int64_t)10000000 +
                      (Feats.shaderInt64 ? (int64_t)5000000 : 0) +
                      (int64_t)Props.apiVersion;
      if (Score > BestScore) {
        BestScore = Score;
        BestPhys = Devs[Di];
        BestQf = Qf;
      }
    }

    if (BestPhys == VK_NULL_HANDLE) {
      yonaVulkanNoteCopy("no physical device exposes a compute queue");
      free(Devs);
      yonaVulkanDestroyAll();
      return -4;
    }
    YonaVulkanPhysicalDevice = BestPhys;
    YonaVulkanQueueFamily = BestQf;
  }

  free(Devs);

  yonaVulkanProbeTimelineSemSupported();

  float Qp = 1.0f;
  VkDeviceQueueCreateInfo Qci = {0};
  Qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  Qci.queueFamilyIndex = YonaVulkanQueueFamily;
  Qci.queueCount = 1;
  Qci.pQueuePriorities = &Qp;

  VkPhysicalDeviceFeatures PdFeats = {0};
  YonaVulkanGetPhysicalDeviceFeatures(YonaVulkanPhysicalDevice, &PdFeats);

  PFN_vkGetPhysicalDeviceFeatures2 PfnGpdf2Dev =
      (PFN_vkGetPhysicalDeviceFeatures2)(void *)
          YonaVulkanGetInstanceProcAddress(YonaVulkanInstance,
                                           "vkGetPhysicalDeviceFeatures2");
  if (!PfnGpdf2Dev)
    PfnGpdf2Dev = (PFN_vkGetPhysicalDeviceFeatures2)(void *)
        YonaVulkanGetInstanceProcAddress(YonaVulkanInstance,
                                         "vkGetPhysicalDeviceFeatures2KHR");

  int TryModern = 0;
  if (PfnGpdf2Dev &&
      YonaVulkanPhysicalDevice_has_extension(
          VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME) &&
      YonaVulkanPhysicalDevice_has_extension(
          VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)) {
    VkPhysicalDeviceTimelineSemaphoreFeatures Qts = {0};
    Qts.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    VkPhysicalDeviceSynchronization2Features Qs2 = {0};
    Qs2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    VkPhysicalDeviceFeatures2 Qf2 = {0};
    Qf2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    Qf2.pNext = &Qts;
    Qts.pNext = &Qs2;
    PfnGpdf2Dev(YonaVulkanPhysicalDevice, &Qf2);
    if (Qts.timelineSemaphore && Qs2.synchronization2)
      TryModern = 1;
  }

  VkDeviceCreateInfo Dci = {0};
  Dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  Dci.queueCreateInfoCount = 1;
  Dci.pQueueCreateInfos = &Qci;
  Dci.enabledLayerCount = 0;

  YonaVulkanShaderInt64Enabled = 0;
  Dci.pNext = NULL;
  Dci.pEnabledFeatures = NULL;
  Dci.enabledExtensionCount = 0;
  Dci.ppEnabledExtensionNames = NULL;

  if (TryModern) {
    yonaVulkanDciResetExts();
    yonaVulkanDciAddPortabilitySubset();
    yonaVulkanDciAddExt(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
    yonaVulkanDciAddExt(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);

    memset(&YonaVulkanFeatures2, 0, sizeof(YonaVulkanFeatures2));
    YonaVulkanFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    if (PdFeats.shaderInt64)
      YonaVulkanFeatures2.features.shaderInt64 = VK_TRUE;

    memset(&YonaVulkanTimelineFeatures, 0, sizeof(YonaVulkanTimelineFeatures));
    YonaVulkanTimelineFeatures.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    YonaVulkanTimelineFeatures.timelineSemaphore = VK_TRUE;

    memset(&YonaVulkanSynchronization2Features, 0,
           sizeof(YonaVulkanSynchronization2Features));
    YonaVulkanSynchronization2Features.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    YonaVulkanSynchronization2Features.synchronization2 = VK_TRUE;

    YonaVulkanFeatures2.pNext = &YonaVulkanTimelineFeatures;
    YonaVulkanTimelineFeatures.pNext = &YonaVulkanSynchronization2Features;

    Dci.pNext = &YonaVulkanFeatures2;
    Dci.pEnabledFeatures = NULL;
    yonaVulkanDciApplyExts(&Dci);
    YonaVulkanShaderInt64Enabled = PdFeats.shaderInt64 ? 1 : 0;
  } else {
    VkPhysicalDeviceFeatures EnabledFeats = {0};
    VkPhysicalDeviceFeatures *PFeatsArg = NULL;
    if (PdFeats.shaderInt64) {
      EnabledFeats.shaderInt64 = VK_TRUE;
      PFeatsArg = &EnabledFeats;
      YonaVulkanShaderInt64Enabled = 1;
    }
    Dci.pEnabledFeatures = PFeatsArg;
    yonaVulkanDciResetExts();
    yonaVulkanDciAddPortabilitySubset();
    yonaVulkanDciApplyExts(&Dci);
  }

  R = YonaVulkanCreateDevice(YonaVulkanPhysicalDevice, &Dci, NULL,
                             &YonaVulkanDevice);

  if ((R != VK_SUCCESS || YonaVulkanDevice == VK_NULL_HANDLE) && TryModern) {
    /* Retry without timeline/sync2 pNext chain (drivers differ on feature
     * chains). */
    TryModern = 0;
    memset(&Dci, 0, sizeof(Dci));
    Dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    Dci.queueCreateInfoCount = 1;
    Dci.pQueueCreateInfos = &Qci;
    Dci.enabledLayerCount = 0;
    Dci.pNext = NULL;

    VkPhysicalDeviceFeatures EnabledFeats = {0};
    VkPhysicalDeviceFeatures *PFeatsArg = NULL;
    YonaVulkanShaderInt64Enabled = 0;
    if (PdFeats.shaderInt64) {
      EnabledFeats.shaderInt64 = VK_TRUE;
      PFeatsArg = &EnabledFeats;
      YonaVulkanShaderInt64Enabled = 1;
    }
    Dci.pEnabledFeatures = PFeatsArg;
    yonaVulkanDciResetExts();
    yonaVulkanDciAddPortabilitySubset();
    yonaVulkanDciApplyExts(&Dci);
    R = YonaVulkanCreateDevice(YonaVulkanPhysicalDevice, &Dci, NULL,
                               &YonaVulkanDevice);
  }

  if (R != VK_SUCCESS || YonaVulkanDevice == VK_NULL_HANDLE) {
    if (Dci.pEnabledFeatures != NULL) {
      YonaVulkanShaderInt64Enabled = 0;
      Dci.pEnabledFeatures = NULL;
      R = YonaVulkanCreateDevice(YonaVulkanPhysicalDevice, &Dci, NULL,
                                 &YonaVulkanDevice);
    }
    if (R != VK_SUCCESS || YonaVulkanDevice == VK_NULL_HANDLE) {
      char B[200];
      snprintf(B, sizeof B, "vkCreateDevice failed VkResult=%d", (int)R);
      yonaVulkanNoteCopy(B);
      yonaVulkanDestroyAll();
      return -3;
    }
  }

  if (yonaVulkanLoadDeviceSymbols(YonaVulkanInstance, YonaVulkanDevice) != 0) {
    yonaVulkanNoteCopy("vkGetInstanceProcAddr missing required device symbols");
    yonaVulkanDestroyAll();
    return -3;
  }

  YonaVulkanGetDeviceQueue(YonaVulkanDevice, YonaVulkanQueueFamily, 0,
                           &YonaVulkanQueue);
  if (YonaVulkanQueue == VK_NULL_HANDLE) {
    yonaVulkanNoteCopy("vkGetDeviceQueue returned null");
    yonaVulkanDestroyAll();
    return -3;
  }

  YonaVulkanSynchronization2Enabled = TryModern ? 1 : 0;

  if (!YonaVulkanShaderInt64Enabled)
    yonaVulkanNoteCopy("device ready; shaderInt64 unavailable — IntArray GPU "
                       "uses i32 when values fit");
  else
    yonaVulkanNoteClear();
  return 0;
}

int YonaRuntimeGpuVulkanDeviceTryInitialize(void) {
  const char *Disabled = getenv("YONA_GPU_DISABLE_VULKAN");
  if (Disabled && Disabled[0] && strcmp(Disabled, "0") != 0)
    return -1;

  int Out = 0;
  YONA_VKDEV_LOCK();
  if (YonaVulkanDeviceLifecycle == YonaVulkanDeviceReady) {
    YONA_VKDEV_UNLOCK();
    return 0;
  }
  if (YonaVulkanDeviceLifecycle == YonaVulkanDeviceFailed) {
    if (yonaVulkanNoteIsEmpty())
      yonaVulkanNoteCopy("device: try_init previously failed; call "
                         "YonaRuntimeGpuVulkanDeviceShutdown() to reset");
    YONA_VKDEV_UNLOCK();
    return -3;
  }
  Out = yonaVulkanDoTryInitUnlocked();
  if (Out == -2) {
    /* Loader probe can differ from dlopen timing; do not latch FAILED. */
    YONA_VKDEV_UNLOCK();
    return -2;
  }
  YonaVulkanDeviceLifecycle =
      (Out == 0) ? YonaVulkanDeviceReady : YonaVulkanDeviceFailed;
  YONA_VKDEV_UNLOCK();
  return Out;
}

int YonaRuntimeGpuVulkanDeviceIsReady(void) {
  int R = 0;
  YONA_VKDEV_LOCK();
  R = (YonaVulkanDevice != VK_NULL_HANDLE) ? 1 : 0;
  YONA_VKDEV_UNLOCK();
  return R;
}

void YonaRuntimeGpuVulkanDeviceShutdown(void) {
  YONA_VKDEV_LOCK();
  YonaVulkanDeviceShuttingDown = 1;
  while (YonaVulkanDeviceActiveOperations != 0)
    YONA_VKDEV_WAIT();
  yonaVulkanDestroyAll();
  YonaVulkanDeviceLifecycle = YonaVulkanDeviceUntried;
  YonaVulkanDeviceShuttingDown = 0;
  YONA_VKDEV_UNLOCK();
  YonaRuntimeGpuVulkanInvalidateCapabilityCache();
}

const char *YonaRuntimeGpuVulkanDeviceStatusName(void) {
  const char *Disabled = getenv("YONA_GPU_DISABLE_VULKAN");
  if (Disabled && Disabled[0] && strcmp(Disabled, "0") != 0)
    return "vulkan-unavailable";
  if (!YonaRuntimeGpuVulkanLoaderAvailable())
    return "vulkan-unavailable";

  int Rc = YonaRuntimeGpuVulkanDeviceTryInitialize();
  if (Rc == 0)
    return "vulkan-device";
  if (Rc == -1 || Rc == -2)
    return "vulkan-unavailable";
  return "vulkan-loader";
}

int YonaRuntimeGpuVulkanDeviceHasShaderInt64(void) {
  int Out = 0;
  YONA_VKDEV_LOCK();
  if (YonaVulkanDevice != VK_NULL_HANDLE)
    Out = YonaVulkanShaderInt64Enabled;
  YONA_VKDEV_UNLOCK();
  return Out;
}

int YonaRuntimeGpuVulkanDeviceHasTimelineSemaphore(void) {
  int Out = 0;
  YONA_VKDEV_LOCK();
  if (YonaVulkanDevice != VK_NULL_HANDLE)
    Out = YonaVulkanTimelineSemaphoreSupported;
  YONA_VKDEV_UNLOCK();
  return Out;
}

int YonaRuntimeGpuVulkanDeviceHasSynchronization2(void) {
  int Out = 0;
  YONA_VKDEV_LOCK();
  if (YonaVulkanDevice != VK_NULL_HANDLE)
    Out = YonaVulkanSynchronization2Enabled;
  YONA_VKDEV_UNLOCK();
  return Out;
}

const char *YonaRuntimeGpuVulkanDeviceLastNote(void) {
  static YONA_VK_THREAD_LOCAL char Snapshot[sizeof(YonaVulkanLastNote)];
  YONA_VK_NOTE_LOCK();
  memcpy(Snapshot, YonaVulkanLastNote, sizeof(Snapshot));
  Snapshot[sizeof(Snapshot) - 1] = 0;
  YONA_VK_NOTE_UNLOCK();
  return Snapshot;
}

int yonaVulkanOperationBegin(void) {
  if (YonaRuntimeGpuVulkanDeviceTryInitialize() != 0)
    return 0;

  int Ready = 0;
  YONA_VKDEV_LOCK();
  if (!YonaVulkanDeviceShuttingDown && YonaVulkanDevice != VK_NULL_HANDLE) {
    ++YonaVulkanDeviceActiveOperations;
    Ready = 1;
  }
  YONA_VKDEV_UNLOCK();
  return Ready;
}

void yonaVulkanOperationEnd(void) {
  YONA_VKDEV_LOCK();
  if (YonaVulkanDeviceActiveOperations != 0)
    --YonaVulkanDeviceActiveOperations;
  if (YonaVulkanDeviceActiveOperations == 0)
    YONA_VKDEV_WAKE_ALL();
  YONA_VKDEV_UNLOCK();
}

int yonaVulkanPickMemoryType(uint32_t TypeBits, VkMemoryPropertyFlags Want,
                             uint32_t *OutIndex) {
  VkPhysicalDeviceMemoryProperties Mp;
  YonaVulkanGetPhysicalDeviceMemoryProperties(YonaVulkanPhysicalDevice, &Mp);
  for (uint32_t I = 0; I < Mp.memoryTypeCount; I++) {
    if ((TypeBits & (1u << I)) &&
        (Mp.memoryTypes[I].propertyFlags & Want) == Want) {
      *OutIndex = I;
      return 0;
    }
  }
  return -1;
}

#endif /* YONA_GPU_VULKAN_ENABLED */
