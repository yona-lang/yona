/* ===== Vulkan device (compute queue) — runtime-loaded entry points =====
 *
 * No link dependency on the Vulkan loader: vulkan-1.dll / libvulkan.so.1 /
 * libvulkan.1.dylib / libMoltenVK.dylib is opened with LoadLibrary/dlopen
 * and all used symbols are resolved dynamically.
 * When YONA_GPU_VULKAN_ENABLED is 0, this TU provides no-op stubs so default
 * packages and CI builds stay free of Khronos headers.
 */

#include "yona/runtime/gpu_vulkan_device.h"
#include "yona/runtime/gpu_build_config.h"

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

int yona_gpu_vulkan_device_try_init(void) { return -1; }

int yona_gpu_vulkan_device_ready(void) { return 0; }

void yona_gpu_vulkan_device_shutdown(void) { yona_gpu_vulkan_invalidate_capability_cache(); }

const char *yona_gpu_vulkan_device_status_name(void) { return "vulkan-loader"; }

int yona_gpu_vulkan_device_shader_int64(void) { return 0; }

const char *yona_gpu_vulkan_device_last_note(void) { return ""; }

void yona_gpu_vulkan_device_set_last_note(const char *msg) { (void)msg; }

int yona_gpu_vulkan_device_timeline_semaphore(void) { return 0; }

int yona_gpu_vulkan_device_last_issue_kind(void) { return 0; }

void yona_gpu_vulkan_device_note_vk(const char *ctx, int32_t vk_result) {
  (void)ctx;
  (void)vk_result;
}

#else /* YONA_GPU_VULKAN_ENABLED */

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#ifndef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
#define VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME "VK_KHR_portability_enumeration"
#endif
#ifndef VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
#define VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR 0x00000001u
#endif
#ifndef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
#define VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME "VK_KHR_portability_subset"
#endif

#if defined(_WIN32)
static CRITICAL_SECTION yona_vkdev_cs;
static volatile LONG yona_vkdev_cs_ready;

static void yona_vkdev_cs_ensure(void) {
  if (InterlockedCompareExchange(&yona_vkdev_cs_ready, 1, 0) == 0)
    InitializeCriticalSection(&yona_vkdev_cs);
}

#define YONA_VKDEV_LOCK()                                                                                                                            \
  do {                                                                                                                                               \
    yona_vkdev_cs_ensure();                                                                                                                          \
    EnterCriticalSection(&yona_vkdev_cs);                                                                                                            \
  } while (0)
#define YONA_VKDEV_UNLOCK() LeaveCriticalSection(&yona_vkdev_cs)

#else /* !_WIN32 */

static pthread_mutex_t yona_vkdev_mutex = PTHREAD_MUTEX_INITIALIZER;
#define YONA_VKDEV_LOCK() pthread_mutex_lock(&yona_vkdev_mutex)
#define YONA_VKDEV_UNLOCK() pthread_mutex_unlock(&yona_vkdev_mutex)

#endif

enum yona_vkdev_lifecycle {
  YONA_VKDEV_LIFE_UNTRIED = 0,
  YONA_VKDEV_LIFE_OK = 1,
  YONA_VKDEV_LIFE_FAILED = 2,
};

static enum yona_vkdev_lifecycle yona_vkdev_life = YONA_VKDEV_LIFE_UNTRIED;

#if defined(_WIN32)
static HMODULE yona_vk_dl;
#else
static void *yona_vk_dl;
#endif

static PFN_vkGetInstanceProcAddr yona_pfn_vkGetInstanceProcAddr;
static PFN_vkCreateInstance yona_pfn_vkCreateInstance;
static PFN_vkDestroyInstance yona_pfn_vkDestroyInstance;
static PFN_vkEnumeratePhysicalDevices yona_pfn_vkEnumeratePhysicalDevices;
static PFN_vkGetPhysicalDeviceQueueFamilyProperties yona_pfn_vkGetPhysicalDeviceQueueFamilyProperties;
static PFN_vkCreateDevice yona_pfn_vkCreateDevice;
static PFN_vkDestroyDevice yona_pfn_vkDestroyDevice;
static PFN_vkGetDeviceQueue yona_pfn_vkGetDeviceQueue;
static PFN_vkGetDeviceProcAddr yona_pfn_vkGetDeviceProcAddr;
static PFN_vkGetPhysicalDeviceFeatures yona_pfn_vkGetPhysicalDeviceFeatures;
static PFN_vkGetPhysicalDeviceMemoryProperties yona_pfn_vkGetPhysicalDeviceMemoryProperties;
static PFN_vkGetPhysicalDeviceProperties yona_pfn_vkGetPhysicalDeviceProperties;

