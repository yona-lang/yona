/// Std\Gpu — Vulkan discovery + optional f64 scale/mul2 compute
/// (`floatArrayScaleAsync`, `floatArrayMul2Async` from Yona; shader push scale,
/// mul2 wraps scale 2.0) plus internal context (instance / device / queue /
/// pools / pipelines). See docs/design-gpu-async.md.

#include "yona/Runtime/Core/Api.h"
#include "yona/Runtime/Gpu/Api.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(YONA_HAS_VULKAN) && !defined(__ANDROID__)

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

typedef SRWLOCK YonaGpuMutex;

static YonaGpuMutex GMu = SRWLOCK_INIT;

static YonaGpuMutex GFenceJobMu = SRWLOCK_INIT;
static CONDITION_VARIABLE GFenceCv = CONDITION_VARIABLE_INIT;
static INIT_ONCE GFenceWorkerOnce = INIT_ONCE_STATIC_INIT;
static int GFenceWorkerStarted;

static inline void lockGpuMutex(YonaGpuMutex *M) {
  AcquireSRWLockExclusive((SRWLOCK *)M);
}

static inline void unlockGpuMutex(YonaGpuMutex *M) {
  ReleaseSRWLockExclusive((SRWLOCK *)M);
}

#define YONA_GPU_FENCE_CV_WAIT()                                               \
  SleepConditionVariableSRW(&GFenceCv, &GFenceJobMu, INFINITE, 0)

#define YONA_GPU_FENCE_CV_WAKE_ONE() WakeConditionVariable(&GFenceCv)
#define YONA_GPU_FENCE_CV_WAKE_ALL() WakeAllConditionVariable(&GFenceCv)

static DWORD WINAPI gpuFenceWorkerWindowsEntry(LPVOID Unused);

static BOOL CALLBACK initializeGpuFenceThreadOnce(PINIT_ONCE InitOnce,
                                                  PVOID Param, PVOID *Ctx) {
  (void)InitOnce;
  (void)Param;
  (void)Ctx;
  HANDLE H =
      CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)gpuFenceWorkerWindowsEntry,
                   NULL, 0, NULL);
  if (H != NULL) {
    CloseHandle(H);
    GFenceWorkerStarted = 1;
    return TRUE;
  }
  return FALSE;
}

#else /* POSIX */

#include <pthread.h>

typedef pthread_mutex_t YonaGpuMutex;

static YonaGpuMutex GMu = PTHREAD_MUTEX_INITIALIZER;

static YonaGpuMutex GFenceJobMu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t GFenceJobCv = PTHREAD_COND_INITIALIZER;
static YonaGpuMutex GFenceWorkerStartMu = PTHREAD_MUTEX_INITIALIZER;

static inline void lockGpuMutex(YonaGpuMutex *M) { pthread_mutex_lock(M); }

static inline void unlockGpuMutex(YonaGpuMutex *M) { pthread_mutex_unlock(M); }

static int GFenceWorkerStarted;

#define YONA_GPU_FENCE_CV_WAIT() pthread_cond_wait(&GFenceJobCv, &GFenceJobMu)

#define YONA_GPU_FENCE_CV_WAKE_ONE() pthread_cond_signal(&GFenceJobCv)
#define YONA_GPU_FENCE_CV_WAKE_ALL() pthread_cond_broadcast(&GFenceJobCv)

#endif /* POSIX vs WIN32 */

#include "Runtime/Generated/Float32ReduceSpv.inc"
#include "Runtime/Generated/Float32ScaleSpv.inc"
#include "Runtime/Generated/Float64MultiplyTwoSpv.inc"
#include "Runtime/Generated/Float64ReduceSpv.inc"
#include "Runtime/Generated/NopSpv.inc"
#include "yona/Runtime/Gpu/VulkanDevice.h"

#include <vulkan/vulkan.h>

#include <stdio.h>

#ifdef VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
#define YONA_VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR                  \
  VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
#else
#define YONA_VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR                  \
  ((VkInstanceCreateFlags)0x00000001)
#endif

static int GProbeDone;
static int GPhysCount;
static int GHaveCompute;

/* Lazily created for submit path (not used by Yona externs yet). */
static VkInstance GInst = VK_NULL_HANDLE;
static VkPhysicalDevice GPhys = VK_NULL_HANDLE;
static VkDevice GDev = VK_NULL_HANDLE;
static VkQueue GQueue = VK_NULL_HANDLE;
static uint32_t GQfamily;
static VkCommandPool GCmdPool = VK_NULL_HANDLE;
static VkShaderModule GShaderMod = VK_NULL_HANDLE;
static VkPipelineLayout GPipeLayout = VK_NULL_HANDLE;
static VkPipeline GComputePipe = VK_NULL_HANDLE;

/* Optional f64 SSBO mul2 pipeline (lazy). */
static int GDeviceShaderF64Enabled;
static int GF64PipelineFail;
static VkShaderModule GF64Shader = VK_NULL_HANDLE;
static VkDescriptorSetLayout GF64DescLayout = VK_NULL_HANDLE;
static VkPipelineLayout GF64PipeLayout = VK_NULL_HANDLE;
static VkPipeline GF64Pipeline = VK_NULL_HANDLE;

/* f32 scale when shaderFloat64 is missing (typical Metal / MoltenVK). */
static int GF32PipelineFail;
static VkShaderModule GF32Shader = VK_NULL_HANDLE;
static VkDescriptorSetLayout GF32DescLayout = VK_NULL_HANDLE;
static VkPipelineLayout GF32PipeLayout = VK_NULL_HANDLE;
static VkPipeline GF32Pipeline = VK_NULL_HANDLE;

/* f64/f32 block-reduce (two SSBOs: input + per-workgroup sums). */
static int GF64ReduceFail;
static VkShaderModule GF64ReduceShader = VK_NULL_HANDLE;
static VkDescriptorSetLayout GF64ReduceDsl = VK_NULL_HANDLE;
static VkPipelineLayout GF64ReduceLayout = VK_NULL_HANDLE;
static VkPipeline GF64ReducePipe = VK_NULL_HANDLE;
static int GF32ReduceFail;
static VkShaderModule GF32ReduceShader = VK_NULL_HANDLE;
static VkDescriptorSetLayout GF32ReduceDsl = VK_NULL_HANDLE;
static VkPipelineLayout GF32ReduceLayout = VK_NULL_HANDLE;
static VkPipeline GF32ReducePipe = VK_NULL_HANDLE;

static PFN_vkQueueSubmit2 GPfnVkQueueSubmit2;
static PFN_vkWaitSemaphores GPfnVkWaitSemaphores;
static int GStubModernSyncReady;
static uint64_t GStubTimelineNext;