static VkInstance yona_vk_instance = VK_NULL_HANDLE;
static VkPhysicalDevice yona_vk_phys = VK_NULL_HANDLE;
static VkDevice yona_vk_dev = VK_NULL_HANDLE;
static VkQueue yona_vk_queue = VK_NULL_HANDLE;
static uint32_t yona_vk_queue_family = (uint32_t)-1;
static int yona_vk_shader_int64_enabled;
static int yona_vk_timeline_sem_supported;

static char yona_vk_last_note[256];
static int yona_vk_last_issue_kind;

static void yona_vk_note_clear(void) {
  yona_vk_last_note[0] = 0;
  yona_vk_last_issue_kind = 0;
}

static void yona_vk_note_cpy(const char *s) {
  if (!s || !s[0]) {
    yona_vk_note_clear();
    return;
  }
  size_t n = strlen(s);
  if (n >= sizeof(yona_vk_last_note))
    n = sizeof(yona_vk_last_note) - 1;
  memcpy(yona_vk_last_note, s, n);
  yona_vk_last_note[n] = 0;
}

void yona_gpu_vulkan_device_set_last_note(const char *msg) {
  YONA_VKDEV_LOCK();
  yona_vk_note_cpy(msg);
  YONA_VKDEV_UNLOCK();
}

int yona_gpu_vulkan_device_last_issue_kind(void) {
  int o;
  YONA_VKDEV_LOCK();
  o = yona_vk_last_issue_kind;
  YONA_VKDEV_UNLOCK();
  return o;
}

void yona_gpu_vulkan_device_note_vk(const char *ctx, int32_t vk_result) {
  VkResult vr = (VkResult)vk_result;
  if (vr == VK_SUCCESS)
    return;
  int kind = 3;
  if (vr == VK_ERROR_OUT_OF_HOST_MEMORY || vr == VK_ERROR_OUT_OF_DEVICE_MEMORY)
    kind = 1;
  else if (vr == VK_ERROR_DEVICE_LOST)
    kind = 2;
  const char *hint = "";
  if (kind == 1)
    hint = " (OOM)";
  else if (kind == 2)
    hint = " (device lost)";
  char buf[256];
  snprintf(buf, sizeof(buf), "%s VkResult %d%s", ctx ? ctx : "vk", (int)vr, hint);
  YONA_VKDEV_LOCK();
  yona_vk_last_issue_kind = kind;
  yona_vk_note_cpy(buf);
  YONA_VKDEV_UNLOCK();
}

static void yona_vk_dl_close(void) {
#if defined(_WIN32)
  if (yona_vk_dl) {
    FreeLibrary(yona_vk_dl);
    yona_vk_dl = NULL;
  }
#else
  if (yona_vk_dl) {
    dlclose(yona_vk_dl);
    yona_vk_dl = NULL;
  }
#endif
}

static void *yona_vk_loader_dlsym(const char *name) {
#if defined(_WIN32)
  if (yona_vk_dl)
    return (void *)GetProcAddress((HMODULE)yona_vk_dl, name);
  return NULL;
#else
  void *p = NULL;
  if (yona_vk_dl)
    p = dlsym(yona_vk_dl, name);
  if (!p)
    p = dlsym(RTLD_DEFAULT, name);
  return p;
#endif
}

/* Resolve a loader-level entry (instance == NULL). GIPA can return NULL when
 * the process already linked a different loader image (gpu_stub / Vulkan::Vulkan);
 * fall back to dlsym on the handle and the process namespace. */
static void *yona_vk_resolve_loader_sym(const char *name) {
  void *p = NULL;
  if (yona_pfn_vkGetInstanceProcAddr)
    p = (void *)yona_pfn_vkGetInstanceProcAddr(VK_NULL_HANDLE, name);
  if (!p)
    p = yona_vk_loader_dlsym(name);
  return p;
}

static int yona_vk_dl_open(void) {
#if defined(YONA_HAS_VULKAN) && !defined(__ANDROID__)
  /* compiled_runtime includes gpu_stub.c first; the linked loader's GIPA
   * shares ICD state with float compute. Still dlopen so shutdown is uniform. */
  yona_pfn_vkGetInstanceProcAddr = vkGetInstanceProcAddr;
#endif
#if defined(_WIN32)
  yona_vk_dl = (HMODULE)yona_gpu_vulkan_open_loader();
#else
  yona_vk_dl = yona_gpu_vulkan_open_loader();
#endif
  if (!yona_pfn_vkGetInstanceProcAddr) {
    if (!yona_vk_dl)
      return -2;
    yona_pfn_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)yona_vk_loader_dlsym("vkGetInstanceProcAddr");
  }
  if (!yona_pfn_vkGetInstanceProcAddr)
    return -2;
  return 0;
}

static void yona_vk_clear_fn_ptrs(void) {
  yona_pfn_vkGetInstanceProcAddr = NULL;
  yona_pfn_vkCreateInstance = NULL;
  yona_pfn_vkDestroyInstance = NULL;
  yona_pfn_vkEnumeratePhysicalDevices = NULL;
  yona_pfn_vkGetPhysicalDeviceQueueFamilyProperties = NULL;
  yona_pfn_vkCreateDevice = NULL;
  yona_pfn_vkDestroyDevice = NULL;
  yona_pfn_vkGetDeviceQueue = NULL;
  yona_pfn_vkGetDeviceProcAddr = NULL;
  yona_pfn_vkGetPhysicalDeviceFeatures = NULL;
  yona_pfn_vkGetPhysicalDeviceMemoryProperties = NULL;
  yona_pfn_vkGetPhysicalDeviceProperties = NULL;
}

static int yona_vk_load_loader_symbols(void) {
  yona_pfn_vkCreateInstance = (PFN_vkCreateInstance)yona_vk_resolve_loader_sym("vkCreateInstance");
  if (!yona_pfn_vkCreateInstance)
    return -2;
  yona_pfn_vkEnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)yona_vk_resolve_loader_sym("vkEnumeratePhysicalDevices");
  yona_pfn_vkDestroyInstance = (PFN_vkDestroyInstance)yona_vk_resolve_loader_sym("vkDestroyInstance");
  if (!yona_pfn_vkEnumeratePhysicalDevices || !yona_pfn_vkDestroyInstance)
    return -2;
  return 0;
}

static int yona_vk_load_instance_symbols(VkInstance inst) {
  PFN_vkGetInstanceProcAddr gipa = yona_pfn_vkGetInstanceProcAddr;
  yona_pfn_vkGetPhysicalDeviceQueueFamilyProperties =
      (PFN_vkGetPhysicalDeviceQueueFamilyProperties)(void *)gipa(inst, "vkGetPhysicalDeviceQueueFamilyProperties");
  yona_pfn_vkCreateDevice = (PFN_vkCreateDevice)(void *)gipa(inst, "vkCreateDevice");
  if (!yona_pfn_vkGetPhysicalDeviceQueueFamilyProperties || !yona_pfn_vkCreateDevice)
    return -3;
  yona_pfn_vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)(void *)gipa(inst, "vkGetDeviceProcAddr");
  yona_pfn_vkGetPhysicalDeviceFeatures = (PFN_vkGetPhysicalDeviceFeatures)(void *)gipa(inst, "vkGetPhysicalDeviceFeatures");
  if (!yona_pfn_vkGetDeviceProcAddr || !yona_pfn_vkGetPhysicalDeviceFeatures)
    return -3;
  yona_pfn_vkGetPhysicalDeviceMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties)(void *)gipa(inst, "vkGetPhysicalDeviceMemoryProperties");
  if (!yona_pfn_vkGetPhysicalDeviceMemoryProperties)
    return -3;
  yona_pfn_vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)(void *)gipa(inst, "vkGetPhysicalDeviceProperties");
  if (!yona_pfn_vkGetPhysicalDeviceProperties)
    return -3;
  return 0;
}

static void yona_vk_compute_destroy_cached_pipelines(void);

static int yona_vk_load_device_symbols(VkInstance inst, VkDevice dev) {
  PFN_vkGetInstanceProcAddr gipa = yona_pfn_vkGetInstanceProcAddr;
  (void)inst;
  yona_pfn_vkDestroyDevice = (PFN_vkDestroyDevice)(void *)gipa(inst, "vkDestroyDevice");
  yona_pfn_vkGetDeviceQueue = (PFN_vkGetDeviceQueue)(void *)gipa(inst, "vkGetDeviceQueue");
  if (!yona_pfn_vkDestroyDevice || !yona_pfn_vkGetDeviceQueue)
    return -3;
  (void)dev;
  return 0;
}

static void yona_vk_destroy_all(void) {
  if (yona_vk_dev != VK_NULL_HANDLE && yona_pfn_vkGetDeviceProcAddr) {
    PFN_vkDeviceWaitIdle pfn_wait = (PFN_vkDeviceWaitIdle)(void *)yona_pfn_vkGetDeviceProcAddr(yona_vk_dev, "vkDeviceWaitIdle");
    if (pfn_wait)
      pfn_wait(yona_vk_dev);
  }
  yona_vk_compute_destroy_cached_pipelines();
  if (yona_vk_dev != VK_NULL_HANDLE && yona_pfn_vkDestroyDevice)
    yona_pfn_vkDestroyDevice(yona_vk_dev, NULL);
  yona_vk_dev = VK_NULL_HANDLE;
  yona_vk_queue = VK_NULL_HANDLE;
  yona_vk_phys = VK_NULL_HANDLE;
  yona_vk_queue_family = (uint32_t)-1;
  yona_vk_shader_int64_enabled = 0;
  yona_vk_timeline_sem_supported = 0;

  if (yona_vk_instance != VK_NULL_HANDLE && yona_pfn_vkDestroyInstance)
    yona_pfn_vkDestroyInstance(yona_vk_instance, NULL);
  yona_vk_instance = VK_NULL_HANDLE;

  yona_vk_clear_fn_ptrs();
  yona_vk_dl_close();
}