static int gpuStubPhysicalDeviceHasExtension(const char *Needle) {
  uint32_t N = 0;
  if (vkEnumerateDeviceExtensionProperties(GPhys, NULL, &N, NULL) !=
          VK_SUCCESS ||
      N == 0)
    return 0;
  VkExtensionProperties *Exts =
      (VkExtensionProperties *)calloc((size_t)N, sizeof(VkExtensionProperties));
  if (!Exts)
    return 0;
  VkResult Rr = vkEnumerateDeviceExtensionProperties(GPhys, NULL, &N, Exts);
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

static int gpuStubAsyncUsesTimeline(void) {
  if (!GStubModernSyncReady)
    return 0;
  const char *E = getenv("YONA_GPU_ASYNC_TIMELINE");
  if (E && E[0] == '0' && E[1] == 0)
    return 0;
  return 1;
}

/* Dedicated thread waits on GPU fences — not the thread pool
 * (design-gpu-async.md). */
typedef struct YonaGpuFenceJob {
  struct YonaGpuFenceJob *Next;
  VkDevice Dev;
  VkFence Fence;
  int AsyncTimeline;
  VkSemaphore TimelineSem;
  uint64_t TimelineValue;
  YonaTaskRef Promise;
  YonaTaskGroupRef Group;
  VkCommandBuffer Cmd;
  VkCommandPool CmdPool;
  VkDescriptorPool Dpool;
  double *HostElements;
  uint32_t Count;
  void *Mapped;
  VkDeviceMemory Mem;
  VkBuffer Buf;
  VkDeviceSize AllocSz;
  VkMemoryPropertyFlags MemProps;
  /* 1 once the user-visible promise was completed (cancel may finish early). */
  int PromiseDelivered;
  /* 1 when cancel asked to skip writing GPU results back to the host buffer. */
  int DiscardHostWrite;
} YonaGpuFenceJob;

static YonaGpuFenceJob *GFenceJobHead;
static YonaGpuFenceJob *GFenceJobTail;
static size_t GFenceJobsActive;

static void noteGpuStubVulkanResult(const char *Ctx, VkResult Vr) {
  if (Vr == VK_SUCCESS)
    return;
  char Prefixed[240];
  snprintf(Prefixed, sizeof Prefixed, "float: %s", Ctx ? Ctx : "");
  YonaRuntimeGpuVulkanDeviceNoteResult(Prefixed, (int32_t)Vr);
}

#define YONA_GPU_STUB_VK_SYNC(stmt, Ctx, errn)                                 \
  do {                                                                         \
    VkResult GpuVulkanResult = (stmt);                                         \
    if (GpuVulkanResult != VK_SUCCESS) {                                       \
      noteGpuStubVulkanResult((Ctx), GpuVulkanResult);                         \
      err = (errn);                                                            \
      goto cleanup;                                                            \
    }                                                                          \
  } while (0)

#define YONA_GPU_STUB_VK_ASYNC(stmt, Ctx, errn)                                \
  do {                                                                         \
    VkResult GpuVulkanResult = (stmt);                                         \
    if (GpuVulkanResult != VK_SUCCESS) {                                       \
      noteGpuStubVulkanResult((Ctx), GpuVulkanResult);                         \
      err = (errn);                                                            \
      goto async_fail;                                                         \
    }                                                                          \
  } while (0)

static void completeGpuFenceJobTask(YonaGpuFenceJob *J, int64_t Res,
                                    int IsErr) {
  if (__atomic_exchange_n(&J->PromiseDelivered, 1, __ATOMIC_SEQ_CST) != 0)
    return;
  YonaRuntimeTaskComplete(J->Promise, Res, IsErr, J->Group);
}

static void runGpuFenceJob(YonaGpuFenceJob *J) {
  VkResult Wr = VK_SUCCESS;
  const uint64_t PollNs = 1000000ull; /* 1ms */

  if (J->AsyncTimeline) {
    if (GPfnVkWaitSemaphores == NULL) {
      Wr = VK_ERROR_FEATURE_NOT_PRESENT;
    } else {
      for (;;) {
        if (J->Group != NULL && YonaRuntimeTaskGroupIsCancelled(J->Group)) {
          __atomic_store_n(&J->DiscardHostWrite, 1, __ATOMIC_SEQ_CST);
          completeGpuFenceJobTask(J, -887, 1);
        }
        VkSemaphoreWaitInfo Wi = {0};
        Wi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        Wi.semaphoreCount = 1;
        Wi.pSemaphores = &J->TimelineSem;
        Wi.pValues = &J->TimelineValue;
        Wr = GPfnVkWaitSemaphores(J->Dev, &Wi, PollNs);
        if (Wr == VK_SUCCESS)
          break;
        if (Wr == VK_TIMEOUT)
          continue;
        break;
      }
    }
    if (Wr != VK_SUCCESS && Wr != VK_TIMEOUT)
      noteGpuStubVulkanResult("vkWaitSemaphores (async fence thread)", Wr);
    if (J->TimelineSem != VK_NULL_HANDLE) {
      vkDestroySemaphore(J->Dev, J->TimelineSem, NULL);
      J->TimelineSem = VK_NULL_HANDLE;
    }
  } else {
    for (;;) {
      if (J->Group != NULL && YonaRuntimeTaskGroupIsCancelled(J->Group)) {
        __atomic_store_n(&J->DiscardHostWrite, 1, __ATOMIC_SEQ_CST);
        completeGpuFenceJobTask(J, -887, 1);
      }
      Wr = vkWaitForFences(J->Dev, 1, &J->Fence, VK_TRUE, PollNs);
      if (Wr == VK_SUCCESS)
        break;
      if (Wr == VK_TIMEOUT)
        continue;
      break;
    }
    vkDestroyFence(J->Dev, J->Fence, NULL);
    J->Fence = VK_NULL_HANDLE;
    if (Wr != VK_SUCCESS && Wr != VK_TIMEOUT)
      noteGpuStubVulkanResult("vkWaitForFences (async fence thread)", Wr);
  }
  vkFreeCommandBuffers(J->Dev, J->CmdPool, 1, &J->Cmd);
  J->Cmd = VK_NULL_HANDLE;

  int Discard = __atomic_load_n(&J->DiscardHostWrite, __ATOMIC_SEQ_CST);
  if (Wr == VK_SUCCESS && !Discard) {
    if ((J->MemProps & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
      VkMappedMemoryRange Rng = {0};
      Rng.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      Rng.memory = J->Mem;
      Rng.offset = 0;
      Rng.size = J->AllocSz;
      vkInvalidateMappedMemoryRanges(J->Dev, 1, &Rng);
    }
    memcpy(J->HostElements, J->Mapped, (size_t)J->Count * sizeof(double));
  }

  if (J->Mapped != NULL) {
    vkUnmapMemory(J->Dev, J->Mem);
    J->Mapped = NULL;
  }
  if (J->Mem != VK_NULL_HANDLE) {
    vkFreeMemory(J->Dev, J->Mem, NULL);
    J->Mem = VK_NULL_HANDLE;
  }
  if (J->Buf != VK_NULL_HANDLE) {
    vkDestroyBuffer(J->Dev, J->Buf, NULL);
    J->Buf = VK_NULL_HANDLE;
  }
  if (J->Dpool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(J->Dev, J->Dpool, NULL);
    J->Dpool = VK_NULL_HANDLE;
  }

  /* If cancel already completed the promise, skip; else map
   * success/fail/cancel. */
  if (__atomic_load_n(&J->PromiseDelivered, __ATOMIC_SEQ_CST) == 0) {
    int64_t Res = (Wr == VK_SUCCESS) ? 0 : (int64_t)Wr;
    int IsErr = (Wr != VK_SUCCESS) ? 1 : 0;
    if (Wr == VK_SUCCESS && J->Group != NULL &&
        YonaRuntimeTaskGroupIsCancelled(J->Group)) {
      Res = -887;
      IsErr = 1;
    }
    completeGpuFenceJobTask(J, Res, IsErr);
  }
  free(J);

  lockGpuMutex(&GFenceJobMu);
  if (GFenceJobsActive != 0) {
    --GFenceJobsActive;
  }
  if (GFenceJobsActive == 0) {
    YONA_GPU_FENCE_CV_WAKE_ALL();
  }
  unlockGpuMutex(&GFenceJobMu);
}

static void *gpuFenceWorkerMain(void *Unused) {
  (void)Unused;
  for (;;) {
    lockGpuMutex(&GFenceJobMu);
    while (GFenceJobHead == NULL) {
      YONA_GPU_FENCE_CV_WAIT();
    }
    YonaGpuFenceJob *J = GFenceJobHead;
    GFenceJobHead = J->Next;
    if (GFenceJobHead == NULL) {
      GFenceJobTail = NULL;
    }
    unlockGpuMutex(&GFenceJobMu);
    runGpuFenceJob(J);
  }
}

#if defined(_WIN32)
static DWORD WINAPI gpuFenceWorkerWindowsEntry(LPVOID Unused) {
  (void)gpuFenceWorkerMain(Unused);
  return 0;
}
#endif

static int ensureGpuFenceWorkerStarted(void) {
#if defined(_WIN32)
  if (!InitOnceExecuteOnce(&GFenceWorkerOnce, initializeGpuFenceThreadOnce,
                           NULL, NULL)) {
    return 0;
  }
#else
  lockGpuMutex(&GFenceWorkerStartMu);
  if (!GFenceWorkerStarted) {
    pthread_t Thread;
    if (pthread_create(&Thread, NULL, gpuFenceWorkerMain, NULL) == 0) {
      pthread_detach(Thread);
      GFenceWorkerStarted = 1;
    }
  }
  unlockGpuMutex(&GFenceWorkerStartMu);
#endif
  return GFenceWorkerStarted;
}

static void enqueueGpuFenceJob(YonaGpuFenceJob *Job) {
  int WorkerStarted = ensureGpuFenceWorkerStarted();
  lockGpuMutex(&GFenceJobMu);
  ++GFenceJobsActive;
  if (!WorkerStarted) {
    unlockGpuMutex(&GFenceJobMu);
    runGpuFenceJob(Job);
    return;
  }
  Job->Next = NULL;
  if (GFenceJobTail != NULL) {
    GFenceJobTail->Next = Job;
  } else {
    GFenceJobHead = Job;
  }
  GFenceJobTail = Job;
  YONA_GPU_FENCE_CV_WAKE_ONE();
  unlockGpuMutex(&GFenceJobMu);
}

static uint32_t yonaVulkanFindMemoryType(uint32_t TypeFilter,
                                         VkMemoryPropertyFlags Props) {
  VkPhysicalDeviceMemoryProperties Mp;
  vkGetPhysicalDeviceMemoryProperties(GPhys, &Mp);
  for (uint32_t I = 0; I < Mp.memoryTypeCount; I++) {
    if ((TypeFilter & (1u << I)) &&
        (Mp.memoryTypes[I].propertyFlags & Props) == Props) {
      return I;
    }
  }
  return UINT32_MAX;
}

static VkResult yonaVulkanCreateInstance(VkInstance *OutInst) {
  VkApplicationInfo App = {0};
  App.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  App.pApplicationName = "yona";
  App.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  App.apiVersion = VK_API_VERSION_1_0;

  VkInstanceCreateInfo Ci = {0};
  Ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  Ci.pApplicationInfo = &App;

#if defined(__APPLE__)
  static const char *inst_exts[] = {"VK_KHR_portability_enumeration"};
  Ci.enabledExtensionCount =
      (uint32_t)(sizeof(inst_exts) / sizeof(inst_exts[0]));
  Ci.ppEnabledExtensionNames = inst_exts;
  Ci.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

  return vkCreateInstance(&Ci, NULL, OutInst);
}

static int pickGpuStubComputeQueue(VkInstance Inst, VkPhysicalDevice *OutPhys,
                                   uint32_t *OutQf) {
  *OutPhys = VK_NULL_HANDLE;
  *OutQf = 0;

  uint32_t N = 0;
  if (vkEnumeratePhysicalDevices(Inst, &N, NULL) != VK_SUCCESS || N == 0) {
    return 0;
  }

  VkPhysicalDevice *Devs =
      (VkPhysicalDevice *)calloc((size_t)N, sizeof(VkPhysicalDevice));
  if (!Devs) {
    return 0;
  }
  if (vkEnumeratePhysicalDevices(Inst, &N, Devs) != VK_SUCCESS) {
    free(Devs);
    return 0;
  }

  VkPhysicalDevice Chosen = VK_NULL_HANDLE;
  uint32_t ChosenQf = 0;

  for (uint32_t I = 0; I < N && Chosen == VK_NULL_HANDLE; I++) {
    uint32_t Qf = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(Devs[I], &Qf, NULL);
    if (Qf == 0) {
      continue;
    }
    VkQueueFamilyProperties *Props = (VkQueueFamilyProperties *)calloc(
        (size_t)Qf, sizeof(VkQueueFamilyProperties));
    if (!Props) {
      continue;
    }
    vkGetPhysicalDeviceQueueFamilyProperties(Devs[I], &Qf, Props);
    for (uint32_t J = 0; J < Qf; J++) {
      if (Props[J].queueFlags & VK_QUEUE_COMPUTE_BIT) {
        Chosen = Devs[I];
        ChosenQf = J;
        break;
      }
    }
    free(Props);
  }

  free(Devs);
  if (Chosen == VK_NULL_HANDLE) {
    return 0;
  }
  *OutPhys = Chosen;
  *OutQf = ChosenQf;
  return 1;
}

static void probeGpuUnlocked(void) {
  if (GProbeDone) {
    return;
  }
  GProbeDone = 1;
  GPhysCount = 0;
  GHaveCompute = 0;

  VkInstance Inst = VK_NULL_HANDLE;
  if (yonaVulkanCreateInstance(&Inst) != VK_SUCCESS) {
    return;
  }

  uint32_t N = 0;
  if (vkEnumeratePhysicalDevices(Inst, &N, NULL) != VK_SUCCESS) {
    vkDestroyInstance(Inst, NULL);
    return;
  }
  GPhysCount = (int)N;

  VkPhysicalDevice Phys = VK_NULL_HANDLE;
  uint32_t Qf = 0;
  if (pickGpuStubComputeQueue(Inst, &Phys, &Qf)) {
    GHaveCompute = 1;
  }

  vkDestroyInstance(Inst, NULL);
}

static int gpuVulkanDisabled(void) {
  const char *Disabled = getenv("YONA_GPU_DISABLE_VULKAN");
  return Disabled != NULL && Disabled[0] != '\0' && strcmp(Disabled, "0") != 0;
}

static void ensureGpuProbe(void) {
  lockGpuMutex(&GMu);
  probeGpuUnlocked();
  unlockGpuMutex(&GMu);
}

int64_t YonaStdGpuAvailable(int64_t Unit) {
  (void)Unit;
  if (gpuVulkanDisabled())
    return 0;
  ensureGpuProbe();
  return GHaveCompute ? 1 : 0;
}

int64_t YonaStdGpuPhysicalDeviceCount(int64_t Unit) {
  (void)Unit;
  if (gpuVulkanDisabled())
    return 0;
  ensureGpuProbe();
  return (int64_t)GPhysCount;
}

YonaTaskRef
YonaStdGpuFloatArrayMul2Async(double *FloatArray,
                              const YonaTypeDescriptor *ResultType) {
  if (FloatArray == NULL) {
    YonaTask *Task = YonaRuntimeTaskCreate(ResultType);
    if (Task)
      YonaRuntimeTaskComplete(Task, -16, 1, NULL);
    return Task;
  }
  int64_t Length = YonaRuntimeFloatArrayLength(FloatArray);
  if (Length < 0)
    Length = 0;
  if (Length > (int64_t)UINT32_MAX)
    Length = (int64_t)UINT32_MAX;
  uint32_t Count = (uint32_t)Length;
  {
    int InitializeResult = YonaRuntimeGpuVulkanContextInitialize();
    if (InitializeResult != 0) {
      YonaTask *Task = YonaRuntimeTaskCreate(ResultType);
      if (Task != NULL) {
        YonaRuntimeTaskComplete(Task, InitializeResult, 1, NULL);
      }
      return Task;
    }
  }
  return YonaRuntimeGpuVulkanFloat64BufferMultiply2Async(FloatArray, Count,
                                                         ResultType, NULL);
}

YonaTaskRef
YonaStdGpuFloatArrayScaleAsync(double Scale, double *FloatArray,
                               const YonaTypeDescriptor *ResultType) {
  if (FloatArray == NULL) {
    YonaTask *Task = YonaRuntimeTaskCreate(ResultType);
    if (Task)
      YonaRuntimeTaskComplete(Task, -16, 1, NULL);
    return Task;
  }
  int64_t Length = YonaRuntimeFloatArrayLength(FloatArray);
  if (Length < 0)
    Length = 0;
  if (Length > (int64_t)UINT32_MAX)
    Length = (int64_t)UINT32_MAX;
  uint32_t Count = (uint32_t)Length;
  {
    int InitializeResult = YonaRuntimeGpuVulkanContextInitialize();
    if (InitializeResult != 0) {
      YonaTask *Task = YonaRuntimeTaskCreate(ResultType);
      if (Task != NULL) {
        YonaRuntimeTaskComplete(Task, InitializeResult, 1, NULL);
      }
      return Task;
    }
  }
  return YonaRuntimeGpuVulkanFloat64BufferScaleAsync(FloatArray, Count, Scale,
                                                     ResultType, NULL);
}

static void teardownFloat64Multiply2PipelineUnlocked(void) {
  if (GF64Pipeline != VK_NULL_HANDLE && GDev != VK_NULL_HANDLE) {
    vkDestroyPipeline(GDev, GF64Pipeline, NULL);
    GF64Pipeline = VK_NULL_HANDLE;
  }
  if (GF64PipeLayout != VK_NULL_HANDLE && GDev != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(GDev, GF64PipeLayout, NULL);
    GF64PipeLayout = VK_NULL_HANDLE;
  }
  if (GF64DescLayout != VK_NULL_HANDLE && GDev != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(GDev, GF64DescLayout, NULL);
    GF64DescLayout = VK_NULL_HANDLE;
  }
  if (GF64Shader != VK_NULL_HANDLE && GDev != VK_NULL_HANDLE) {
    vkDestroyShaderModule(GDev, GF64Shader, NULL);
    GF64Shader = VK_NULL_HANDLE;
  }
  GF64PipelineFail = 0;
}

static void teardownFloat32ScalePipelineUnlocked(void) {
  if (GF32Pipeline != VK_NULL_HANDLE && GDev != VK_NULL_HANDLE) {
    vkDestroyPipeline(GDev, GF32Pipeline, NULL);
    GF32Pipeline = VK_NULL_HANDLE;
  }
  if (GF32PipeLayout != VK_NULL_HANDLE && GDev != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(GDev, GF32PipeLayout, NULL);
    GF32PipeLayout = VK_NULL_HANDLE;
  }
  if (GF32DescLayout != VK_NULL_HANDLE && GDev != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(GDev, GF32DescLayout, NULL);
    GF32DescLayout = VK_NULL_HANDLE;
  }
  if (GF32Shader != VK_NULL_HANDLE && GDev != VK_NULL_HANDLE) {
    vkDestroyShaderModule(GDev, GF32Shader, NULL);
    GF32Shader = VK_NULL_HANDLE;
  }
  GF32PipelineFail = 0;
}

static void teardownFloatReducePipelineUnlocked(void) {
  if (GF64ReducePipe != VK_NULL_HANDLE && GDev != VK_NULL_HANDLE)
    vkDestroyPipeline(GDev, GF64ReducePipe, NULL);
  GF64ReducePipe = VK_NULL_HANDLE;
  if (GF64ReduceLayout != VK_NULL_HANDLE && GDev != VK_NULL_HANDLE)
    vkDestroyPipelineLayout(GDev, GF64ReduceLayout, NULL);
  GF64ReduceLayout = VK_NULL_HANDLE;
  if (GF64ReduceDsl != VK_NULL_HANDLE && GDev != VK_NULL_HANDLE)
    vkDestroyDescriptorSetLayout(GDev, GF64ReduceDsl, NULL);
  GF64ReduceDsl = VK_NULL_HANDLE;
  if (GF64ReduceShader != VK_NULL_HANDLE && GDev != VK_NULL_HANDLE)
    vkDestroyShaderModule(GDev, GF64ReduceShader, NULL);
  GF64ReduceShader = VK_NULL_HANDLE;
  GF64ReduceFail = 0;

  if (GF32ReducePipe != VK_NULL_HANDLE && GDev != VK_NULL_HANDLE)
    vkDestroyPipeline(GDev, GF32ReducePipe, NULL);
  GF32ReducePipe = VK_NULL_HANDLE;
  if (GF32ReduceLayout != VK_NULL_HANDLE && GDev != VK_NULL_HANDLE)
    vkDestroyPipelineLayout(GDev, GF32ReduceLayout, NULL);
  GF32ReduceLayout = VK_NULL_HANDLE;
  if (GF32ReduceDsl != VK_NULL_HANDLE && GDev != VK_NULL_HANDLE)
    vkDestroyDescriptorSetLayout(GDev, GF32ReduceDsl, NULL);
  GF32ReduceDsl = VK_NULL_HANDLE;
  if (GF32ReduceShader != VK_NULL_HANDLE && GDev != VK_NULL_HANDLE)
    vkDestroyShaderModule(GDev, GF32ReduceShader, NULL);
  GF32ReduceShader = VK_NULL_HANDLE;
  GF32ReduceFail = 0;
}

void YonaRuntimeGpuVulkanContextShutdown(void) {
  lockGpuMutex(&GMu);
  lockGpuMutex(&GFenceJobMu);
  while (GFenceJobsActive != 0) {
    YONA_GPU_FENCE_CV_WAIT();
  }
  unlockGpuMutex(&GFenceJobMu);
  if (GDev != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(GDev);
  }
  teardownFloat64Multiply2PipelineUnlocked();
  teardownFloat32ScalePipelineUnlocked();
  teardownFloatReducePipelineUnlocked();
  if (GComputePipe != VK_NULL_HANDLE && GDev != VK_NULL_HANDLE) {
    vkDestroyPipeline(GDev, GComputePipe, NULL);
    GComputePipe = VK_NULL_HANDLE;
  }
  if (GPipeLayout != VK_NULL_HANDLE && GDev != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(GDev, GPipeLayout, NULL);
    GPipeLayout = VK_NULL_HANDLE;
  }
  if (GShaderMod != VK_NULL_HANDLE && GDev != VK_NULL_HANDLE) {
    vkDestroyShaderModule(GDev, GShaderMod, NULL);
    GShaderMod = VK_NULL_HANDLE;
  }
  if (GCmdPool != VK_NULL_HANDLE && GDev != VK_NULL_HANDLE) {
    vkDestroyCommandPool(GDev, GCmdPool, NULL);
    GCmdPool = VK_NULL_HANDLE;
  }
  if (GDev != VK_NULL_HANDLE) {
    vkDestroyDevice(GDev, NULL);
    GDev = VK_NULL_HANDLE;
    GQueue = VK_NULL_HANDLE;
  }
  if (GInst != VK_NULL_HANDLE) {
    vkDestroyInstance(GInst, NULL);
    GInst = VK_NULL_HANDLE;
  }
  GPhys = VK_NULL_HANDLE;
  GStubModernSyncReady = 0;
  GPfnVkQueueSubmit2 = NULL;
  GPfnVkWaitSemaphores = NULL;
  GStubTimelineNext = 0;
  unlockGpuMutex(&GMu);
}

int YonaRuntimeGpuVulkanContextInitialize(void) {
  if (gpuVulkanDisabled())
    return -2;
  lockGpuMutex(&GMu);
  if (GDev != VK_NULL_HANDLE) {
    unlockGpuMutex(&GMu);
    return 0;
  }

  probeGpuUnlocked();

  if (!GHaveCompute) {
    unlockGpuMutex(&GMu);
    return -2;
  }

  if (yonaVulkanCreateInstance(&GInst) != VK_SUCCESS) {
    unlockGpuMutex(&GMu);
    return -3;
  }

  if (!pickGpuStubComputeQueue(GInst, &GPhys, &GQfamily)) {
    vkDestroyInstance(GInst, NULL);
    GInst = VK_NULL_HANDLE;
    unlockGpuMutex(&GMu);
    return -2;
  }

  PFN_vkGetPhysicalDeviceFeatures2 PfnGpdf2 =
      (PFN_vkGetPhysicalDeviceFeatures2)vkGetInstanceProcAddr(
          GInst, "vkGetPhysicalDeviceFeatures2");
  if (!PfnGpdf2)
    PfnGpdf2 = (PFN_vkGetPhysicalDeviceFeatures2)vkGetInstanceProcAddr(
        GInst, "vkGetPhysicalDeviceFeatures2KHR");

  VkPhysicalDeviceFeatures PdevFeat = {0};
  vkGetPhysicalDeviceFeatures(GPhys, &PdevFeat);
  GDeviceShaderF64Enabled = PdevFeat.shaderFloat64 ? 1 : 0;

  int TryModern = 0;
  if (PfnGpdf2 &&
      gpuStubPhysicalDeviceHasExtension(
          VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME) &&
      gpuStubPhysicalDeviceHasExtension(
          VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)) {
    VkPhysicalDeviceTimelineSemaphoreFeatures Qts = {0};
    Qts.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    VkPhysicalDeviceSynchronization2Features Qs2 = {0};
    Qs2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    VkPhysicalDeviceFeatures2 Qf2 = {0};
    Qf2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    Qf2.pNext = &Qts;
    Qts.pNext = &Qs2;
    PfnGpdf2(GPhys, &Qf2);
    if (Qts.timelineSemaphore && Qs2.synchronization2)
      TryModern = 1;
  }

  float Qp = 1.0f;
  VkDeviceQueueCreateInfo Qci = {0};
  Qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  Qci.queueFamilyIndex = GQfamily;
  Qci.queueCount = 1;
  Qci.pQueuePriorities = &Qp;

  static VkPhysicalDeviceFeatures2 SStubDciF2;
  static VkPhysicalDeviceTimelineSemaphoreFeatures SStubDciTf;
  static VkPhysicalDeviceSynchronization2Features SStubDciS2f;
  static const char *SStubExts[12];
  uint32_t ExtN = 0;
#if defined(__APPLE__)
  SStubExts[ExtN++] = "VK_KHR_portability_subset";
#endif

  VkPhysicalDeviceFeatures EnabledFeat = {0};
  VkDeviceCreateInfo Dci = {0};
  Dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  Dci.queueCreateInfoCount = 1;
  Dci.pQueueCreateInfos = &Qci;

  if (TryModern) {
    SStubExts[ExtN++] = VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME;
    SStubExts[ExtN++] = VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME;
    memset(&SStubDciF2, 0, sizeof(SStubDciF2));
    SStubDciF2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    if (GDeviceShaderF64Enabled)
      SStubDciF2.features.shaderFloat64 = VK_TRUE;
    memset(&SStubDciTf, 0, sizeof(SStubDciTf));
    SStubDciTf.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    SStubDciTf.timelineSemaphore = VK_TRUE;
    memset(&SStubDciS2f, 0, sizeof(SStubDciS2f));
    SStubDciS2f.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    SStubDciS2f.synchronization2 = VK_TRUE;
    SStubDciF2.pNext = &SStubDciTf;
    SStubDciTf.pNext = &SStubDciS2f;
    Dci.pNext = &SStubDciF2;
    Dci.pEnabledFeatures = NULL;
    Dci.enabledExtensionCount = ExtN;
    Dci.ppEnabledExtensionNames = SStubExts;
  } else {
    if (GDeviceShaderF64Enabled)
      EnabledFeat.shaderFloat64 = VK_TRUE;
    Dci.pEnabledFeatures = GDeviceShaderF64Enabled ? &EnabledFeat : NULL;
    Dci.enabledExtensionCount = ExtN;
    Dci.ppEnabledExtensionNames = ExtN ? SStubExts : NULL;
  }

  GStubModernSyncReady = 0;
  GPfnVkQueueSubmit2 = NULL;
  GPfnVkWaitSemaphores = NULL;

  VkResult CrDev = vkCreateDevice(GPhys, &Dci, NULL, &GDev);
  if (CrDev != VK_SUCCESS && TryModern) {
    memset(&Dci, 0, sizeof(Dci));
    Dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    Dci.queueCreateInfoCount = 1;
    Dci.pQueueCreateInfos = &Qci;
    Dci.pNext = NULL;
    memset(&EnabledFeat, 0, sizeof(EnabledFeat));
    if (GDeviceShaderF64Enabled)
      EnabledFeat.shaderFloat64 = VK_TRUE;
    Dci.pEnabledFeatures = GDeviceShaderF64Enabled ? &EnabledFeat : NULL;
    ExtN = 0;
#if defined(__APPLE__)
    SStubExts[ExtN++] = "VK_KHR_portability_subset";
#endif
    Dci.enabledExtensionCount = ExtN;
    Dci.ppEnabledExtensionNames = ExtN ? SStubExts : NULL;
    CrDev = vkCreateDevice(GPhys, &Dci, NULL, &GDev);
  }

  if (CrDev != VK_SUCCESS) {
    vkDestroyInstance(GInst, NULL);
    GInst = VK_NULL_HANDLE;
    GPhys = VK_NULL_HANDLE;
    unlockGpuMutex(&GMu);
    return -4;
  }

  GPfnVkQueueSubmit2 =
      (PFN_vkQueueSubmit2)vkGetDeviceProcAddr(GDev, "vkQueueSubmit2");
  GPfnVkWaitSemaphores =
      (PFN_vkWaitSemaphores)vkGetDeviceProcAddr(GDev, "vkWaitSemaphores");
  if (TryModern && GPfnVkQueueSubmit2 && GPfnVkWaitSemaphores)
    GStubModernSyncReady = 1;

  vkGetDeviceQueue(GDev, GQfamily, 0, &GQueue);

  VkCommandPoolCreateInfo Pci = {0};
  Pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  Pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  Pci.queueFamilyIndex = GQfamily;

  if (vkCreateCommandPool(GDev, &Pci, NULL, &GCmdPool) != VK_SUCCESS) {
    vkDestroyDevice(GDev, NULL);
    GDev = VK_NULL_HANDLE;
    GQueue = VK_NULL_HANDLE;
    vkDestroyInstance(GInst, NULL);
    GInst = VK_NULL_HANDLE;
    GPhys = VK_NULL_HANDLE;
    unlockGpuMutex(&GMu);
    return -5;
  }

  VkShaderModuleCreateInfo Smci = {0};
  Smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  Smci.codeSize = sizeof(YonaGpuNopSpv);
  Smci.pCode = YonaGpuNopSpv;

  if (vkCreateShaderModule(GDev, &Smci, NULL, &GShaderMod) != VK_SUCCESS) {
    vkDestroyCommandPool(GDev, GCmdPool, NULL);
    GCmdPool = VK_NULL_HANDLE;
    vkDestroyDevice(GDev, NULL);
    GDev = VK_NULL_HANDLE;
    GQueue = VK_NULL_HANDLE;
    vkDestroyInstance(GInst, NULL);
    GInst = VK_NULL_HANDLE;
    GPhys = VK_NULL_HANDLE;
    unlockGpuMutex(&GMu);
    return -6;
  }

  VkPipelineLayoutCreateInfo Plci = {0};
  Plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  if (vkCreatePipelineLayout(GDev, &Plci, NULL, &GPipeLayout) != VK_SUCCESS) {
    vkDestroyShaderModule(GDev, GShaderMod, NULL);
    GShaderMod = VK_NULL_HANDLE;
    vkDestroyCommandPool(GDev, GCmdPool, NULL);
    GCmdPool = VK_NULL_HANDLE;
    vkDestroyDevice(GDev, NULL);
    GDev = VK_NULL_HANDLE;
    GQueue = VK_NULL_HANDLE;
    vkDestroyInstance(GInst, NULL);
    GInst = VK_NULL_HANDLE;
    GPhys = VK_NULL_HANDLE;
    unlockGpuMutex(&GMu);
    return -7;
  }

  VkPipelineShaderStageCreateInfo Stage = {0};
  Stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  Stage.module = GShaderMod;
  Stage.pName = "main";

  VkComputePipelineCreateInfo Cpci = {0};
  Cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  Cpci.stage = Stage;
  Cpci.layout = GPipeLayout;

  if (vkCreateComputePipelines(GDev, VK_NULL_HANDLE, 1, &Cpci, NULL,
                               &GComputePipe) != VK_SUCCESS) {
    vkDestroyPipelineLayout(GDev, GPipeLayout, NULL);
    GPipeLayout = VK_NULL_HANDLE;
    vkDestroyShaderModule(GDev, GShaderMod, NULL);
    GShaderMod = VK_NULL_HANDLE;
    vkDestroyCommandPool(GDev, GCmdPool, NULL);
    GCmdPool = VK_NULL_HANDLE;
    vkDestroyDevice(GDev, NULL);
    GDev = VK_NULL_HANDLE;
    GQueue = VK_NULL_HANDLE;
    vkDestroyInstance(GInst, NULL);
    GInst = VK_NULL_HANDLE;
    GPhys = VK_NULL_HANDLE;
    unlockGpuMutex(&GMu);
    return -8;
  }

  unlockGpuMutex(&GMu);
  return 0;
}

static int ensureFloat64Multiply2PipelineUnlocked(void) {
  if (GF64Pipeline != VK_NULL_HANDLE) {
    return 0;
  }
  if (GF64PipelineFail) {
    return -21;
  }
  if (!GDeviceShaderF64Enabled) {
    return -20;
  }

  VkShaderModuleCreateInfo Smci = {0};
  Smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  Smci.codeSize = sizeof(YonaGpuFloat64MultiplyTwoSpv);
  Smci.pCode = YonaGpuFloat64MultiplyTwoSpv;
  if (vkCreateShaderModule(GDev, &Smci, NULL, &GF64Shader) != VK_SUCCESS) {
    GF64PipelineFail = 1;
    return -22;
  }

  VkDescriptorSetLayoutBinding Bind = {0};
  Bind.binding = 0;
  Bind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Bind.descriptorCount = 1;
  Bind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

  VkDescriptorSetLayoutCreateInfo Dslci = {0};
  Dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  Dslci.bindingCount = 1;
  Dslci.pBindings = &Bind;
  if (vkCreateDescriptorSetLayout(GDev, &Dslci, NULL, &GF64DescLayout) !=
      VK_SUCCESS) {
    GF64PipelineFail = 1;
    vkDestroyShaderModule(GDev, GF64Shader, NULL);
    GF64Shader = VK_NULL_HANDLE;
    return -22;
  }

  VkPushConstantRange Pcr = {0};
  Pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  Pcr.offset = 0;
  /* SPIR-V push block: uint n @0, double scale @8 (GLSL-aligned). */
  Pcr.size = (uint32_t)(8u + sizeof(double));

  VkPipelineLayoutCreateInfo Plci = {0};
  Plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  Plci.setLayoutCount = 1;
  Plci.pSetLayouts = &GF64DescLayout;
  Plci.pushConstantRangeCount = 1;
  Plci.pPushConstantRanges = &Pcr;
  if (vkCreatePipelineLayout(GDev, &Plci, NULL, &GF64PipeLayout) !=
      VK_SUCCESS) {
    GF64PipelineFail = 1;
    vkDestroyDescriptorSetLayout(GDev, GF64DescLayout, NULL);
    GF64DescLayout = VK_NULL_HANDLE;
    vkDestroyShaderModule(GDev, GF64Shader, NULL);
    GF64Shader = VK_NULL_HANDLE;
    return -22;
  }

  VkPipelineShaderStageCreateInfo Stage = {0};
  Stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  Stage.module = GF64Shader;
  Stage.pName = "main";

  VkComputePipelineCreateInfo Cpci = {0};
  Cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  Cpci.stage = Stage;
  Cpci.layout = GF64PipeLayout;

  if (vkCreateComputePipelines(GDev, VK_NULL_HANDLE, 1, &Cpci, NULL,
                               &GF64Pipeline) != VK_SUCCESS) {
    GF64PipelineFail = 1;
    vkDestroyPipelineLayout(GDev, GF64PipeLayout, NULL);
    GF64PipeLayout = VK_NULL_HANDLE;
    vkDestroyDescriptorSetLayout(GDev, GF64DescLayout, NULL);
    GF64DescLayout = VK_NULL_HANDLE;
    vkDestroyShaderModule(GDev, GF64Shader, NULL);
    GF64Shader = VK_NULL_HANDLE;
    return -22;
  }
  return 0;
}

static int ensureFloat32ScalePipelineUnlocked(void) {
  if (GF32Pipeline != VK_NULL_HANDLE) {
    return 0;
  }
  if (GF32PipelineFail) {
    return -21;
  }

  VkShaderModuleCreateInfo Smci = {0};
  Smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  Smci.codeSize = sizeof(YonaGpuFloat32ScaleSpv);
  Smci.pCode = YonaGpuFloat32ScaleSpv;
  if (vkCreateShaderModule(GDev, &Smci, NULL, &GF32Shader) != VK_SUCCESS) {
    GF32PipelineFail = 1;
    return -22;
  }

  VkDescriptorSetLayoutBinding Bind = {0};
  Bind.binding = 0;
  Bind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Bind.descriptorCount = 1;
  Bind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

  VkDescriptorSetLayoutCreateInfo Dslci = {0};
  Dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  Dslci.bindingCount = 1;
  Dslci.pBindings = &Bind;
  if (vkCreateDescriptorSetLayout(GDev, &Dslci, NULL, &GF32DescLayout) !=
      VK_SUCCESS) {
    GF32PipelineFail = 1;
    vkDestroyShaderModule(GDev, GF32Shader, NULL);
    GF32Shader = VK_NULL_HANDLE;
    return -22;
  }

  VkPushConstantRange Pcr = {0};
  Pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  Pcr.offset = 0;
  /* SPIR-V push block: uint n @0, float scale @4. */
  Pcr.size = 8u;

  VkPipelineLayoutCreateInfo Plci = {0};
  Plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  Plci.setLayoutCount = 1;
  Plci.pSetLayouts = &GF32DescLayout;
  Plci.pushConstantRangeCount = 1;
  Plci.pPushConstantRanges = &Pcr;
  if (vkCreatePipelineLayout(GDev, &Plci, NULL, &GF32PipeLayout) !=
      VK_SUCCESS) {
    GF32PipelineFail = 1;
    vkDestroyDescriptorSetLayout(GDev, GF32DescLayout, NULL);
    GF32DescLayout = VK_NULL_HANDLE;
    vkDestroyShaderModule(GDev, GF32Shader, NULL);
    GF32Shader = VK_NULL_HANDLE;
    return -22;
  }

  VkPipelineShaderStageCreateInfo Stage = {0};
  Stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  Stage.module = GF32Shader;
  Stage.pName = "main";

  VkComputePipelineCreateInfo Cpci = {0};
  Cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  Cpci.stage = Stage;
  Cpci.layout = GF32PipeLayout;

  if (vkCreateComputePipelines(GDev, VK_NULL_HANDLE, 1, &Cpci, NULL,
                               &GF32Pipeline) != VK_SUCCESS) {
    GF32PipelineFail = 1;
    vkDestroyPipelineLayout(GDev, GF32PipeLayout, NULL);
    GF32PipeLayout = VK_NULL_HANDLE;
    vkDestroyDescriptorSetLayout(GDev, GF32DescLayout, NULL);
    GF32DescLayout = VK_NULL_HANDLE;
    vkDestroyShaderModule(GDev, GF32Shader, NULL);
    GF32Shader = VK_NULL_HANDLE;
    return -22;
  }
  return 0;
}

static int ensureFloatReducePipeline(int UseF32) {
  VkPipeline *Pipe = UseF32 ? &GF32ReducePipe : &GF64ReducePipe;
  int *Fail = UseF32 ? &GF32ReduceFail : &GF64ReduceFail;
  VkShaderModule *Sm = UseF32 ? &GF32ReduceShader : &GF64ReduceShader;
  VkDescriptorSetLayout *Dsl = UseF32 ? &GF32ReduceDsl : &GF64ReduceDsl;
  VkPipelineLayout *Pl = UseF32 ? &GF32ReduceLayout : &GF64ReduceLayout;
  if (*Pipe != VK_NULL_HANDLE)
    return 0;
  if (*Fail)
    return -21;
  if (!UseF32 && !GDeviceShaderF64Enabled)
    return -20;

  const uint32_t *Words =
      UseF32 ? YonaGpuFloat32ReduceSpv : YonaGpuFloat64ReduceSpv;
  size_t Nbytes = UseF32 ? sizeof(YonaGpuFloat32ReduceSpv)
                         : sizeof(YonaGpuFloat64ReduceSpv);

  VkShaderModuleCreateInfo Smci = {0};
  Smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  Smci.codeSize = Nbytes;
  Smci.pCode = Words;
  if (vkCreateShaderModule(GDev, &Smci, NULL, Sm) != VK_SUCCESS) {
    *Fail = 1;
    return -22;
  }

  VkDescriptorSetLayoutBinding Binds[2];
  memset(Binds, 0, sizeof Binds);
  Binds[0].binding = 0;
  Binds[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Binds[0].descriptorCount = 1;
  Binds[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  Binds[1].binding = 1;
  Binds[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Binds[1].descriptorCount = 1;
  Binds[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

  VkDescriptorSetLayoutCreateInfo Dslci = {0};
  Dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  Dslci.bindingCount = 2;
  Dslci.pBindings = Binds;
  if (vkCreateDescriptorSetLayout(GDev, &Dslci, NULL, Dsl) != VK_SUCCESS) {
    *Fail = 1;
    vkDestroyShaderModule(GDev, *Sm, NULL);
    *Sm = VK_NULL_HANDLE;
    return -22;
  }

  VkPushConstantRange Pcr = {0};
  Pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  Pcr.offset = 0;
  Pcr.size = 4u;

  VkPipelineLayoutCreateInfo Plci = {0};
  Plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  Plci.setLayoutCount = 1;
  Plci.pSetLayouts = Dsl;
  Plci.pushConstantRangeCount = 1;
  Plci.pPushConstantRanges = &Pcr;
  if (vkCreatePipelineLayout(GDev, &Plci, NULL, Pl) != VK_SUCCESS) {
    *Fail = 1;
    vkDestroyDescriptorSetLayout(GDev, *Dsl, NULL);
    *Dsl = VK_NULL_HANDLE;
    vkDestroyShaderModule(GDev, *Sm, NULL);
    *Sm = VK_NULL_HANDLE;
    return -22;
  }

  VkPipelineShaderStageCreateInfo Stage = {0};
  Stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  Stage.module = *Sm;
  Stage.pName = "main";

  VkComputePipelineCreateInfo Cpci = {0};
  Cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  Cpci.stage = Stage;
  Cpci.layout = *Pl;
  if (vkCreateComputePipelines(GDev, VK_NULL_HANDLE, 1, &Cpci, NULL, Pipe) !=
      VK_SUCCESS) {
    *Fail = 1;
    vkDestroyPipelineLayout(GDev, *Pl, NULL);
    *Pl = VK_NULL_HANDLE;
    vkDestroyDescriptorSetLayout(GDev, *Dsl, NULL);
    *Dsl = VK_NULL_HANDLE;
    vkDestroyShaderModule(GDev, *Sm, NULL);
    *Sm = VK_NULL_HANDLE;
    return -22;
  }
  return 0;
}

static void pushFloat64ScaleCommand(VkCommandBuffer Cmd,
                                    VkPipelineLayout Layout, uint32_t ElemCount,
                                    double Scale) {
  vkCmdPushConstants(Cmd, Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                     sizeof(uint32_t), &ElemCount);
  vkCmdPushConstants(Cmd, Layout, VK_SHADER_STAGE_COMPUTE_BIT, 8,
                     sizeof(double), &Scale);
}

static void pushFloat32ScaleCommand(VkCommandBuffer Cmd,
                                    VkPipelineLayout Layout, uint32_t ElemCount,
                                    float Scale) {
  vkCmdPushConstants(Cmd, Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                     sizeof(uint32_t), &ElemCount);
  vkCmdPushConstants(Cmd, Layout, VK_SHADER_STAGE_COMPUTE_BIT, 4, sizeof(float),
                     &Scale);
}

int YonaRuntimeGpuVulkanFloat64BufferScaleInPlace(double *Elements,
                                                  uint32_t Count,
                                                  double Scale) {
  if (Elements == NULL) {
    return -16;
  }
  if (Count == 0) {
    return 0;
  }

  lockGpuMutex(&GMu);
  if (GDev == VK_NULL_HANDLE || GQueue == VK_NULL_HANDLE ||
      GCmdPool == VK_NULL_HANDLE) {
    unlockGpuMutex(&GMu);
    return -9;
  }

  int UseF32 = 0;
  int Pe = ensureFloat64Multiply2PipelineUnlocked();
  if (Pe == -20) {
    Pe = ensureFloat32ScalePipelineUnlocked();
    UseF32 = (Pe == 0);
  }
  if (Pe != 0) {
    unlockGpuMutex(&GMu);
    return Pe;
  }

  VkDeviceSize Nbytes =
      (VkDeviceSize)Count * (UseF32 ? sizeof(float) : sizeof(double));
  VkBuffer Buf = VK_NULL_HANDLE;
  VkDeviceMemory Mem = VK_NULL_HANDLE;
  VkDescriptorPool Dpool = VK_NULL_HANDLE;
  VkFence Fence = VK_NULL_HANDLE;
  VkCommandBuffer Cmd = VK_NULL_HANDLE;
  void *Mapped = NULL;
  int err = -30;

  VkBufferCreateInfo Bci = {0};
  Bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  Bci.size = Nbytes;
  Bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  Bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  YONA_GPU_STUB_VK_SYNC(vkCreateBuffer(GDev, &Bci, NULL, &Buf),
                        "vkCreateBuffer (f64 SSBO)", -30);

  VkMemoryRequirements Req;
  vkGetBufferMemoryRequirements(GDev, Buf, &Req);
  VkDeviceSize AllocSz = Req.size;
  if (Req.alignment > 0 && (AllocSz % Req.alignment) != 0) {
    AllocSz = ((AllocSz + Req.alignment - 1) / Req.alignment) * Req.alignment;
  }

  uint32_t Mt = yonaVulkanFindMemoryType(
      Req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VkMemoryPropertyFlags MemProps = 0;
  if (Mt == UINT32_MAX) {
    Mt = yonaVulkanFindMemoryType(Req.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (Mt == UINT32_MAX) {
      YonaRuntimeGpuVulkanDeviceSetLastNote(
          "float: no HOST_VISIBLE memory type for f64 SSBO");
      err = -31;
      goto cleanup;
    }
    VkPhysicalDeviceMemoryProperties Mp;
    vkGetPhysicalDeviceMemoryProperties(GPhys, &Mp);
    MemProps = Mp.memoryTypes[Mt].propertyFlags;
  } else {
    MemProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  }

  VkMemoryAllocateInfo Mai = {0};
  Mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  Mai.allocationSize = AllocSz;
  Mai.memoryTypeIndex = Mt;
  YONA_GPU_STUB_VK_SYNC(vkAllocateMemory(GDev, &Mai, NULL, &Mem),
                        "vkAllocateMemory (f64 SSBO)", -32);
  YONA_GPU_STUB_VK_SYNC(vkBindBufferMemory(GDev, Buf, Mem, 0),
                        "vkBindBufferMemory (f64 SSBO)", -33);

  YONA_GPU_STUB_VK_SYNC(vkMapMemory(GDev, Mem, 0, AllocSz, 0, &Mapped),
                        "vkMapMemory (f64 SSBO)", -34);
  if (UseF32) {
    float *Dst = (float *)Mapped;
    for (uint32_t I = 0; I < Count; I++)
      Dst[I] = (float)Elements[I];
  } else {
    memcpy(Mapped, Elements, (size_t)Nbytes);
  }
  if ((MemProps & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
    VkMappedMemoryRange Rng = {0};
    Rng.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    Rng.memory = Mem;
    Rng.offset = 0;
    Rng.size = AllocSz;
    vkFlushMappedMemoryRanges(GDev, 1, &Rng);
  }

  VkDescriptorPoolSize Psz = {0};
  Psz.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Psz.descriptorCount = 1;
  VkDescriptorPoolCreateInfo Dpci = {0};
  Dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  Dpci.maxSets = 1;
  Dpci.poolSizeCount = 1;
  Dpci.pPoolSizes = &Psz;
  YONA_GPU_STUB_VK_SYNC(vkCreateDescriptorPool(GDev, &Dpci, NULL, &Dpool),
                        "vkCreateDescriptorPool (f64)", -35);

  VkDescriptorSetAllocateInfo Dsai = {0};
  Dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  Dsai.descriptorPool = Dpool;
  Dsai.descriptorSetCount = 1;
  Dsai.pSetLayouts = UseF32 ? &GF32DescLayout : &GF64DescLayout;
  VkDescriptorSet Set = VK_NULL_HANDLE;
  YONA_GPU_STUB_VK_SYNC(vkAllocateDescriptorSets(GDev, &Dsai, &Set),
                        "vkAllocateDescriptorSets (f64)", -36);

  VkDescriptorBufferInfo Dbi = {0};
  Dbi.buffer = Buf;
  Dbi.offset = 0;
  Dbi.range = Nbytes;
  VkWriteDescriptorSet Wr = {0};
  Wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  Wr.dstSet = Set;
  Wr.dstBinding = 0;
  Wr.dstArrayElement = 0;
  Wr.descriptorCount = 1;
  Wr.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Wr.pBufferInfo = &Dbi;
  vkUpdateDescriptorSets(GDev, 1, &Wr, 0, NULL);

  VkFenceCreateInfo Fi = {0};
  Fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  YONA_GPU_STUB_VK_SYNC(vkCreateFence(GDev, &Fi, NULL, &Fence),
                        "vkCreateFence (sync f64)", -37);

  VkCommandBufferAllocateInfo Ai = {0};
  Ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  Ai.commandPool = GCmdPool;
  Ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  Ai.commandBufferCount = 1;
  YONA_GPU_STUB_VK_SYNC(vkAllocateCommandBuffers(GDev, &Ai, &Cmd),
                        "vkAllocateCommandBuffers (sync f64)", -38);

  VkCommandBufferBeginInfo Bi = {0};
  Bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  Bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  YONA_GPU_STUB_VK_SYNC(vkBeginCommandBuffer(Cmd, &Bi),
                        "vkBeginCommandBuffer (sync f64)", -39);

  VkMemoryBarrier Pre = {0};
  Pre.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  Pre.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
  Pre.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_HOST_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &Pre, 0,
                       NULL, 0, NULL);

  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                    UseF32 ? GF32Pipeline : GF64Pipeline);
  vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          UseF32 ? GF32PipeLayout : GF64PipeLayout, 0, 1, &Set,
                          0, NULL);
  if (UseF32)
    pushFloat32ScaleCommand(Cmd, GF32PipeLayout, Count, (float)Scale);
  else
    pushFloat64ScaleCommand(Cmd, GF64PipeLayout, Count, Scale);

  uint32_t Gx = (Count + 63u) / 64u;
  vkCmdDispatch(Cmd, Gx, 1, 1);

  VkMemoryBarrier Post = {0};
  Post.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  Post.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  Post.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &Post, 0, NULL, 0,
                       NULL);

  YONA_GPU_STUB_VK_SYNC(vkEndCommandBuffer(Cmd),
                        "vkEndCommandBuffer (sync f64)", -40);

  VkSubmitInfo Si = {0};
  Si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  Si.commandBufferCount = 1;
  Si.pCommandBuffers = &Cmd;
  YONA_GPU_STUB_VK_SYNC(vkQueueSubmit(GQueue, 1, &Si, Fence),
                        "vkQueueSubmit (sync f64)", -41);
  YONA_GPU_STUB_VK_SYNC(vkWaitForFences(GDev, 1, &Fence, VK_TRUE, UINT64_MAX),
                        "vkWaitForFences (sync f64)", -42);

  if ((MemProps & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
    VkMappedMemoryRange Rng = {0};
    Rng.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    Rng.memory = Mem;
    Rng.offset = 0;
    Rng.size = AllocSz;
    vkInvalidateMappedMemoryRanges(GDev, 1, &Rng);
  }
  if (UseF32) {
    const float *Src = (const float *)Mapped;
    for (uint32_t I = 0; I < Count; I++)
      Elements[I] = (double)Src[I];
  } else {
    memcpy(Elements, Mapped, (size_t)Nbytes);
  }
  err = 0;

cleanup:
  if (Cmd != VK_NULL_HANDLE) {
    vkFreeCommandBuffers(GDev, GCmdPool, 1, &Cmd);
  }
  if (Fence != VK_NULL_HANDLE) {
    vkDestroyFence(GDev, Fence, NULL);
  }
  if (Dpool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(GDev, Dpool, NULL);
  }
  if (Mapped != NULL) {
    vkUnmapMemory(GDev, Mem);
    Mapped = NULL;
  }
  if (Mem != VK_NULL_HANDLE) {
    vkFreeMemory(GDev, Mem, NULL);
  }
  if (Buf != VK_NULL_HANDLE) {
    vkDestroyBuffer(GDev, Buf, NULL);
  }
  unlockGpuMutex(&GMu);
  return err;
}

static int allocateGpuStubHostStorageBuffer(VkDeviceSize Nbytes, VkBuffer *Buf,
                                            VkDeviceMemory *Mem, void **Mapped,
                                            VkMemoryPropertyFlags *MemProps,
                                            VkDeviceSize *AllocSz) {
  *Buf = VK_NULL_HANDLE;
  *Mem = VK_NULL_HANDLE;
  *Mapped = NULL;
  VkBufferCreateInfo Bci = {0};
  Bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  Bci.size = Nbytes;
  Bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  Bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(GDev, &Bci, NULL, Buf) != VK_SUCCESS)
    return -30;
  VkMemoryRequirements Req;
  vkGetBufferMemoryRequirements(GDev, *Buf, &Req);
  *AllocSz = Req.size;
  if (Req.alignment > 0 && (*AllocSz % Req.alignment) != 0)
    *AllocSz = ((*AllocSz + Req.alignment - 1) / Req.alignment) * Req.alignment;
  uint32_t Mt = yonaVulkanFindMemoryType(
      Req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (Mt == UINT32_MAX) {
    Mt = yonaVulkanFindMemoryType(Req.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (Mt == UINT32_MAX) {
      vkDestroyBuffer(GDev, *Buf, NULL);
      *Buf = VK_NULL_HANDLE;
      return -31;
    }
    VkPhysicalDeviceMemoryProperties Mp;
    vkGetPhysicalDeviceMemoryProperties(GPhys, &Mp);
    *MemProps = Mp.memoryTypes[Mt].propertyFlags;
  } else {
    *MemProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  }
  VkMemoryAllocateInfo Mai = {0};
  Mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  Mai.allocationSize = *AllocSz;
  Mai.memoryTypeIndex = Mt;
  if (vkAllocateMemory(GDev, &Mai, NULL, Mem) != VK_SUCCESS) {
    vkDestroyBuffer(GDev, *Buf, NULL);
    *Buf = VK_NULL_HANDLE;
    return -32;
  }
  if (vkBindBufferMemory(GDev, *Buf, *Mem, 0) != VK_SUCCESS) {
    vkFreeMemory(GDev, *Mem, NULL);
    *Mem = VK_NULL_HANDLE;
    vkDestroyBuffer(GDev, *Buf, NULL);
    *Buf = VK_NULL_HANDLE;
    return -33;
  }
  if (vkMapMemory(GDev, *Mem, 0, *AllocSz, 0, Mapped) != VK_SUCCESS) {
    vkFreeMemory(GDev, *Mem, NULL);
    *Mem = VK_NULL_HANDLE;
    vkDestroyBuffer(GDev, *Buf, NULL);
    *Buf = VK_NULL_HANDLE;
    return -34;
  }
  return 0;
}

int YonaRuntimeGpuVulkanFloat64BufferReduceSum(const double *Elements,
                                               uint32_t Count, double *OutSum) {
  if (OutSum)
    *OutSum = 0.0;
  if (Elements == NULL)
    return -16;
  if (Count == 0)
    return 0;
  if (!OutSum)
    return -16;

  lockGpuMutex(&GMu);
  if (GDev == VK_NULL_HANDLE || GQueue == VK_NULL_HANDLE ||
      GCmdPool == VK_NULL_HANDLE) {
    unlockGpuMutex(&GMu);
    return -9;
  }

  int UseF32 = 0;
  int Pe = ensureFloatReducePipeline(0);
  if (Pe == -20) {
    Pe = ensureFloatReducePipeline(1);
    UseF32 = (Pe == 0);
  }
  if (Pe != 0) {
    unlockGpuMutex(&GMu);
    return Pe;
  }

  uint32_t Groups = (Count + 63u) / 64u;
  VkDeviceSize Esz = UseF32 ? sizeof(float) : sizeof(double);
  VkDeviceSize NbytesIn = (VkDeviceSize)Count * Esz;
  VkDeviceSize NbytesSums = (VkDeviceSize)Groups * Esz;

  VkBuffer BufIn = VK_NULL_HANDLE, BufSums = VK_NULL_HANDLE;
  VkDeviceMemory MemIn = VK_NULL_HANDLE, MemSums = VK_NULL_HANDLE;
  void *MappedIn = NULL;
  void *MappedSums = NULL;
  VkMemoryPropertyFlags PropsIn = 0, PropsSums = 0;
  VkDeviceSize AllocIn = 0, AllocSums = 0;
  VkDescriptorPool Dpool = VK_NULL_HANDLE;
  VkFence Fence = VK_NULL_HANDLE;
  VkCommandBuffer Cmd = VK_NULL_HANDLE;
  int err = -30;

  err = allocateGpuStubHostStorageBuffer(NbytesIn, &BufIn, &MemIn, &MappedIn,
                                         &PropsIn, &AllocIn);
  if (err != 0) {
    YonaRuntimeGpuVulkanDeviceSetLastNote(
        "float reduce: host SSBO alloc failed (input)");
    goto cleanup;
  }
  err = allocateGpuStubHostStorageBuffer(NbytesSums, &BufSums, &MemSums,
                                         &MappedSums, &PropsSums, &AllocSums);
  if (err != 0) {
    YonaRuntimeGpuVulkanDeviceSetLastNote(
        "float reduce: host SSBO alloc failed (block sums)");
    goto cleanup;
  }

  if (UseF32) {
    float *Dst = (float *)MappedIn;
    for (uint32_t I = 0; I < Count; I++)
      Dst[I] = (float)Elements[I];
  } else {
    memcpy(MappedIn, Elements, (size_t)NbytesIn);
  }
  memset(MappedSums, 0, (size_t)NbytesSums);

  if ((PropsIn & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
    VkMappedMemoryRange Rng = {0};
    Rng.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    Rng.memory = MemIn;
    Rng.offset = 0;
    Rng.size = AllocIn;
    vkFlushMappedMemoryRanges(GDev, 1, &Rng);
  }

  VkDescriptorPoolSize Psz = {0};
  Psz.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Psz.descriptorCount = 2;
  VkDescriptorPoolCreateInfo Dpci = {0};
  Dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  Dpci.maxSets = 1;
  Dpci.poolSizeCount = 1;
  Dpci.pPoolSizes = &Psz;
  YONA_GPU_STUB_VK_SYNC(vkCreateDescriptorPool(GDev, &Dpci, NULL, &Dpool),
                        "vkCreateDescriptorPool (float reduce)", -35);

  VkDescriptorSetAllocateInfo Dsai = {0};
  Dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  Dsai.descriptorPool = Dpool;
  Dsai.descriptorSetCount = 1;
  Dsai.pSetLayouts = UseF32 ? &GF32ReduceDsl : &GF64ReduceDsl;
  VkDescriptorSet Set = VK_NULL_HANDLE;
  YONA_GPU_STUB_VK_SYNC(vkAllocateDescriptorSets(GDev, &Dsai, &Set),
                        "vkAllocateDescriptorSets (float reduce)", -36);

  VkDescriptorBufferInfo Dbi[2];
  memset(Dbi, 0, sizeof Dbi);
  Dbi[0].buffer = BufIn;
  Dbi[0].range = NbytesIn;
  Dbi[1].buffer = BufSums;
  Dbi[1].range = NbytesSums;
  VkWriteDescriptorSet Wr[2];
  memset(Wr, 0, sizeof Wr);
  Wr[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  Wr[0].dstSet = Set;
  Wr[0].dstBinding = 0;
  Wr[0].descriptorCount = 1;
  Wr[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Wr[0].pBufferInfo = &Dbi[0];
  Wr[1] = Wr[0];
  Wr[1].dstBinding = 1;
  Wr[1].pBufferInfo = &Dbi[1];
  vkUpdateDescriptorSets(GDev, 2, Wr, 0, NULL);

  VkFenceCreateInfo Fi = {0};
  Fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  YONA_GPU_STUB_VK_SYNC(vkCreateFence(GDev, &Fi, NULL, &Fence),
                        "vkCreateFence (float reduce)", -37);

  VkCommandBufferAllocateInfo Ai = {0};
  Ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  Ai.commandPool = GCmdPool;
  Ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  Ai.commandBufferCount = 1;
  YONA_GPU_STUB_VK_SYNC(vkAllocateCommandBuffers(GDev, &Ai, &Cmd),
                        "vkAllocateCommandBuffers (float reduce)", -38);

  VkCommandBufferBeginInfo Bi = {0};
  Bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  Bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  YONA_GPU_STUB_VK_SYNC(vkBeginCommandBuffer(Cmd, &Bi),
                        "vkBeginCommandBuffer (float reduce)", -39);

  VkMemoryBarrier Pre = {0};
  Pre.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  Pre.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
  Pre.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
  vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_HOST_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &Pre, 0,
                       NULL, 0, NULL);

  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                    UseF32 ? GF32ReducePipe : GF64ReducePipe);
  vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          UseF32 ? GF32ReduceLayout : GF64ReduceLayout, 0, 1,
                          &Set, 0, NULL);
  vkCmdPushConstants(Cmd, UseF32 ? GF32ReduceLayout : GF64ReduceLayout,
                     VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &Count);
  vkCmdDispatch(Cmd, Groups, 1, 1);

  VkMemoryBarrier Post = {0};
  Post.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  Post.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  Post.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &Post, 0, NULL, 0,
                       NULL);

  YONA_GPU_STUB_VK_SYNC(vkEndCommandBuffer(Cmd),
                        "vkEndCommandBuffer (float reduce)", -40);

  VkSubmitInfo Si = {0};
  Si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  Si.commandBufferCount = 1;
  Si.pCommandBuffers = &Cmd;
  YONA_GPU_STUB_VK_SYNC(vkQueueSubmit(GQueue, 1, &Si, Fence),
                        "vkQueueSubmit (float reduce)", -41);
  YONA_GPU_STUB_VK_SYNC(vkWaitForFences(GDev, 1, &Fence, VK_TRUE, UINT64_MAX),
                        "vkWaitForFences (float reduce)", -42);

  if ((PropsSums & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
    VkMappedMemoryRange Rng = {0};
    Rng.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    Rng.memory = MemSums;
    Rng.offset = 0;
    Rng.size = AllocSums;
    vkInvalidateMappedMemoryRanges(GDev, 1, &Rng);
  }

  double Sum = 0.0;
  if (UseF32) {
    const float *Src = (const float *)MappedSums;
    for (uint32_t I = 0; I < Groups; I++)
      Sum += (double)Src[I];
  } else {
    const double *Src = (const double *)MappedSums;
    for (uint32_t I = 0; I < Groups; I++)
      Sum += Src[I];
  }
  *OutSum = Sum;
  err = 0;

cleanup:
  if (Cmd != VK_NULL_HANDLE)
    vkFreeCommandBuffers(GDev, GCmdPool, 1, &Cmd);
  if (Fence != VK_NULL_HANDLE)
    vkDestroyFence(GDev, Fence, NULL);
  if (Dpool != VK_NULL_HANDLE)
    vkDestroyDescriptorPool(GDev, Dpool, NULL);
  if (MappedIn)
    vkUnmapMemory(GDev, MemIn);
  if (MappedSums)
    vkUnmapMemory(GDev, MemSums);
  if (MemIn != VK_NULL_HANDLE)
    vkFreeMemory(GDev, MemIn, NULL);
  if (MemSums != VK_NULL_HANDLE)
    vkFreeMemory(GDev, MemSums, NULL);
  if (BufIn != VK_NULL_HANDLE)
    vkDestroyBuffer(GDev, BufIn, NULL);
  if (BufSums != VK_NULL_HANDLE)
    vkDestroyBuffer(GDev, BufSums, NULL);
  unlockGpuMutex(&GMu);
  return err;
}

int YonaRuntimeGpuVulkanFloat64BufferMultiply2InPlace(double *Elements,
                                                      uint32_t Count) {
  return YonaRuntimeGpuVulkanFloat64BufferScaleInPlace(Elements, Count, 2.0);
}

YonaTaskRef YonaRuntimeGpuVulkanFloat64BufferScaleAsync(
    double *Elements, uint32_t Count, double Scale,
    const YonaTypeDescriptor *ResultType, YonaTaskGroupRef Group) {
  YonaTask *P = YonaRuntimeTaskCreate(ResultType);
  if (P == NULL) {
    return NULL;
  }
  if (Group != NULL && !YonaRuntimeTaskGroupRegister(Group, P)) {
    YonaRuntimeTaskComplete(P, 0, 1, NULL);
    (void)YonaRuntimeTaskAwait(P);
    return NULL;
  }
  if (Elements == NULL) {
    YonaRuntimeTaskComplete(P, -16, 1, Group);
    return P;
  }
  if (Count == 0) {
    YonaRuntimeTaskComplete(P, 0, 0, Group);
    return P;
  }

  lockGpuMutex(&GMu);
  if (GDev == VK_NULL_HANDLE || GQueue == VK_NULL_HANDLE ||
      GCmdPool == VK_NULL_HANDLE) {
    unlockGpuMutex(&GMu);
    YonaRuntimeTaskComplete(P, -9, 1, Group);
    return P;
  }

  int Pe = ensureFloat64Multiply2PipelineUnlocked();
  if (Pe == -20) {
    unlockGpuMutex(&GMu);
    int Ir =
        YonaRuntimeGpuVulkanFloat64BufferScaleInPlace(Elements, Count, Scale);
    YonaRuntimeTaskComplete(P, Ir, Ir != 0 ? 1 : 0, Group);
    return P;
  }
  if (Pe != 0) {
    unlockGpuMutex(&GMu);
    YonaRuntimeTaskComplete(P, Pe, 1, Group);
    return P;
  }

  VkDeviceSize Nbytes = (VkDeviceSize)Count * sizeof(double);
  VkBuffer Buf = VK_NULL_HANDLE;
  VkDeviceMemory Mem = VK_NULL_HANDLE;
  VkDescriptorPool Dpool = VK_NULL_HANDLE;
  VkFence Fence = VK_NULL_HANDLE;
  VkSemaphore TimelineSem = VK_NULL_HANDLE;
  VkCommandBuffer Cmd = VK_NULL_HANDLE;
  void *Mapped = NULL;
  int err = -30;

  VkBufferCreateInfo Bci = {0};
  Bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  Bci.size = Nbytes;
  Bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  Bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  YONA_GPU_STUB_VK_ASYNC(vkCreateBuffer(GDev, &Bci, NULL, &Buf),
                         "vkCreateBuffer (f64 SSBO async)", -30);

  VkMemoryRequirements Req;
  vkGetBufferMemoryRequirements(GDev, Buf, &Req);
  VkDeviceSize AllocSz = Req.size;
  if (Req.alignment > 0 && (AllocSz % Req.alignment) != 0) {
    AllocSz = ((AllocSz + Req.alignment - 1) / Req.alignment) * Req.alignment;
  }

  uint32_t Mt = yonaVulkanFindMemoryType(
      Req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VkMemoryPropertyFlags MemProps = 0;
  if (Mt == UINT32_MAX) {
    Mt = yonaVulkanFindMemoryType(Req.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (Mt == UINT32_MAX) {
      YonaRuntimeGpuVulkanDeviceSetLastNote(
          "float: no HOST_VISIBLE memory type for f64 SSBO");
      err = -31;
      goto async_fail;
    }
    VkPhysicalDeviceMemoryProperties Mp;
    vkGetPhysicalDeviceMemoryProperties(GPhys, &Mp);
    MemProps = Mp.memoryTypes[Mt].propertyFlags;
  } else {
    MemProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  }

  VkMemoryAllocateInfo Mai = {0};
  Mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  Mai.allocationSize = AllocSz;
  Mai.memoryTypeIndex = Mt;
  YONA_GPU_STUB_VK_ASYNC(vkAllocateMemory(GDev, &Mai, NULL, &Mem),
                         "vkAllocateMemory (f64 SSBO async)", -32);
  YONA_GPU_STUB_VK_ASYNC(vkBindBufferMemory(GDev, Buf, Mem, 0),
                         "vkBindBufferMemory (f64 SSBO async)", -33);

  YONA_GPU_STUB_VK_ASYNC(vkMapMemory(GDev, Mem, 0, AllocSz, 0, &Mapped),
                         "vkMapMemory (f64 SSBO async)", -34);
  memcpy(Mapped, Elements, (size_t)Nbytes);
  if ((MemProps & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
    VkMappedMemoryRange Rng = {0};
    Rng.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    Rng.memory = Mem;
    Rng.offset = 0;
    Rng.size = AllocSz;
    vkFlushMappedMemoryRanges(GDev, 1, &Rng);
  }

  VkDescriptorPoolSize Psz = {0};
  Psz.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Psz.descriptorCount = 1;
  VkDescriptorPoolCreateInfo Dpci = {0};
  Dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  Dpci.maxSets = 1;
  Dpci.poolSizeCount = 1;
  Dpci.pPoolSizes = &Psz;
  YONA_GPU_STUB_VK_ASYNC(vkCreateDescriptorPool(GDev, &Dpci, NULL, &Dpool),
                         "vkCreateDescriptorPool (f64 async)", -35);

  VkDescriptorSetAllocateInfo Dsai = {0};
  Dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  Dsai.descriptorPool = Dpool;
  Dsai.descriptorSetCount = 1;
  Dsai.pSetLayouts = &GF64DescLayout;
  VkDescriptorSet Set = VK_NULL_HANDLE;
  YONA_GPU_STUB_VK_ASYNC(vkAllocateDescriptorSets(GDev, &Dsai, &Set),
                         "vkAllocateDescriptorSets (f64 async)", -36);

  VkDescriptorBufferInfo Dbi = {0};
  Dbi.buffer = Buf;
  Dbi.offset = 0;
  Dbi.range = Nbytes;
  VkWriteDescriptorSet Wr = {0};
  Wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  Wr.dstSet = Set;
  Wr.dstBinding = 0;
  Wr.dstArrayElement = 0;
  Wr.descriptorCount = 1;
  Wr.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Wr.pBufferInfo = &Dbi;
  vkUpdateDescriptorSets(GDev, 1, &Wr, 0, NULL);

  VkCommandBufferAllocateInfo Ai = {0};
  Ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  Ai.commandPool = GCmdPool;
  Ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  Ai.commandBufferCount = 1;
  YONA_GPU_STUB_VK_ASYNC(vkAllocateCommandBuffers(GDev, &Ai, &Cmd),
                         "vkAllocateCommandBuffers (async f64)", -38);

  VkCommandBufferBeginInfo Bi = {0};
  Bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  Bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  YONA_GPU_STUB_VK_ASYNC(vkBeginCommandBuffer(Cmd, &Bi),
                         "vkBeginCommandBuffer (async f64)", -39);

  VkMemoryBarrier Pre = {0};
  Pre.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  Pre.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
  Pre.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_HOST_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &Pre, 0,
                       NULL, 0, NULL);

  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, GF64Pipeline);
  vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, GF64PipeLayout,
                          0, 1, &Set, 0, NULL);
  pushFloat64ScaleCommand(Cmd, GF64PipeLayout, Count, Scale);

  uint32_t Gx = (Count + 63u) / 64u;
  vkCmdDispatch(Cmd, Gx, 1, 1);

  VkMemoryBarrier Post = {0};
  Post.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  Post.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  Post.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &Post, 0, NULL, 0,
                       NULL);

  YONA_GPU_STUB_VK_ASYNC(vkEndCommandBuffer(Cmd),
                         "vkEndCommandBuffer (async f64)", -40);

  int UseTl = gpuStubAsyncUsesTimeline() && GPfnVkQueueSubmit2 != NULL &&
              GPfnVkWaitSemaphores != NULL;
  if (!UseTl) {
    VkFenceCreateInfo Fi = {0};
    Fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    YONA_GPU_STUB_VK_ASYNC(vkCreateFence(GDev, &Fi, NULL, &Fence),
                           "vkCreateFence (async f64)", -37);
  }

  if (Group != NULL && YonaRuntimeTaskGroupIsCancelled(Group)) {
    vkFreeCommandBuffers(GDev, GCmdPool, 1, &Cmd);
    if (Fence != VK_NULL_HANDLE)
      vkDestroyFence(GDev, Fence, NULL);
    vkDestroyDescriptorPool(GDev, Dpool, NULL);
    vkUnmapMemory(GDev, Mem);
    vkFreeMemory(GDev, Mem, NULL);
    vkDestroyBuffer(GDev, Buf, NULL);
    unlockGpuMutex(&GMu);
    YonaRuntimeTaskComplete(P, -887, 1, Group);
    return P;
  }

  uint64_t TlValS = 0;
  if (UseTl) {
    VkSemaphoreTypeCreateInfo Sti = {0};
    Sti.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    Sti.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo Sci = {0};
    Sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    Sci.pNext = &Sti;
    YONA_GPU_STUB_VK_ASYNC(vkCreateSemaphore(GDev, &Sci, NULL, &TimelineSem),
                           "vkCreateSemaphore (async f64 timeline)", -43);
    TlValS = ++GStubTimelineNext;
    if (TlValS == 0)
      TlValS = ++GStubTimelineNext;

    VkCommandBufferSubmitInfo Cbs = {0};
    Cbs.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    Cbs.commandBuffer = Cmd;

    VkSemaphoreSubmitInfo Sig = {0};
    Sig.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    Sig.semaphore = TimelineSem;
    Sig.value = TlValS;
    Sig.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkTimelineSemaphoreSubmitInfo Tlsi = {0};
    Tlsi.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    Tlsi.signalSemaphoreValueCount = 1;
    Tlsi.pSignalSemaphoreValues = &TlValS;

    VkSubmitInfo2 Si2 = {0};
    Si2.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    Si2.pNext = &Tlsi;
    Si2.commandBufferInfoCount = 1;
    Si2.pCommandBufferInfos = &Cbs;
    Si2.signalSemaphoreInfoCount = 1;
    Si2.pSignalSemaphoreInfos = &Sig;

    YONA_GPU_STUB_VK_ASYNC(GPfnVkQueueSubmit2(GQueue, 1, &Si2, VK_NULL_HANDLE),
                           "vkQueueSubmit2 (async f64)", -44);
  } else {
    VkSubmitInfo Si = {0};
    Si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    Si.commandBufferCount = 1;
    Si.pCommandBuffers = &Cmd;
    YONA_GPU_STUB_VK_ASYNC(vkQueueSubmit(GQueue, 1, &Si, Fence),
                           "vkQueueSubmit (async f64)", -41);
  }

  YonaGpuFenceJob *Job = (YonaGpuFenceJob *)calloc(1, sizeof(YonaGpuFenceJob));
  if (Job == NULL) {
    if (UseTl && TimelineSem != VK_NULL_HANDLE &&
        GPfnVkWaitSemaphores != NULL) {
      VkSemaphoreWaitInfo Wi = {0};
      Wi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
      Wi.semaphoreCount = 1;
      Wi.pSemaphores = &TimelineSem;
      Wi.pValues = &TlValS;
      VkResult Wr = GPfnVkWaitSemaphores(GDev, &Wi, UINT64_MAX);
      char Nbuf[256];
      if (Wr != VK_SUCCESS)
        snprintf(Nbuf, sizeof(Nbuf),
                 "float-async: calloc(YonaGpuFenceJob) after submit; drain "
                 "timeline VkResult %d",
                 (int)Wr);
      else
        snprintf(Nbuf, sizeof(Nbuf),
                 "float-async: calloc(YonaGpuFenceJob) after submit (timeline "
                 "OK; likely host OOM)");
      YonaRuntimeGpuVulkanDeviceSetLastNote(Nbuf);
      vkDestroySemaphore(GDev, TimelineSem, NULL);
    } else {
      VkResult Wr = vkWaitForFences(GDev, 1, &Fence, VK_TRUE, UINT64_MAX);
      char Nbuf[256];
      if (Wr != VK_SUCCESS)
        snprintf(
            Nbuf, sizeof(Nbuf),
            "float-async: calloc(YonaGpuFenceJob) after submit; drain fence "
            "VkResult %d",
            (int)Wr);
      else
        snprintf(Nbuf, sizeof(Nbuf),
                 "float-async: calloc(YonaGpuFenceJob) after submit (fence OK; "
                 "likely host OOM)");
      YonaRuntimeGpuVulkanDeviceSetLastNote(Nbuf);
    }
    err = -50;
    goto async_fail;
  }
  Job->Dev = GDev;
  Job->AsyncTimeline = UseTl ? 1 : 0;
  if (UseTl) {
    Job->Fence = VK_NULL_HANDLE;
    Job->TimelineSem = TimelineSem;
    Job->TimelineValue = TlValS;
    TimelineSem = VK_NULL_HANDLE;
  } else {
    Job->Fence = Fence;
    Job->TimelineSem = VK_NULL_HANDLE;
    Job->TimelineValue = 0;
    Fence = VK_NULL_HANDLE;
  }
  Job->Promise = P;
  Job->Group = Group;
  Job->Cmd = Cmd;
  Job->CmdPool = GCmdPool;
  Job->Dpool = Dpool;
  Job->HostElements = Elements;
  Job->Count = Count;
  Job->Mapped = Mapped;
  Job->Mem = Mem;
  Job->Buf = Buf;
  Job->AllocSz = AllocSz;
  Job->MemProps = MemProps;

  Cmd = VK_NULL_HANDLE;
  Dpool = VK_NULL_HANDLE;
  Mapped = NULL;
  Mem = VK_NULL_HANDLE;
  Buf = VK_NULL_HANDLE;

  enqueueGpuFenceJob(Job);
  unlockGpuMutex(&GMu);
  return P;

async_fail:
  if (Cmd != VK_NULL_HANDLE) {
    vkFreeCommandBuffers(GDev, GCmdPool, 1, &Cmd);
  }
  if (Fence != VK_NULL_HANDLE) {
    vkDestroyFence(GDev, Fence, NULL);
  }
  if (TimelineSem != VK_NULL_HANDLE) {
    vkDestroySemaphore(GDev, TimelineSem, NULL);
  }
  if (Dpool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(GDev, Dpool, NULL);
  }
  if (Mapped != NULL) {
    vkUnmapMemory(GDev, Mem);
  }
  if (Mem != VK_NULL_HANDLE) {
    vkFreeMemory(GDev, Mem, NULL);
  }
  if (Buf != VK_NULL_HANDLE) {
    vkDestroyBuffer(GDev, Buf, NULL);
  }
  unlockGpuMutex(&GMu);
  YonaRuntimeTaskComplete(P, err, 1, Group);
  return P;
}

YonaTaskRef YonaRuntimeGpuVulkanFloat64BufferMultiply2Async(
    double *Elements, uint32_t Count, const YonaTypeDescriptor *ResultType,
    YonaTaskGroupRef Group) {
  return YonaRuntimeGpuVulkanFloat64BufferScaleAsync(Elements, Count, 2.0,
                                                     ResultType, Group);
}

int YonaRuntimeGpuVulkanDispatchNopOnce(void) {
  lockGpuMutex(&GMu);
  if (GDev == VK_NULL_HANDLE || GQueue == VK_NULL_HANDLE ||
      GCmdPool == VK_NULL_HANDLE || GComputePipe == VK_NULL_HANDLE) {
    unlockGpuMutex(&GMu);
    return -9;
  }

  VkFence Fence = VK_NULL_HANDLE;
  VkFenceCreateInfo Fi = {0};
  Fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  {
    VkResult Vr = vkCreateFence(GDev, &Fi, NULL, &Fence);
    if (Vr != VK_SUCCESS) {
      noteGpuStubVulkanResult("vkCreateFence (dispatch nop)", Vr);
      unlockGpuMutex(&GMu);
      return -10;
    }
  }

  VkCommandBuffer Cmd = VK_NULL_HANDLE;
  VkCommandBufferAllocateInfo Ai = {0};
  Ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  Ai.commandPool = GCmdPool;
  Ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  Ai.commandBufferCount = 1;
  {
    VkResult Vr = vkAllocateCommandBuffers(GDev, &Ai, &Cmd);
    if (Vr != VK_SUCCESS) {
      noteGpuStubVulkanResult("vkAllocateCommandBuffers (dispatch nop)", Vr);
      vkDestroyFence(GDev, Fence, NULL);
      unlockGpuMutex(&GMu);
      return -11;
    }
  }

  VkCommandBufferBeginInfo Bi = {0};
  Bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  Bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  {
    VkResult Vr = vkBeginCommandBuffer(Cmd, &Bi);
    if (Vr != VK_SUCCESS) {
      noteGpuStubVulkanResult("vkBeginCommandBuffer (dispatch nop)", Vr);
      vkFreeCommandBuffers(GDev, GCmdPool, 1, &Cmd);
      vkDestroyFence(GDev, Fence, NULL);
      unlockGpuMutex(&GMu);
      return -12;
    }
  }

  vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, GComputePipe);
  vkCmdDispatch(Cmd, 1, 1, 1);
  {
    const char *Dde = getenv("YONA_GPU_TEST_DUAL_DISPATCH");
    if (Dde && Dde[0] == '1' && Dde[1] == 0) {
      vkCmdDispatch(Cmd, 1, 1, 1);
    }
  }

  {
    VkResult Vr = vkEndCommandBuffer(Cmd);
    if (Vr != VK_SUCCESS) {
      noteGpuStubVulkanResult("vkEndCommandBuffer (dispatch nop)", Vr);
      vkFreeCommandBuffers(GDev, GCmdPool, 1, &Cmd);
      vkDestroyFence(GDev, Fence, NULL);
      unlockGpuMutex(&GMu);
      return -13;
    }
  }

  VkSubmitInfo Si = {0};
  Si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  Si.commandBufferCount = 1;
  Si.pCommandBuffers = &Cmd;

  {
    VkResult Vr = vkQueueSubmit(GQueue, 1, &Si, Fence);
    if (Vr != VK_SUCCESS) {
      noteGpuStubVulkanResult("vkQueueSubmit (dispatch nop)", Vr);
      vkFreeCommandBuffers(GDev, GCmdPool, 1, &Cmd);
      vkDestroyFence(GDev, Fence, NULL);
      unlockGpuMutex(&GMu);
      return -14;
    }
  }

  {
    VkResult Vr = vkWaitForFences(GDev, 1, &Fence, VK_TRUE, UINT64_MAX);
    if (Vr != VK_SUCCESS) {
      noteGpuStubVulkanResult("vkWaitForFences (dispatch nop)", Vr);
      vkFreeCommandBuffers(GDev, GCmdPool, 1, &Cmd);
      vkDestroyFence(GDev, Fence, NULL);
      unlockGpuMutex(&GMu);
      return -15;
    }
  }

  vkFreeCommandBuffers(GDev, GCmdPool, 1, &Cmd);
  vkDestroyFence(GDev, Fence, NULL);
  unlockGpuMutex(&GMu);
  return 0;
}

typedef struct YonaGpuVulkanPinned {
  VkBuffer Buf;
  VkDeviceMemory Mem;
  VkDeviceSize AllocSz;
  double *Mapped;
  int64_t Count;
} YonaGpuVulkanPinned;

int YonaRuntimeGpuVulkanAllocatePinnedFloats(int64_t Count, double **OutHost,
                                             void **OutOpaque) {
  if (!OutHost || !OutOpaque)
    return -1;
  *OutHost = NULL;
  *OutOpaque = NULL;
  if (Count < 0)
    Count = 0;
  if (YonaRuntimeGpuVulkanContextInitialize() != 0)
    return -1;
  lockGpuMutex(&GMu);
  if (GDev == VK_NULL_HANDLE || GPhys == VK_NULL_HANDLE) {
    unlockGpuMutex(&GMu);
    return -1;
  }

  YonaGpuVulkanPinned *P = (YonaGpuVulkanPinned *)calloc(1, sizeof(*P));
  if (!P) {
    unlockGpuMutex(&GMu);
    return -1;
  }
  P->Count = Count;
  VkDeviceSize Nbytes = (VkDeviceSize)Count * (VkDeviceSize)sizeof(double);
  if (Nbytes == 0)
    Nbytes = sizeof(double); /* Vulkan forbids zero-sized buffers */

  VkBufferCreateInfo Bci = {0};
  Bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  Bci.size = Nbytes;
  Bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
              VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
              VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  Bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VkResult Vr = vkCreateBuffer(GDev, &Bci, NULL, &P->Buf);
  if (Vr != VK_SUCCESS) {
    noteGpuStubVulkanResult("pinned: vkCreateBuffer", Vr);
    free(P);
    unlockGpuMutex(&GMu);
    return -1;
  }

  VkMemoryRequirements Req;
  vkGetBufferMemoryRequirements(GDev, P->Buf, &Req);
  P->AllocSz = Req.size;
  uint32_t Mt = yonaVulkanFindMemoryType(
      Req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (Mt == UINT32_MAX) {
    Mt = yonaVulkanFindMemoryType(Req.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (Mt == UINT32_MAX) {
      YonaRuntimeGpuVulkanDeviceSetLastNote(
          "pinned: no HOST_VISIBLE memory type");
      vkDestroyBuffer(GDev, P->Buf, NULL);
      free(P);
      unlockGpuMutex(&GMu);
      return -1;
    }
  }

  VkMemoryAllocateInfo Mai = {0};
  Mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  Mai.allocationSize = Req.size;
  Mai.memoryTypeIndex = Mt;
  Vr = vkAllocateMemory(GDev, &Mai, NULL, &P->Mem);
  if (Vr != VK_SUCCESS) {
    noteGpuStubVulkanResult("pinned: vkAllocateMemory", Vr);
    vkDestroyBuffer(GDev, P->Buf, NULL);
    free(P);
    unlockGpuMutex(&GMu);
    return -1;
  }
  Vr = vkBindBufferMemory(GDev, P->Buf, P->Mem, 0);
  if (Vr != VK_SUCCESS) {
    noteGpuStubVulkanResult("pinned: vkBindBufferMemory", Vr);
    vkFreeMemory(GDev, P->Mem, NULL);
    vkDestroyBuffer(GDev, P->Buf, NULL);
    free(P);
    unlockGpuMutex(&GMu);
    return -1;
  }
  void *Mapped = NULL;
  Vr = vkMapMemory(GDev, P->Mem, 0, P->AllocSz, 0, &Mapped);
  if (Vr != VK_SUCCESS) {
    noteGpuStubVulkanResult("pinned: vkMapMemory", Vr);
    vkFreeMemory(GDev, P->Mem, NULL);
    vkDestroyBuffer(GDev, P->Buf, NULL);
    free(P);
    unlockGpuMutex(&GMu);
    return -1;
  }
  P->Mapped = (double *)Mapped;
  if (Count > 0)
    memset(P->Mapped, 0, (size_t)Count * sizeof(double));
  else
    memset(P->Mapped, 0, sizeof(double));

  *OutHost = P->Mapped;
  *OutOpaque = P;
  unlockGpuMutex(&GMu);
  return 0;
}

void YonaRuntimeGpuVulkanFreePinnedFloats(void *Opaque) {
  if (!Opaque)
    return;
  YonaGpuVulkanPinned *P = (YonaGpuVulkanPinned *)Opaque;
  lockGpuMutex(&GMu);
  if (GDev != VK_NULL_HANDLE) {
    if (P->Mapped && P->Mem != VK_NULL_HANDLE)
      vkUnmapMemory(GDev, P->Mem);
    if (P->Buf != VK_NULL_HANDLE)
      vkDestroyBuffer(GDev, P->Buf, NULL);
    if (P->Mem != VK_NULL_HANDLE)
      vkFreeMemory(GDev, P->Mem, NULL);
  }
  unlockGpuMutex(&GMu);
  free(P);
}

#else /* !YONA_HAS_VULKAN */

void YonaRuntimeGpuVulkanContextShutdown(void) {}

int YonaRuntimeGpuVulkanContextInitialize(void) { return -1; }

int YonaRuntimeGpuVulkanDispatchNopOnce(void) { return -1; }

int YonaRuntimeGpuVulkanFloat64BufferMultiply2InPlace(double *Elements,
                                                      uint32_t Count) {
  (void)Elements;
  (void)Count;
  return -1;
}

int YonaRuntimeGpuVulkanFloat64BufferScaleInPlace(double *Elements,
                                                  uint32_t Count,
                                                  double Scale) {
  (void)Elements;
  (void)Count;
  (void)Scale;
  return -1;
}

int YonaRuntimeGpuVulkanFloat64BufferReduceSum(const double *Elements,
                                               uint32_t Count, double *OutSum) {
  (void)Elements;
  (void)Count;
  if (OutSum)
    *OutSum = 0.0;
  return -1;
}

YonaTaskRef YonaRuntimeGpuVulkanFloat64BufferScaleAsync(
    double *Elements, uint32_t Count, double Scale,
    const YonaTypeDescriptor *ResultType, YonaTaskGroupRef Group) {
  (void)Elements;
  (void)Count;
  (void)Scale;
  YonaTask *P = YonaRuntimeTaskCreate(ResultType);
  if (P != NULL) {
    if (Group != NULL && !YonaRuntimeTaskGroupRegister(Group, P)) {
      YonaRuntimeTaskComplete(P, 0, 1, NULL);
      (void)YonaRuntimeTaskAwait(P);
      return NULL;
    }
    YonaRuntimeTaskComplete(P, -1, 1, Group);
  }
  return P;
}

YonaTaskRef YonaRuntimeGpuVulkanFloat64BufferMultiply2Async(
    double *Elements, uint32_t Count, const YonaTypeDescriptor *ResultType,
    YonaTaskGroupRef Group) {
  return YonaRuntimeGpuVulkanFloat64BufferScaleAsync(Elements, Count, 2.0,
                                                     ResultType, Group);
}

int64_t YonaStdGpuAvailable(int64_t Unit) {
  (void)Unit;
  return 0;
}

int64_t YonaStdGpuPhysicalDeviceCount(int64_t Unit) {
  (void)Unit;
  return 0;
}

YonaTaskRef
YonaStdGpuFloatArrayMul2Async(double *FloatArray,
                              const YonaTypeDescriptor *ResultType) {
  if (FloatArray == NULL) {
    YonaTask *P = YonaRuntimeTaskCreate(ResultType);
    if (P)
      YonaRuntimeTaskComplete(P, -16, 1, NULL);
    return P;
  }
  int64_t Len64 = YonaRuntimeFloatArrayLength(FloatArray);
  if (Len64 < 0)
    Len64 = 0;
  if (Len64 > (int64_t)UINT32_MAX)
    Len64 = (int64_t)UINT32_MAX;
  uint32_t Count = (uint32_t)Len64;
  return YonaRuntimeGpuVulkanFloat64BufferMultiply2Async(FloatArray, Count,
                                                         ResultType, NULL);
}

YonaTaskRef
YonaStdGpuFloatArrayScaleAsync(double Scale, double *FloatArray,
                               const YonaTypeDescriptor *ResultType) {
  if (FloatArray == NULL) {
    YonaTask *P = YonaRuntimeTaskCreate(ResultType);
    if (P)
      YonaRuntimeTaskComplete(P, -16, 1, NULL);
    return P;
  }
  int64_t Len64 = YonaRuntimeFloatArrayLength(FloatArray);
  if (Len64 < 0)
    Len64 = 0;
  if (Len64 > (int64_t)UINT32_MAX)
    Len64 = (int64_t)UINT32_MAX;
  uint32_t Count = (uint32_t)Len64;
  return YonaRuntimeGpuVulkanFloat64BufferScaleAsync(FloatArray, Count, Scale,
                                                     ResultType, NULL);
}

int YonaRuntimeGpuVulkanAllocatePinnedFloats(int64_t Count, double **OutHost,
                                             void **OutOpaque) {
  (void)Count;
  if (OutHost)
    *OutHost = NULL;
  if (OutOpaque)
    *OutOpaque = NULL;
  return -1;
}

void YonaRuntimeGpuVulkanFreePinnedFloats(void *Opaque) { (void)Opaque; }

#endif