static int yona_vk_pick_compute_queue(VkPhysicalDevice phys, uint32_t *out_family) {
  uint32_t n = 0;
  yona_pfn_vkGetPhysicalDeviceQueueFamilyProperties(phys, &n, NULL);
  if (n == 0)
    return -4;
  VkQueueFamilyProperties *props = (VkQueueFamilyProperties *)malloc((size_t)n * sizeof(VkQueueFamilyProperties));
  if (!props)
    return -5;
  yona_pfn_vkGetPhysicalDeviceQueueFamilyProperties(phys, &n, props);
  uint32_t chosen = (uint32_t)-1;
  for (uint32_t i = 0; i < n; i++) {
    if (props[i].queueCount > 0 && (props[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
      chosen = i;
      break;
    }
  }
  free(props);
  if (chosen == (uint32_t)-1)
    return -4;
  *out_family = chosen;
  return 0;
}

static void yona_vk_probe_timeline_sem_supported(void) {
  yona_vk_timeline_sem_supported = 0;
  if (yona_vk_phys == VK_NULL_HANDLE || yona_vk_instance == VK_NULL_HANDLE)
    return;

  PFN_vkGetPhysicalDeviceFeatures2 pfn_gpdf2 =
      (PFN_vkGetPhysicalDeviceFeatures2)(void *)yona_pfn_vkGetInstanceProcAddr(yona_vk_instance, "vkGetPhysicalDeviceFeatures2");
  if (!pfn_gpdf2)
    pfn_gpdf2 = (PFN_vkGetPhysicalDeviceFeatures2)(void *)yona_pfn_vkGetInstanceProcAddr(yona_vk_instance, "vkGetPhysicalDeviceFeatures2KHR");
  VkPhysicalDeviceProperties props;
  yona_pfn_vkGetPhysicalDeviceProperties(yona_vk_phys, &props);
  if (pfn_gpdf2 && props.apiVersion >= VK_API_VERSION_1_2) {
    VkPhysicalDeviceVulkan12Features feats12 = {0};
    feats12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceFeatures2 feats2 = {0};
    feats2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    feats2.pNext = &feats12;
    pfn_gpdf2(yona_vk_phys, &feats2);
    if (feats12.timelineSemaphore == VK_TRUE) {
      yona_vk_timeline_sem_supported = 1;
      return;
    }
  }

  PFN_vkEnumerateDeviceExtensionProperties pfn_enum =
      (PFN_vkEnumerateDeviceExtensionProperties)(void *)yona_pfn_vkGetInstanceProcAddr(yona_vk_instance, "vkEnumerateDeviceExtensionProperties");
  if (!pfn_enum)
    return;

  uint32_t n = 0;
  VkResult rr = pfn_enum(yona_vk_phys, NULL, &n, NULL);
  if (rr != VK_SUCCESS || n == 0)
    return;
  VkExtensionProperties *exts = (VkExtensionProperties *)calloc((size_t)n, sizeof(VkExtensionProperties));
  if (!exts)
    return;
  rr = pfn_enum(yona_vk_phys, NULL, &n, exts);
  if (rr == VK_SUCCESS) {
    for (uint32_t i = 0; i < n; i++) {
      if (strcmp(exts[i].extensionName, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME) == 0) {
        yona_vk_timeline_sem_supported = 1;
        break;
      }
    }
  }
  free(exts);
}

static int yona_vk_phys_has_extension(const char *needle) {
  PFN_vkEnumerateDeviceExtensionProperties pfn_enum =
      (PFN_vkEnumerateDeviceExtensionProperties)(void *)yona_pfn_vkGetInstanceProcAddr(yona_vk_instance, "vkEnumerateDeviceExtensionProperties");
  if (!pfn_enum || yona_vk_phys == VK_NULL_HANDLE)
    return 0;
  uint32_t n = 0;
  VkResult rr = pfn_enum(yona_vk_phys, NULL, &n, NULL);
  if (rr != VK_SUCCESS || n == 0)
    return 0;
  VkExtensionProperties *exts = (VkExtensionProperties *)calloc((size_t)n, sizeof(VkExtensionProperties));
  if (!exts)
    return 0;
  rr = pfn_enum(yona_vk_phys, NULL, &n, exts);
  int found = 0;
  if (rr == VK_SUCCESS) {
    for (uint32_t i = 0; i < n; i++) {
      if (strcmp(exts[i].extensionName, needle) == 0) {
        found = 1;
        break;
      }
    }
  }
  free(exts);
  return found;
}

static VkPhysicalDeviceFeatures2 yona_vk_dci_f2;
static VkPhysicalDeviceTimelineSemaphoreFeatures yona_vk_dci_tf;
static VkPhysicalDeviceSynchronization2Features yona_vk_dci_s2f;
static const char *yona_vk_dci_exts_storage[12];
static uint32_t yona_vk_dci_ext_count;

static int yona_vk_instance_has_extension(const char *needle) {
  PFN_vkEnumerateInstanceExtensionProperties pfn =
      (PFN_vkEnumerateInstanceExtensionProperties)(void *)yona_pfn_vkGetInstanceProcAddr(NULL, "vkEnumerateInstanceExtensionProperties");
  if (!pfn || !needle)
    return 0;
  uint32_t n = 0;
  if (pfn(NULL, &n, NULL) != VK_SUCCESS || n == 0)
    return 0;
  VkExtensionProperties *exts = (VkExtensionProperties *)calloc((size_t)n, sizeof(VkExtensionProperties));
  if (!exts)
    return 0;
  VkResult rr = pfn(NULL, &n, exts);
  int found = 0;
  if (rr == VK_SUCCESS) {
    for (uint32_t i = 0; i < n; i++) {
      if (strcmp(exts[i].extensionName, needle) == 0) {
        found = 1;
        break;
      }
    }
  }
  free(exts);
  return found;
}

static void yona_vk_dci_reset_exts(void) { yona_vk_dci_ext_count = 0; }

static void yona_vk_dci_add_ext(const char *name) {
  uint32_t cap = (uint32_t)(sizeof(yona_vk_dci_exts_storage) / sizeof(yona_vk_dci_exts_storage[0]));
  if (!name || yona_vk_dci_ext_count >= cap)
    return;
  yona_vk_dci_exts_storage[yona_vk_dci_ext_count++] = name;
}

static void yona_vk_dci_add_portability_subset(void) {
  if (yona_vk_phys_has_extension(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
    yona_vk_dci_add_ext(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
}

static void yona_vk_dci_apply_exts(VkDeviceCreateInfo *dci) {
  dci->enabledExtensionCount = yona_vk_dci_ext_count;
  dci->ppEnabledExtensionNames = yona_vk_dci_ext_count ? yona_vk_dci_exts_storage : NULL;
}

static int yona_vk_do_try_init_unlocked(void) {
  yona_vk_note_clear();

  if (!yona_gpu_vulkan_loader_available()) {
    yona_vk_note_cpy("Vulkan loader not loadable");
    return -2;
  }

  if (yona_vk_dl_open() != 0) {
    yona_vk_note_cpy("dlopen/vkGetInstanceProcAddr failed");
    return -2;
  }
  if (yona_vk_load_loader_symbols() != 0) {
    yona_vk_note_cpy("vkGetInstanceProcAddr missing vkCreateInstance");
    yona_vk_destroy_all();
    return -2;
  }

  VkApplicationInfo app = {0};
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = "Yona";
  app.applicationVersion = 0;
  app.pEngineName = "Yona";
  app.engineVersion = 0;
  app.apiVersion = VK_API_VERSION_1_0;

  VkInstanceCreateInfo ici = {0};
  ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ici.pApplicationInfo = &app;
  ici.enabledLayerCount = 0;

  /* MoltenVK (and any VK_KHR_portability_enumeration loader) hides portability
   * physical devices unless the instance opts in. Query before create. */
  static const char *inst_exts[4];
  uint32_t inst_n = 0;
  if (yona_vk_instance_has_extension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
    inst_exts[inst_n++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    ici.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  }
  if (yona_vk_instance_has_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
    inst_exts[inst_n++] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
  ici.enabledExtensionCount = inst_n;
  ici.ppEnabledExtensionNames = inst_n ? inst_exts : NULL;

  VkResult r = yona_pfn_vkCreateInstance(&ici, NULL, &yona_vk_instance);
  if (r != VK_SUCCESS || yona_vk_instance == VK_NULL_HANDLE) {
    char b[200];
    snprintf(b, sizeof b, "vkCreateInstance failed VkResult=%d", (int)r);
    yona_vk_note_cpy(b);
    yona_vk_destroy_all();
    return -3;
  }

  if (yona_vk_load_instance_symbols(yona_vk_instance) != 0) {
    yona_vk_note_cpy("vkGetInstanceProcAddr missing required instance symbols");
    yona_vk_destroy_all();
    return -3;
  }

  uint32_t ndev = 0;
  r = yona_pfn_vkEnumeratePhysicalDevices(yona_vk_instance, &ndev, NULL);
  if (r != VK_SUCCESS || ndev == 0) {
    char b[200];
    snprintf(b, sizeof b, "vkEnumeratePhysicalDevices failed VkResult=%d count=%u", (int)r, ndev);
    yona_vk_note_cpy(b);
    yona_vk_destroy_all();
    return -4;
  }

  VkPhysicalDevice *devs = (VkPhysicalDevice *)malloc((size_t)ndev * sizeof(VkPhysicalDevice));
  if (!devs) {
    yona_vk_note_cpy("malloc failed for physical device list");
    yona_vk_destroy_all();
    return -5;
  }
  r = yona_pfn_vkEnumeratePhysicalDevices(yona_vk_instance, &ndev, devs);
  if (r != VK_SUCCESS) {
    char b[200];
    snprintf(b, sizeof b, "vkEnumeratePhysicalDevices (2nd) VkResult=%d", (int)r);
    yona_vk_note_cpy(b);
    free(devs);
    yona_vk_destroy_all();
    return -3;
  }

  int used_forced_index = 0;
  const char *idx_env = getenv("YONA_GPU_VULKAN_PHYSICAL_DEVICE_INDEX");
  if (idx_env && idx_env[0]) {
    unsigned long fi = strtoul(idx_env, NULL, 10);
    if (fi < (unsigned long)ndev) {
      uint32_t qf = 0;
      if (yona_vk_pick_compute_queue(devs[fi], &qf) == 0) {
        yona_vk_phys = devs[fi];
        yona_vk_queue_family = qf;
        used_forced_index = 1;
      } else {
        yona_vk_note_cpy("YONA_GPU_VULKAN_PHYSICAL_DEVICE_INDEX: no compute queue on that device; "
                         "auto-selecting");
      }
    } else {
      yona_vk_note_cpy("YONA_GPU_VULKAN_PHYSICAL_DEVICE_INDEX: out of range; auto-selecting");
    }
  }

  if (!used_forced_index) {
    int64_t best_score = (int64_t)-1;
    VkPhysicalDevice best_phys = VK_NULL_HANDLE;
    uint32_t best_qf = (uint32_t)-1;

    for (uint32_t di = 0; di < ndev; di++) {
      uint32_t qf = 0;
      if (yona_vk_pick_compute_queue(devs[di], &qf) != 0)
        continue;

      VkPhysicalDeviceProperties props;
      yona_pfn_vkGetPhysicalDeviceProperties(devs[di], &props);
      VkPhysicalDeviceFeatures feats;
      yona_pfn_vkGetPhysicalDeviceFeatures(devs[di], &feats);

      int64_t type_rank;
      switch (props.deviceType) {
      case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        type_rank = 5;
        break;
      case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        type_rank = 4;
        break;
      case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        type_rank = 3;
        break;
      case VK_PHYSICAL_DEVICE_TYPE_CPU:
        type_rank = 2;
        break;
      default:
        type_rank = 1;
        break;
      }
      int64_t score = type_rank * (int64_t)10000000 + (feats.shaderInt64 ? (int64_t)5000000 : 0) + (int64_t)props.apiVersion;
      if (score > best_score) {
        best_score = score;
        best_phys = devs[di];
        best_qf = qf;
      }
    }

    if (best_phys == VK_NULL_HANDLE) {
      yona_vk_note_cpy("no physical device exposes a compute queue");
      free(devs);
      yona_vk_destroy_all();
      return -4;
    }
    yona_vk_phys = best_phys;
    yona_vk_queue_family = best_qf;
  }

  free(devs);

  yona_vk_probe_timeline_sem_supported();

  float qp = 1.0f;
  VkDeviceQueueCreateInfo qci = {0};
  qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qci.queueFamilyIndex = yona_vk_queue_family;
  qci.queueCount = 1;
  qci.pQueuePriorities = &qp;

  VkPhysicalDeviceFeatures pd_feats = {0};
  yona_pfn_vkGetPhysicalDeviceFeatures(yona_vk_phys, &pd_feats);

  PFN_vkGetPhysicalDeviceFeatures2 pfn_gpdf2_dev =
      (PFN_vkGetPhysicalDeviceFeatures2)(void *)yona_pfn_vkGetInstanceProcAddr(yona_vk_instance, "vkGetPhysicalDeviceFeatures2");
  if (!pfn_gpdf2_dev)
    pfn_gpdf2_dev = (PFN_vkGetPhysicalDeviceFeatures2)(void *)yona_pfn_vkGetInstanceProcAddr(yona_vk_instance, "vkGetPhysicalDeviceFeatures2KHR");

  int try_modern = 0;
  if (pfn_gpdf2_dev && yona_vk_phys_has_extension(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME) &&
      yona_vk_phys_has_extension(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)) {
    VkPhysicalDeviceTimelineSemaphoreFeatures qts = {0};
    qts.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    VkPhysicalDeviceSynchronization2Features qs2 = {0};
    qs2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    VkPhysicalDeviceFeatures2 qf2 = {0};
    qf2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    qf2.pNext = &qts;
    qts.pNext = &qs2;
    pfn_gpdf2_dev(yona_vk_phys, &qf2);
    if (qts.timelineSemaphore && qs2.synchronization2)
      try_modern = 1;
  }

  VkDeviceCreateInfo dci = {0};
  dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.enabledLayerCount = 0;

  yona_vk_shader_int64_enabled = 0;
  dci.pNext = NULL;
  dci.pEnabledFeatures = NULL;
  dci.enabledExtensionCount = 0;
  dci.ppEnabledExtensionNames = NULL;

  if (try_modern) {
    yona_vk_dci_reset_exts();
    yona_vk_dci_add_portability_subset();
    yona_vk_dci_add_ext(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
    yona_vk_dci_add_ext(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);

    memset(&yona_vk_dci_f2, 0, sizeof(yona_vk_dci_f2));
    yona_vk_dci_f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    if (pd_feats.shaderInt64)
      yona_vk_dci_f2.features.shaderInt64 = VK_TRUE;

    memset(&yona_vk_dci_tf, 0, sizeof(yona_vk_dci_tf));
    yona_vk_dci_tf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    yona_vk_dci_tf.timelineSemaphore = VK_TRUE;

    memset(&yona_vk_dci_s2f, 0, sizeof(yona_vk_dci_s2f));
    yona_vk_dci_s2f.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    yona_vk_dci_s2f.synchronization2 = VK_TRUE;

    yona_vk_dci_f2.pNext = &yona_vk_dci_tf;
    yona_vk_dci_tf.pNext = &yona_vk_dci_s2f;

    dci.pNext = &yona_vk_dci_f2;
    dci.pEnabledFeatures = NULL;
    yona_vk_dci_apply_exts(&dci);
    yona_vk_shader_int64_enabled = pd_feats.shaderInt64 ? 1 : 0;
  } else {
    VkPhysicalDeviceFeatures enabled_feats = {0};
    VkPhysicalDeviceFeatures *p_feats_arg = NULL;
    if (pd_feats.shaderInt64) {
      enabled_feats.shaderInt64 = VK_TRUE;
      p_feats_arg = &enabled_feats;
      yona_vk_shader_int64_enabled = 1;
    }
    dci.pEnabledFeatures = p_feats_arg;
    yona_vk_dci_reset_exts();
    yona_vk_dci_add_portability_subset();
    yona_vk_dci_apply_exts(&dci);
  }

  r = yona_pfn_vkCreateDevice(yona_vk_phys, &dci, NULL, &yona_vk_dev);

  if ((r != VK_SUCCESS || yona_vk_dev == VK_NULL_HANDLE) && try_modern) {
    /* Retry without timeline/sync2 pNext chain (drivers differ on feature chains). */
    try_modern = 0;
    memset(&dci, 0, sizeof(dci));
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledLayerCount = 0;
    dci.pNext = NULL;

    VkPhysicalDeviceFeatures enabled_feats = {0};
    VkPhysicalDeviceFeatures *p_feats_arg = NULL;
    yona_vk_shader_int64_enabled = 0;
    if (pd_feats.shaderInt64) {
      enabled_feats.shaderInt64 = VK_TRUE;
      p_feats_arg = &enabled_feats;
      yona_vk_shader_int64_enabled = 1;
    }
    dci.pEnabledFeatures = p_feats_arg;
    yona_vk_dci_reset_exts();
    yona_vk_dci_add_portability_subset();
    yona_vk_dci_apply_exts(&dci);
    r = yona_pfn_vkCreateDevice(yona_vk_phys, &dci, NULL, &yona_vk_dev);
  }

  if (r != VK_SUCCESS || yona_vk_dev == VK_NULL_HANDLE) {
    if (dci.pEnabledFeatures != NULL) {
      yona_vk_shader_int64_enabled = 0;
      dci.pEnabledFeatures = NULL;
      r = yona_pfn_vkCreateDevice(yona_vk_phys, &dci, NULL, &yona_vk_dev);
    }
    if (r != VK_SUCCESS || yona_vk_dev == VK_NULL_HANDLE) {
      char b[200];
      snprintf(b, sizeof b, "vkCreateDevice failed VkResult=%d", (int)r);
      yona_vk_note_cpy(b);
      yona_vk_destroy_all();
      return -3;
    }
  }

  if (yona_vk_load_device_symbols(yona_vk_instance, yona_vk_dev) != 0) {
    yona_vk_note_cpy("vkGetInstanceProcAddr missing required device symbols");
    yona_vk_destroy_all();
    return -3;
  }

  yona_pfn_vkGetDeviceQueue(yona_vk_dev, yona_vk_queue_family, 0, &yona_vk_queue);
  if (yona_vk_queue == VK_NULL_HANDLE) {
    yona_vk_note_cpy("vkGetDeviceQueue returned null");
    yona_vk_destroy_all();
    return -3;
  }

  if (!yona_vk_shader_int64_enabled)
    yona_vk_note_cpy("device ready; shaderInt64 unavailable — IntArray GPU uses i32 when values fit");
  else
    yona_vk_note_clear();
  return 0;
}

int yona_gpu_vulkan_device_try_init(void) {
  const char *disabled = getenv("YONA_GPU_DISABLE_VULKAN");
  if (disabled && disabled[0] && strcmp(disabled, "0") != 0)
    return -1;

  int out = 0;
  YONA_VKDEV_LOCK();
  if (yona_vkdev_life == YONA_VKDEV_LIFE_OK) {
    YONA_VKDEV_UNLOCK();
    return 0;
  }
  if (yona_vkdev_life == YONA_VKDEV_LIFE_FAILED) {
    if (!yona_vk_last_note[0])
      yona_vk_note_cpy("device: try_init previously failed; call yona_gpu_vulkan_device_shutdown() to reset");
    YONA_VKDEV_UNLOCK();
    return -3;
  }
  out = yona_vk_do_try_init_unlocked();
  if (out == -2) {
    /* Loader probe can differ from dlopen timing; do not latch FAILED. */
    YONA_VKDEV_UNLOCK();
    return -2;
  }
  yona_vkdev_life = (out == 0) ? YONA_VKDEV_LIFE_OK : YONA_VKDEV_LIFE_FAILED;
  YONA_VKDEV_UNLOCK();
  return out;
}

int yona_gpu_vulkan_device_ready(void) {
  int r = 0;
  YONA_VKDEV_LOCK();
  r = (yona_vk_dev != VK_NULL_HANDLE) ? 1 : 0;
  YONA_VKDEV_UNLOCK();
  return r;
}

void yona_gpu_vulkan_device_shutdown(void) {
  YONA_VKDEV_LOCK();
  yona_vk_destroy_all();
  yona_vkdev_life = YONA_VKDEV_LIFE_UNTRIED;
  YONA_VKDEV_UNLOCK();
  yona_gpu_vulkan_invalidate_capability_cache();
}

const char *yona_gpu_vulkan_device_status_name(void) {
  const char *disabled = getenv("YONA_GPU_DISABLE_VULKAN");
  if (disabled && disabled[0] && strcmp(disabled, "0") != 0)
    return "vulkan-unavailable";
  if (!yona_gpu_vulkan_loader_available())
    return "vulkan-unavailable";

  int rc = yona_gpu_vulkan_device_try_init();
  if (rc == 0)
    return "vulkan-device";
  if (rc == -1 || rc == -2)
    return "vulkan-unavailable";
  return "vulkan-loader";
}

int yona_gpu_vulkan_device_shader_int64(void) {
  int out = 0;
  YONA_VKDEV_LOCK();
  if (yona_vk_dev != VK_NULL_HANDLE)
    out = yona_vk_shader_int64_enabled;
  YONA_VKDEV_UNLOCK();
  return out;
}

int yona_gpu_vulkan_device_timeline_semaphore(void) {
  int out = 0;
  YONA_VKDEV_LOCK();
  if (yona_vk_dev != VK_NULL_HANDLE)
    out = yona_vk_timeline_sem_supported;
  YONA_VKDEV_UNLOCK();
  return out;
}

const char *yona_gpu_vulkan_device_last_note(void) { return yona_vk_last_note[0] ? yona_vk_last_note : ""; }

static int yona_vk_pick_memory_type(uint32_t type_bits, VkMemoryPropertyFlags want, uint32_t *out_index) {
  VkPhysicalDeviceMemoryProperties mp;
  yona_pfn_vkGetPhysicalDeviceMemoryProperties(yona_vk_phys, &mp);
  for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
    if ((type_bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) {
      *out_index = i;
      return 0;
    }
  }
  return -1;
}

#include "gpu_vulkan_compute.c"
#include "gpu_vulkan_ops.c"

#endif /* YONA_GPU_VULKAN_ENABLED */
