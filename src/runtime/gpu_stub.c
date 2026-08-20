/// Std\GPU — Vulkan discovery + optional f64 scale/mul2 compute (`floatArrayScaleAsync`,
/// `floatArrayMul2Async` from Yona; shader push scale, mul2 wraps scale 2.0)
/// plus internal context (instance / device / queue / pools / pipelines). See
/// docs/design-gpu-async.md.

#include "runtime/gpu_stub.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern int64_t yona_rt_float_array_length(double* arr);

#if defined(YONA_HAS_VULKAN) && !defined(__ANDROID__)

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

typedef SRWLOCK gpu_stub_mtx_t;
#define GPU_STUB_MTX_INIT SRWLOCK_INIT

static gpu_stub_mtx_t g_mu = SRWLOCK_INIT;

static gpu_stub_mtx_t g_fence_job_mu = SRWLOCK_INIT;
static CONDITION_VARIABLE g_fence_cv = CONDITION_VARIABLE_INIT;
static INIT_ONCE g_fence_worker_once = INIT_ONCE_STATIC_INIT;

static inline void gpu_stub_mtx_lock(gpu_stub_mtx_t* m) {
    AcquireSRWLockExclusive((SRWLOCK*)m);
}

static inline void gpu_stub_mtx_unlock(gpu_stub_mtx_t* m) {
    ReleaseSRWLockExclusive((SRWLOCK*)m);
}

#define gpu_fence_cv_wait_while_empty() SleepConditionVariableSRW(&g_fence_cv, &g_fence_job_mu, INFINITE, 0)

#define gpu_fence_cv_wake_one() WakeConditionVariable(&g_fence_cv)

static DWORD WINAPI yona_gpu_fence_worker_winentry(LPVOID unused);

static BOOL CALLBACK yona_gpu_fence_thread_init_once_cb(PINIT_ONCE init_once, PVOID param, PVOID* ctx) {
    (void)init_once;
    (void)param;
    (void)ctx;
    HANDLE h =
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)yona_gpu_fence_worker_winentry, NULL, 0, NULL);
    if (h != NULL) {
        CloseHandle(h);
    }
    return TRUE;
}

#else /* POSIX */

#include <pthread.h>

typedef pthread_mutex_t gpu_stub_mtx_t;
#define GPU_STUB_MTX_INIT PTHREAD_MUTEX_INITIALIZER

static gpu_stub_mtx_t g_mu = PTHREAD_MUTEX_INITIALIZER;

static gpu_stub_mtx_t g_fence_job_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_fence_job_cv = PTHREAD_COND_INITIALIZER;
static gpu_stub_mtx_t g_fence_worker_start_mu = PTHREAD_MUTEX_INITIALIZER;

static inline void gpu_stub_mtx_lock(gpu_stub_mtx_t* m) {
    pthread_mutex_lock(m);
}

static inline void gpu_stub_mtx_unlock(gpu_stub_mtx_t* m) {
    pthread_mutex_unlock(m);
}

static int g_fence_worker_started;

#define gpu_fence_cv_wait_while_empty() pthread_cond_wait(&g_fence_job_cv, &g_fence_job_mu)

#define gpu_fence_cv_wake_one() pthread_cond_signal(&g_fence_job_cv)

#endif /* POSIX vs WIN32 */

#include <vulkan/vulkan.h>

#include <stdio.h>
#include "yona/runtime/gpu_vulkan_device.h"

#include "runtime/gpu_nop_spv.inl"
#include "runtime/gpu_f64_mul2_spv.inl"
#include "runtime/gpu_f32_scale_spv.inl"
#include "runtime/gpu_f64_reduce_spv.inl"
#include "runtime/gpu_f32_reduce_spv.inl"

#ifndef VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
#define VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR ((VkInstanceCreateFlags)0x00000001)
#endif

static int g_probe_done;
static int g_phys_count;
static int g_have_compute;

/* Lazily created for submit path (not used by Yona externs yet). */
static VkInstance g_inst = VK_NULL_HANDLE;
static VkPhysicalDevice g_phys = VK_NULL_HANDLE;
static VkDevice g_dev = VK_NULL_HANDLE;
static VkQueue g_queue = VK_NULL_HANDLE;
static uint32_t g_qfamily;
static VkCommandPool g_cmd_pool = VK_NULL_HANDLE;
static VkShaderModule g_shader_mod = VK_NULL_HANDLE;
static VkPipelineLayout g_pipe_layout = VK_NULL_HANDLE;
static VkPipeline g_compute_pipe = VK_NULL_HANDLE;

/* Optional f64 SSBO mul2 pipeline (lazy). */
static int g_device_shader_f64_enabled;
static int g_f64_pipeline_fail;
static VkShaderModule g_f64_shader = VK_NULL_HANDLE;
static VkDescriptorSetLayout g_f64_desc_layout = VK_NULL_HANDLE;
static VkPipelineLayout g_f64_pipe_layout = VK_NULL_HANDLE;
static VkPipeline g_f64_pipeline = VK_NULL_HANDLE;

/* f32 scale when shaderFloat64 is missing (typical Metal / MoltenVK). */
static int g_f32_pipeline_fail;
static VkShaderModule g_f32_shader = VK_NULL_HANDLE;
static VkDescriptorSetLayout g_f32_desc_layout = VK_NULL_HANDLE;
static VkPipelineLayout g_f32_pipe_layout = VK_NULL_HANDLE;
static VkPipeline g_f32_pipeline = VK_NULL_HANDLE;

/* f64/f32 block-reduce (two SSBOs: input + per-workgroup sums). */
static int g_f64_reduce_fail;
static VkShaderModule g_f64_reduce_shader = VK_NULL_HANDLE;
static VkDescriptorSetLayout g_f64_reduce_dsl = VK_NULL_HANDLE;
static VkPipelineLayout g_f64_reduce_layout = VK_NULL_HANDLE;
static VkPipeline g_f64_reduce_pipe = VK_NULL_HANDLE;
static int g_f32_reduce_fail;
static VkShaderModule g_f32_reduce_shader = VK_NULL_HANDLE;
static VkDescriptorSetLayout g_f32_reduce_dsl = VK_NULL_HANDLE;
static VkPipelineLayout g_f32_reduce_layout = VK_NULL_HANDLE;
static VkPipeline g_f32_reduce_pipe = VK_NULL_HANDLE;

static PFN_vkQueueSubmit2 g_pfn_vkQueueSubmit2;
static PFN_vkWaitSemaphores g_pfn_vkWaitSemaphores;
static int g_stub_modern_sync_ready;
static uint64_t g_stub_timeline_next;

static int yona_gpu_stub_phys_has_ext(const char* needle) {
    uint32_t n = 0;
    if (vkEnumerateDeviceExtensionProperties(g_phys, NULL, &n, NULL) != VK_SUCCESS || n == 0)
        return 0;
    VkExtensionProperties* exts = (VkExtensionProperties*)calloc((size_t)n, sizeof(VkExtensionProperties));
    if (!exts) return 0;
    VkResult rr = vkEnumerateDeviceExtensionProperties(g_phys, NULL, &n, exts);
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

static int yona_gpu_stub_async_use_timeline(void) {
    if (!g_stub_modern_sync_ready) return 0;
    const char* e = getenv("YONA_GPU_ASYNC_TIMELINE");
    if (e && e[0] == '0' && e[1] == 0) return 0;
    return 1;
}

/* Dedicated thread waits on GPU fences — not the thread pool (design-gpu-async.md). */
typedef struct gpu_fence_job {
    struct gpu_fence_job* next;
    VkDevice dev;
    VkFence fence;
    int async_timeline;
    VkSemaphore timeline_sem;
    uint64_t timeline_value;
    yona_promise_t* promise;
    yona_task_group_t* group;
    VkCommandBuffer cmd;
    VkCommandPool cmd_pool;
    VkDescriptorPool dpool;
    double* host_elements;
    uint32_t count;
    void* mapped;
    VkDeviceMemory mem;
    VkBuffer buf;
    VkDeviceSize alloc_sz;
    VkMemoryPropertyFlags mem_props;
    /* 1 once the user-visible promise was completed (cancel may finish early). */
    int promise_delivered;
    /* 1 when cancel asked to skip writing GPU results back to the host buffer. */
    int discard_host_write;
} gpu_fence_job_t;

static gpu_fence_job_t* g_fence_job_head;
static gpu_fence_job_t* g_fence_job_tail;

static void yona_gpu_stub_note_vk(const char* ctx, VkResult vr) {
    if (vr == VK_SUCCESS) return;
    char prefixed[240];
    snprintf(prefixed, sizeof prefixed, "float: %s", ctx ? ctx : "");
    yona_gpu_vulkan_device_note_vk(prefixed, (int32_t)vr);
}

#define YONA_GPU_STUB_VK_SYNC(stmt, ctx, errn)                                          \
    do {                                                                                \
        VkResult yona_gpu__vr = (stmt);                                                 \
        if (yona_gpu__vr != VK_SUCCESS) {                                               \
            yona_gpu_stub_note_vk((ctx), yona_gpu__vr);                                  \
            err = (errn);                                                               \
            goto cleanup;                                                               \
        }                                                                               \
    } while (0)

#define YONA_GPU_STUB_VK_ASYNC(stmt, ctx, errn)                                         \
    do {                                                                                \
        VkResult yona_gpu__vr = (stmt);                                                 \
        if (yona_gpu__vr != VK_SUCCESS) {                                               \
            yona_gpu_stub_note_vk((ctx), yona_gpu__vr);                                  \
            err = (errn);                                                               \
            goto async_fail;                                                            \
        }                                                                               \
    } while (0)

static void yona_gpu_fence_job_complete_promise(gpu_fence_job_t* j, int64_t res, int is_err) {
    if (__atomic_exchange_n(&j->promise_delivered, 1, __ATOMIC_SEQ_CST) != 0) return;
    yona_rt_promise_complete(j->promise, res, is_err, j->group);
}

static void yona_gpu_fence_worker_do_one(gpu_fence_job_t* j) {
    VkResult wr = VK_SUCCESS;
    const uint64_t poll_ns = 1000000ull; /* 1ms */

    if (j->async_timeline) {
        if (g_pfn_vkWaitSemaphores == NULL) {
            wr = VK_ERROR_FEATURE_NOT_PRESENT;
        } else {
            for (;;) {
                if (j->group != NULL && yona_rt_group_is_cancelled(j->group)) {
                    __atomic_store_n(&j->discard_host_write, 1, __ATOMIC_SEQ_CST);
                    yona_gpu_fence_job_complete_promise(j, -887, 1);
                }
                VkSemaphoreWaitInfo wi = {0};
                wi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
                wi.semaphoreCount = 1;
                wi.pSemaphores = &j->timeline_sem;
                wi.pValues = &j->timeline_value;
                wr = g_pfn_vkWaitSemaphores(j->dev, &wi, poll_ns);
                if (wr == VK_SUCCESS) break;
                if (wr == VK_TIMEOUT) continue;
                break;
            }
        }
        if (wr != VK_SUCCESS && wr != VK_TIMEOUT)
            yona_gpu_stub_note_vk("vkWaitSemaphores (async fence thread)", wr);
        if (j->timeline_sem != VK_NULL_HANDLE) {
            vkDestroySemaphore(j->dev, j->timeline_sem, NULL);
            j->timeline_sem = VK_NULL_HANDLE;
        }
    } else {
        for (;;) {
            if (j->group != NULL && yona_rt_group_is_cancelled(j->group)) {
                __atomic_store_n(&j->discard_host_write, 1, __ATOMIC_SEQ_CST);
                yona_gpu_fence_job_complete_promise(j, -887, 1);
            }
            wr = vkWaitForFences(j->dev, 1, &j->fence, VK_TRUE, poll_ns);
            if (wr == VK_SUCCESS) break;
            if (wr == VK_TIMEOUT) continue;
            break;
        }
        vkDestroyFence(j->dev, j->fence, NULL);
        j->fence = VK_NULL_HANDLE;
        if (wr != VK_SUCCESS && wr != VK_TIMEOUT)
            yona_gpu_stub_note_vk("vkWaitForFences (async fence thread)", wr);
    }
    vkFreeCommandBuffers(j->dev, j->cmd_pool, 1, &j->cmd);
    j->cmd = VK_NULL_HANDLE;

    int discard = __atomic_load_n(&j->discard_host_write, __ATOMIC_SEQ_CST);
    if (wr == VK_SUCCESS && !discard) {
        if ((j->mem_props & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
            VkMappedMemoryRange rng = {0};
            rng.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            rng.memory = j->mem;
            rng.offset = 0;
            rng.size = j->alloc_sz;
            vkInvalidateMappedMemoryRanges(j->dev, 1, &rng);
        }
        memcpy(j->host_elements, j->mapped, (size_t)j->count * sizeof(double));
    }

    if (j->mapped != NULL) {
        vkUnmapMemory(j->dev, j->mem);
        j->mapped = NULL;
    }
    if (j->mem != VK_NULL_HANDLE) {
        vkFreeMemory(j->dev, j->mem, NULL);
        j->mem = VK_NULL_HANDLE;
    }
    if (j->buf != VK_NULL_HANDLE) {
        vkDestroyBuffer(j->dev, j->buf, NULL);
        j->buf = VK_NULL_HANDLE;
    }
    if (j->dpool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(j->dev, j->dpool, NULL);
        j->dpool = VK_NULL_HANDLE;
    }

    /* If cancel already completed the promise, skip; else map success/fail/cancel. */
    if (__atomic_load_n(&j->promise_delivered, __ATOMIC_SEQ_CST) == 0) {
        int64_t res = (wr == VK_SUCCESS) ? 0 : (int64_t)wr;
        int is_err = (wr != VK_SUCCESS) ? 1 : 0;
        if (wr == VK_SUCCESS && j->group != NULL && yona_rt_group_is_cancelled(j->group)) {
            res = -887;
            is_err = 1;
        }
        yona_gpu_fence_job_complete_promise(j, res, is_err);
    }
    free(j);
}

static void* yona_gpu_fence_worker_main(void* unused) {
    (void)unused;
    for (;;) {
        gpu_stub_mtx_lock(&g_fence_job_mu);
        while (g_fence_job_head == NULL) {
            gpu_fence_cv_wait_while_empty();
        }
        gpu_fence_job_t* j = g_fence_job_head;
        g_fence_job_head = j->next;
        if (g_fence_job_head == NULL) {
            g_fence_job_tail = NULL;
        }
        gpu_stub_mtx_unlock(&g_fence_job_mu);
        yona_gpu_fence_worker_do_one(j);
    }
}

#if defined(_WIN32)
static DWORD WINAPI yona_gpu_fence_worker_winentry(LPVOID unused) {
    (void)yona_gpu_fence_worker_main(unused);
    return 0;
}
#endif

static void yona_gpu_fence_worker_ensure_started(void) {
#if defined(_WIN32)
    InitOnceExecuteOnce(&g_fence_worker_once, yona_gpu_fence_thread_init_once_cb, NULL, NULL);
#else
    gpu_stub_mtx_lock(&g_fence_worker_start_mu);
    if (!g_fence_worker_started) {
        pthread_t th;
        if (pthread_create(&th, NULL, yona_gpu_fence_worker_main, NULL) == 0) {
            pthread_detach(th);
            g_fence_worker_started = 1;
        }
    }
    gpu_stub_mtx_unlock(&g_fence_worker_start_mu);
#endif
}

static void yona_gpu_fence_job_enqueue(gpu_fence_job_t* job) {
    yona_gpu_fence_worker_ensure_started();
    gpu_stub_mtx_lock(&g_fence_job_mu);
    job->next = NULL;
    if (g_fence_job_tail != NULL) {
        g_fence_job_tail->next = job;
    } else {
        g_fence_job_head = job;
    }
    g_fence_job_tail = job;
    gpu_fence_cv_wake_one();
    gpu_stub_mtx_unlock(&g_fence_job_mu);
}

static uint32_t yona_vk_find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(g_phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((type_filter & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    return UINT32_MAX;
}

static VkResult yona_vk_create_instance(VkInstance* out_inst) {
    VkApplicationInfo app = {0};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "yona";
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo ci = {0};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;

#if defined(__APPLE__)
    static const char* inst_exts[] = {"VK_KHR_portability_enumeration"};
    ci.enabledExtensionCount = (uint32_t)(sizeof(inst_exts) / sizeof(inst_exts[0]));
    ci.ppEnabledExtensionNames = inst_exts;
    ci.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    return vkCreateInstance(&ci, NULL, out_inst);
}

static int yona_gpu_stub_pick_compute_queue(VkInstance inst, VkPhysicalDevice* out_phys, uint32_t* out_qf) {
    *out_phys = VK_NULL_HANDLE;
    *out_qf = 0;

    uint32_t n = 0;
    if (vkEnumeratePhysicalDevices(inst, &n, NULL) != VK_SUCCESS || n == 0) {
        return 0;
    }

    VkPhysicalDevice* devs = (VkPhysicalDevice*)calloc((size_t)n, sizeof(VkPhysicalDevice));
    if (!devs) {
        return 0;
    }
    if (vkEnumeratePhysicalDevices(inst, &n, devs) != VK_SUCCESS) {
        free(devs);
        return 0;
    }

    VkPhysicalDevice chosen = VK_NULL_HANDLE;
    uint32_t chosen_qf = 0;

    for (uint32_t i = 0; i < n && chosen == VK_NULL_HANDLE; i++) {
        uint32_t qf = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &qf, NULL);
        if (qf == 0) {
            continue;
        }
        VkQueueFamilyProperties* props =
            (VkQueueFamilyProperties*)calloc((size_t)qf, sizeof(VkQueueFamilyProperties));
        if (!props) {
            continue;
        }
        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &qf, props);
        for (uint32_t j = 0; j < qf; j++) {
            if (props[j].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                chosen = devs[i];
                chosen_qf = j;
                break;
            }
        }
        free(props);
    }

    free(devs);
    if (chosen == VK_NULL_HANDLE) {
        return 0;
    }
    *out_phys = chosen;
    *out_qf = chosen_qf;
    return 1;
}

static void gpu_probe_unlocked(void) {
    if (g_probe_done) {
        return;
    }
    g_probe_done = 1;
    g_phys_count = 0;
    g_have_compute = 0;

    VkInstance inst = VK_NULL_HANDLE;
    if (yona_vk_create_instance(&inst) != VK_SUCCESS) {
        return;
    }

    uint32_t n = 0;
    if (vkEnumeratePhysicalDevices(inst, &n, NULL) != VK_SUCCESS) {
        vkDestroyInstance(inst, NULL);
        return;
    }
    g_phys_count = (int)n;

    VkPhysicalDevice phys = VK_NULL_HANDLE;
    uint32_t qf = 0;
    if (yona_gpu_stub_pick_compute_queue(inst, &phys, &qf)) {
        g_have_compute = 1;
    }

    vkDestroyInstance(inst, NULL);
}

static void gpu_ensure_probe(void) {
    gpu_stub_mtx_lock(&g_mu);
    gpu_probe_unlocked();
    gpu_stub_mtx_unlock(&g_mu);
}

int64_t yona_Std_GPU__available(int64_t unit) {
    (void)unit;
    gpu_ensure_probe();
    return g_have_compute ? 1 : 0;
}

int64_t yona_Std_GPU__apiVersion(int64_t unit) {
    (void)unit;
    return 1;
}

int64_t yona_Std_GPU__physicalDeviceCount(int64_t unit) {
    (void)unit;
    gpu_ensure_probe();
    return (int64_t)g_phys_count;
}

yona_promise_t* yona_Std_GPU__floatArrayMul2Async(double* arr) {
    if (arr == NULL) {
        yona_promise_t* p = yona_rt_promise_new();
        if (p) yona_rt_promise_complete(p, -16, 1, NULL);
        return p;
    }
    int64_t len64 = yona_rt_float_array_length(arr);
    if (len64 < 0) len64 = 0;
    if (len64 > (int64_t)UINT32_MAX) len64 = (int64_t)UINT32_MAX;
    uint32_t count = (uint32_t)len64;
    {
        int ci = yona_gpu_vulkan_ctx_init();
        if (ci != 0) {
            yona_promise_t* pe = yona_rt_promise_new();
            if (pe != NULL) {
                yona_rt_promise_complete(pe, ci, 1, NULL);
            }
            return pe;
        }
    }
    return yona_gpu_vulkan_float64_buffer_mul2_async(arr, count, NULL);
}

yona_promise_t* yona_Std_GPU__floatArrayScaleAsync(double scale, double* arr) {
    if (arr == NULL) {
        yona_promise_t* p = yona_rt_promise_new();
        if (p) yona_rt_promise_complete(p, -16, 1, NULL);
        return p;
    }
    int64_t len64 = yona_rt_float_array_length(arr);
    if (len64 < 0) len64 = 0;
    if (len64 > (int64_t)UINT32_MAX) len64 = (int64_t)UINT32_MAX;
    uint32_t count = (uint32_t)len64;
    {
        int ci = yona_gpu_vulkan_ctx_init();
        if (ci != 0) {
            yona_promise_t* pe = yona_rt_promise_new();
            if (pe != NULL) {
                yona_rt_promise_complete(pe, ci, 1, NULL);
            }
            return pe;
        }
    }
    return yona_gpu_vulkan_float64_buffer_scale_async(arr, count, scale, NULL);
}

static void yona_f64_mul2_pipeline_teardown_unlocked(void) {
    if (g_f64_pipeline != VK_NULL_HANDLE && g_dev != VK_NULL_HANDLE) {
        vkDestroyPipeline(g_dev, g_f64_pipeline, NULL);
        g_f64_pipeline = VK_NULL_HANDLE;
    }
    if (g_f64_pipe_layout != VK_NULL_HANDLE && g_dev != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(g_dev, g_f64_pipe_layout, NULL);
        g_f64_pipe_layout = VK_NULL_HANDLE;
    }
    if (g_f64_desc_layout != VK_NULL_HANDLE && g_dev != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(g_dev, g_f64_desc_layout, NULL);
        g_f64_desc_layout = VK_NULL_HANDLE;
    }
    if (g_f64_shader != VK_NULL_HANDLE && g_dev != VK_NULL_HANDLE) {
        vkDestroyShaderModule(g_dev, g_f64_shader, NULL);
        g_f64_shader = VK_NULL_HANDLE;
    }
    g_f64_pipeline_fail = 0;
}

static void yona_f32_scale_pipeline_teardown_unlocked(void) {
    if (g_f32_pipeline != VK_NULL_HANDLE && g_dev != VK_NULL_HANDLE) {
        vkDestroyPipeline(g_dev, g_f32_pipeline, NULL);
        g_f32_pipeline = VK_NULL_HANDLE;
    }
    if (g_f32_pipe_layout != VK_NULL_HANDLE && g_dev != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(g_dev, g_f32_pipe_layout, NULL);
        g_f32_pipe_layout = VK_NULL_HANDLE;
    }
    if (g_f32_desc_layout != VK_NULL_HANDLE && g_dev != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(g_dev, g_f32_desc_layout, NULL);
        g_f32_desc_layout = VK_NULL_HANDLE;
    }
    if (g_f32_shader != VK_NULL_HANDLE && g_dev != VK_NULL_HANDLE) {
        vkDestroyShaderModule(g_dev, g_f32_shader, NULL);
        g_f32_shader = VK_NULL_HANDLE;
    }
    g_f32_pipeline_fail = 0;
}

static void yona_float_reduce_pipeline_teardown_unlocked(void) {
    if (g_f64_reduce_pipe != VK_NULL_HANDLE && g_dev != VK_NULL_HANDLE)
        vkDestroyPipeline(g_dev, g_f64_reduce_pipe, NULL);
    g_f64_reduce_pipe = VK_NULL_HANDLE;
    if (g_f64_reduce_layout != VK_NULL_HANDLE && g_dev != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(g_dev, g_f64_reduce_layout, NULL);
    g_f64_reduce_layout = VK_NULL_HANDLE;
    if (g_f64_reduce_dsl != VK_NULL_HANDLE && g_dev != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(g_dev, g_f64_reduce_dsl, NULL);
    g_f64_reduce_dsl = VK_NULL_HANDLE;
    if (g_f64_reduce_shader != VK_NULL_HANDLE && g_dev != VK_NULL_HANDLE)
        vkDestroyShaderModule(g_dev, g_f64_reduce_shader, NULL);
    g_f64_reduce_shader = VK_NULL_HANDLE;
    g_f64_reduce_fail = 0;

    if (g_f32_reduce_pipe != VK_NULL_HANDLE && g_dev != VK_NULL_HANDLE)
        vkDestroyPipeline(g_dev, g_f32_reduce_pipe, NULL);
    g_f32_reduce_pipe = VK_NULL_HANDLE;
    if (g_f32_reduce_layout != VK_NULL_HANDLE && g_dev != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(g_dev, g_f32_reduce_layout, NULL);
    g_f32_reduce_layout = VK_NULL_HANDLE;
    if (g_f32_reduce_dsl != VK_NULL_HANDLE && g_dev != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(g_dev, g_f32_reduce_dsl, NULL);
    g_f32_reduce_dsl = VK_NULL_HANDLE;
    if (g_f32_reduce_shader != VK_NULL_HANDLE && g_dev != VK_NULL_HANDLE)
        vkDestroyShaderModule(g_dev, g_f32_reduce_shader, NULL);
    g_f32_reduce_shader = VK_NULL_HANDLE;
    g_f32_reduce_fail = 0;
}

void yona_gpu_vulkan_ctx_shutdown(void) {
    gpu_stub_mtx_lock(&g_mu);
    if (g_dev != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(g_dev);
    }
    yona_f64_mul2_pipeline_teardown_unlocked();
    yona_f32_scale_pipeline_teardown_unlocked();
    yona_float_reduce_pipeline_teardown_unlocked();
    if (g_compute_pipe != VK_NULL_HANDLE && g_dev != VK_NULL_HANDLE) {
        vkDestroyPipeline(g_dev, g_compute_pipe, NULL);
        g_compute_pipe = VK_NULL_HANDLE;
    }
    if (g_pipe_layout != VK_NULL_HANDLE && g_dev != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(g_dev, g_pipe_layout, NULL);
        g_pipe_layout = VK_NULL_HANDLE;
    }
    if (g_shader_mod != VK_NULL_HANDLE && g_dev != VK_NULL_HANDLE) {
        vkDestroyShaderModule(g_dev, g_shader_mod, NULL);
        g_shader_mod = VK_NULL_HANDLE;
    }
    if (g_cmd_pool != VK_NULL_HANDLE && g_dev != VK_NULL_HANDLE) {
        vkDestroyCommandPool(g_dev, g_cmd_pool, NULL);
        g_cmd_pool = VK_NULL_HANDLE;
    }
    if (g_dev != VK_NULL_HANDLE) {
        vkDestroyDevice(g_dev, NULL);
        g_dev = VK_NULL_HANDLE;
        g_queue = VK_NULL_HANDLE;
    }
    if (g_inst != VK_NULL_HANDLE) {
        vkDestroyInstance(g_inst, NULL);
        g_inst = VK_NULL_HANDLE;
    }
    g_phys = VK_NULL_HANDLE;
    g_stub_modern_sync_ready = 0;
    g_pfn_vkQueueSubmit2 = NULL;
    g_pfn_vkWaitSemaphores = NULL;
    g_stub_timeline_next = 0;
    gpu_stub_mtx_unlock(&g_mu);
}

int yona_gpu_vulkan_ctx_init(void) {
    gpu_stub_mtx_lock(&g_mu);
    if (g_dev != VK_NULL_HANDLE) {
        gpu_stub_mtx_unlock(&g_mu);
        return 0;
    }

    gpu_probe_unlocked();

    if (!g_have_compute) {
        gpu_stub_mtx_unlock(&g_mu);
        return -2;
    }

    if (yona_vk_create_instance(&g_inst) != VK_SUCCESS) {
        gpu_stub_mtx_unlock(&g_mu);
        return -3;
    }

    if (!yona_gpu_stub_pick_compute_queue(g_inst, &g_phys, &g_qfamily)) {
        vkDestroyInstance(g_inst, NULL);
        g_inst = VK_NULL_HANDLE;
        gpu_stub_mtx_unlock(&g_mu);
        return -2;
    }

    PFN_vkGetPhysicalDeviceFeatures2 pfn_gpdf2 =
        (PFN_vkGetPhysicalDeviceFeatures2)vkGetInstanceProcAddr(g_inst, "vkGetPhysicalDeviceFeatures2");
    if (!pfn_gpdf2)
        pfn_gpdf2 =
            (PFN_vkGetPhysicalDeviceFeatures2)vkGetInstanceProcAddr(g_inst, "vkGetPhysicalDeviceFeatures2KHR");

    VkPhysicalDeviceFeatures pdev_feat = {0};
    vkGetPhysicalDeviceFeatures(g_phys, &pdev_feat);
    g_device_shader_f64_enabled = pdev_feat.shaderFloat64 ? 1 : 0;

    int try_modern = 0;
    if (pfn_gpdf2 && yona_gpu_stub_phys_has_ext(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME) &&
        yona_gpu_stub_phys_has_ext(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)) {
        VkPhysicalDeviceTimelineSemaphoreFeatures qts = {0};
        qts.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
        VkPhysicalDeviceSynchronization2Features qs2 = {0};
        qs2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
        VkPhysicalDeviceFeatures2 qf2 = {0};
        qf2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        qf2.pNext = &qts;
        qts.pNext = &qs2;
        pfn_gpdf2(g_phys, &qf2);
        if (qts.timelineSemaphore && qs2.synchronization2) try_modern = 1;
    }

    float qp = 1.0f;
    VkDeviceQueueCreateInfo qci = {0};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = g_qfamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &qp;

    static VkPhysicalDeviceFeatures2 s_stub_dci_f2;
    static VkPhysicalDeviceTimelineSemaphoreFeatures s_stub_dci_tf;
    static VkPhysicalDeviceSynchronization2Features s_stub_dci_s2f;
    static const char* s_stub_exts[12];
    uint32_t ext_n = 0;
#if defined(__APPLE__)
    s_stub_exts[ext_n++] = "VK_KHR_portability_subset";
#endif

    VkPhysicalDeviceFeatures enabled_feat = {0};
    VkDeviceCreateInfo dci = {0};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;

    if (try_modern) {
        s_stub_exts[ext_n++] = VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME;
        s_stub_exts[ext_n++] = VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME;
        memset(&s_stub_dci_f2, 0, sizeof(s_stub_dci_f2));
        s_stub_dci_f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        if (g_device_shader_f64_enabled) s_stub_dci_f2.features.shaderFloat64 = VK_TRUE;
        memset(&s_stub_dci_tf, 0, sizeof(s_stub_dci_tf));
        s_stub_dci_tf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
        s_stub_dci_tf.timelineSemaphore = VK_TRUE;
        memset(&s_stub_dci_s2f, 0, sizeof(s_stub_dci_s2f));
        s_stub_dci_s2f.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
        s_stub_dci_s2f.synchronization2 = VK_TRUE;
        s_stub_dci_f2.pNext = &s_stub_dci_tf;
        s_stub_dci_tf.pNext = &s_stub_dci_s2f;
        dci.pNext = &s_stub_dci_f2;
        dci.pEnabledFeatures = NULL;
        dci.enabledExtensionCount = ext_n;
        dci.ppEnabledExtensionNames = s_stub_exts;
    } else {
        if (g_device_shader_f64_enabled) enabled_feat.shaderFloat64 = VK_TRUE;
        dci.pEnabledFeatures = g_device_shader_f64_enabled ? &enabled_feat : NULL;
        dci.enabledExtensionCount = ext_n;
        dci.ppEnabledExtensionNames = ext_n ? s_stub_exts : NULL;
    }

    g_stub_modern_sync_ready = 0;
    g_pfn_vkQueueSubmit2 = NULL;
    g_pfn_vkWaitSemaphores = NULL;

    VkResult cr_dev = vkCreateDevice(g_phys, &dci, NULL, &g_dev);
    if (cr_dev != VK_SUCCESS && try_modern) {
        memset(&dci, 0, sizeof(dci));
        dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qci;
        dci.pNext = NULL;
        memset(&enabled_feat, 0, sizeof(enabled_feat));
        if (g_device_shader_f64_enabled) enabled_feat.shaderFloat64 = VK_TRUE;
        dci.pEnabledFeatures = g_device_shader_f64_enabled ? &enabled_feat : NULL;
        ext_n = 0;
#if defined(__APPLE__)
        s_stub_exts[ext_n++] = "VK_KHR_portability_subset";
#endif
        dci.enabledExtensionCount = ext_n;
        dci.ppEnabledExtensionNames = ext_n ? s_stub_exts : NULL;
        cr_dev = vkCreateDevice(g_phys, &dci, NULL, &g_dev);
    }

    if (cr_dev != VK_SUCCESS) {
        vkDestroyInstance(g_inst, NULL);
        g_inst = VK_NULL_HANDLE;
        g_phys = VK_NULL_HANDLE;
        gpu_stub_mtx_unlock(&g_mu);
        return -4;
    }

    g_pfn_vkQueueSubmit2 = (PFN_vkQueueSubmit2)vkGetDeviceProcAddr(g_dev, "vkQueueSubmit2");
    g_pfn_vkWaitSemaphores = (PFN_vkWaitSemaphores)vkGetDeviceProcAddr(g_dev, "vkWaitSemaphores");
    if (try_modern && g_pfn_vkQueueSubmit2 && g_pfn_vkWaitSemaphores)
        g_stub_modern_sync_ready = 1;

    vkGetDeviceQueue(g_dev, g_qfamily, 0, &g_queue);

    VkCommandPoolCreateInfo pci = {0};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = g_qfamily;

    if (vkCreateCommandPool(g_dev, &pci, NULL, &g_cmd_pool) != VK_SUCCESS) {
        vkDestroyDevice(g_dev, NULL);
        g_dev = VK_NULL_HANDLE;
        g_queue = VK_NULL_HANDLE;
        vkDestroyInstance(g_inst, NULL);
        g_inst = VK_NULL_HANDLE;
        g_phys = VK_NULL_HANDLE;
        gpu_stub_mtx_unlock(&g_mu);
        return -5;
    }

    VkShaderModuleCreateInfo smci = {0};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = sizeof(yona_gpu_nop_spv_bytes);
    smci.pCode = (const uint32_t*)(const void*)yona_gpu_nop_spv_bytes;

    if (vkCreateShaderModule(g_dev, &smci, NULL, &g_shader_mod) != VK_SUCCESS) {
        vkDestroyCommandPool(g_dev, g_cmd_pool, NULL);
        g_cmd_pool = VK_NULL_HANDLE;
        vkDestroyDevice(g_dev, NULL);
        g_dev = VK_NULL_HANDLE;
        g_queue = VK_NULL_HANDLE;
        vkDestroyInstance(g_inst, NULL);
        g_inst = VK_NULL_HANDLE;
        g_phys = VK_NULL_HANDLE;
        gpu_stub_mtx_unlock(&g_mu);
        return -6;
    }

    VkPipelineLayoutCreateInfo plci = {0};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (vkCreatePipelineLayout(g_dev, &plci, NULL, &g_pipe_layout) != VK_SUCCESS) {
        vkDestroyShaderModule(g_dev, g_shader_mod, NULL);
        g_shader_mod = VK_NULL_HANDLE;
        vkDestroyCommandPool(g_dev, g_cmd_pool, NULL);
        g_cmd_pool = VK_NULL_HANDLE;
        vkDestroyDevice(g_dev, NULL);
        g_dev = VK_NULL_HANDLE;
        g_queue = VK_NULL_HANDLE;
        vkDestroyInstance(g_inst, NULL);
        g_inst = VK_NULL_HANDLE;
        g_phys = VK_NULL_HANDLE;
        gpu_stub_mtx_unlock(&g_mu);
        return -7;
    }

    VkPipelineShaderStageCreateInfo stage = {0};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = g_shader_mod;
    stage.pName = "main";

    VkComputePipelineCreateInfo cpci = {0};
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage = stage;
    cpci.layout = g_pipe_layout;

    if (vkCreateComputePipelines(g_dev, VK_NULL_HANDLE, 1, &cpci, NULL, &g_compute_pipe) != VK_SUCCESS) {
        vkDestroyPipelineLayout(g_dev, g_pipe_layout, NULL);
        g_pipe_layout = VK_NULL_HANDLE;
        vkDestroyShaderModule(g_dev, g_shader_mod, NULL);
        g_shader_mod = VK_NULL_HANDLE;
        vkDestroyCommandPool(g_dev, g_cmd_pool, NULL);
        g_cmd_pool = VK_NULL_HANDLE;
        vkDestroyDevice(g_dev, NULL);
        g_dev = VK_NULL_HANDLE;
        g_queue = VK_NULL_HANDLE;
        vkDestroyInstance(g_inst, NULL);
        g_inst = VK_NULL_HANDLE;
        g_phys = VK_NULL_HANDLE;
        gpu_stub_mtx_unlock(&g_mu);
        return -8;
    }

    gpu_stub_mtx_unlock(&g_mu);
    return 0;
}

static int yona_ensure_f64_mul2_pipeline_unlocked(void) {
    if (g_f64_pipeline != VK_NULL_HANDLE) {
        return 0;
    }
    if (g_f64_pipeline_fail) {
        return -21;
    }
    if (!g_device_shader_f64_enabled) {
        return -20;
    }

    VkShaderModuleCreateInfo smci = {0};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = sizeof(yona_gpu_f64_mul2_spv_bytes);
    smci.pCode = (const uint32_t*)(const void*)yona_gpu_f64_mul2_spv_bytes;
    if (vkCreateShaderModule(g_dev, &smci, NULL, &g_f64_shader) != VK_SUCCESS) {
        g_f64_pipeline_fail = 1;
        return -22;
    }

    VkDescriptorSetLayoutBinding bind = {0};
    bind.binding = 0;
    bind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bind.descriptorCount = 1;
    bind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dslci = {0};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 1;
    dslci.pBindings = &bind;
    if (vkCreateDescriptorSetLayout(g_dev, &dslci, NULL, &g_f64_desc_layout) != VK_SUCCESS) {
        g_f64_pipeline_fail = 1;
        vkDestroyShaderModule(g_dev, g_f64_shader, NULL);
        g_f64_shader = VK_NULL_HANDLE;
        return -22;
    }

    VkPushConstantRange pcr = {0};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset = 0;
    /* SPIR-V push block: uint n @0, double scale @8 (GLSL-aligned). */
    pcr.size = (uint32_t)(8u + sizeof(double));

    VkPipelineLayoutCreateInfo plci = {0};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &g_f64_desc_layout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(g_dev, &plci, NULL, &g_f64_pipe_layout) != VK_SUCCESS) {
        g_f64_pipeline_fail = 1;
        vkDestroyDescriptorSetLayout(g_dev, g_f64_desc_layout, NULL);
        g_f64_desc_layout = VK_NULL_HANDLE;
        vkDestroyShaderModule(g_dev, g_f64_shader, NULL);
        g_f64_shader = VK_NULL_HANDLE;
        return -22;
    }

    VkPipelineShaderStageCreateInfo stage = {0};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = g_f64_shader;
    stage.pName = "main";

    VkComputePipelineCreateInfo cpci = {0};
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage = stage;
    cpci.layout = g_f64_pipe_layout;

    if (vkCreateComputePipelines(g_dev, VK_NULL_HANDLE, 1, &cpci, NULL, &g_f64_pipeline) != VK_SUCCESS) {
        g_f64_pipeline_fail = 1;
        vkDestroyPipelineLayout(g_dev, g_f64_pipe_layout, NULL);
        g_f64_pipe_layout = VK_NULL_HANDLE;
        vkDestroyDescriptorSetLayout(g_dev, g_f64_desc_layout, NULL);
        g_f64_desc_layout = VK_NULL_HANDLE;
        vkDestroyShaderModule(g_dev, g_f64_shader, NULL);
        g_f64_shader = VK_NULL_HANDLE;
        return -22;
    }
    return 0;
}

static int yona_ensure_f32_scale_pipeline_unlocked(void) {
    if (g_f32_pipeline != VK_NULL_HANDLE) {
        return 0;
    }
    if (g_f32_pipeline_fail) {
        return -21;
    }

    VkShaderModuleCreateInfo smci = {0};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = sizeof(yona_gpu_f32_scale_spv_bytes);
    smci.pCode = (const uint32_t*)(const void*)yona_gpu_f32_scale_spv_bytes;
    if (vkCreateShaderModule(g_dev, &smci, NULL, &g_f32_shader) != VK_SUCCESS) {
        g_f32_pipeline_fail = 1;
        return -22;
    }

    VkDescriptorSetLayoutBinding bind = {0};
    bind.binding = 0;
    bind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bind.descriptorCount = 1;
    bind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dslci = {0};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 1;
    dslci.pBindings = &bind;
    if (vkCreateDescriptorSetLayout(g_dev, &dslci, NULL, &g_f32_desc_layout) != VK_SUCCESS) {
        g_f32_pipeline_fail = 1;
        vkDestroyShaderModule(g_dev, g_f32_shader, NULL);
        g_f32_shader = VK_NULL_HANDLE;
        return -22;
    }

    VkPushConstantRange pcr = {0};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset = 0;
    /* SPIR-V push block: uint n @0, float scale @4. */
    pcr.size = 8u;

    VkPipelineLayoutCreateInfo plci = {0};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &g_f32_desc_layout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(g_dev, &plci, NULL, &g_f32_pipe_layout) != VK_SUCCESS) {
        g_f32_pipeline_fail = 1;
        vkDestroyDescriptorSetLayout(g_dev, g_f32_desc_layout, NULL);
        g_f32_desc_layout = VK_NULL_HANDLE;
        vkDestroyShaderModule(g_dev, g_f32_shader, NULL);
        g_f32_shader = VK_NULL_HANDLE;
        return -22;
    }

    VkPipelineShaderStageCreateInfo stage = {0};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = g_f32_shader;
    stage.pName = "main";

    VkComputePipelineCreateInfo cpci = {0};
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage = stage;
    cpci.layout = g_f32_pipe_layout;

    if (vkCreateComputePipelines(g_dev, VK_NULL_HANDLE, 1, &cpci, NULL, &g_f32_pipeline) != VK_SUCCESS) {
        g_f32_pipeline_fail = 1;
        vkDestroyPipelineLayout(g_dev, g_f32_pipe_layout, NULL);
        g_f32_pipe_layout = VK_NULL_HANDLE;
        vkDestroyDescriptorSetLayout(g_dev, g_f32_desc_layout, NULL);
        g_f32_desc_layout = VK_NULL_HANDLE;
        vkDestroyShaderModule(g_dev, g_f32_shader, NULL);
        g_f32_shader = VK_NULL_HANDLE;
        return -22;
    }
    return 0;
}

static int yona_ensure_float_reduce_pipe(int use_f32) {
    VkPipeline* pipe = use_f32 ? &g_f32_reduce_pipe : &g_f64_reduce_pipe;
    int* fail = use_f32 ? &g_f32_reduce_fail : &g_f64_reduce_fail;
    VkShaderModule* sm = use_f32 ? &g_f32_reduce_shader : &g_f64_reduce_shader;
    VkDescriptorSetLayout* dsl = use_f32 ? &g_f32_reduce_dsl : &g_f64_reduce_dsl;
    VkPipelineLayout* pl = use_f32 ? &g_f32_reduce_layout : &g_f64_reduce_layout;
    if (*pipe != VK_NULL_HANDLE)
        return 0;
    if (*fail)
        return -21;
    if (!use_f32 && !g_device_shader_f64_enabled)
        return -20;

    const unsigned char* bytes = use_f32 ? yona_gpu_f32_reduce_spv_bytes : yona_gpu_f64_reduce_spv_bytes;
    size_t nbytes = use_f32 ? sizeof(yona_gpu_f32_reduce_spv_bytes) : sizeof(yona_gpu_f64_reduce_spv_bytes);

    VkShaderModuleCreateInfo smci = {0};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = nbytes;
    smci.pCode = (const uint32_t*)(const void*)bytes;
    if (vkCreateShaderModule(g_dev, &smci, NULL, sm) != VK_SUCCESS) {
        *fail = 1;
        return -22;
    }

    VkDescriptorSetLayoutBinding binds[2];
    memset(binds, 0, sizeof binds);
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    binds[1].binding = 1;
    binds[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dslci = {0};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 2;
    dslci.pBindings = binds;
    if (vkCreateDescriptorSetLayout(g_dev, &dslci, NULL, dsl) != VK_SUCCESS) {
        *fail = 1;
        vkDestroyShaderModule(g_dev, *sm, NULL);
        *sm = VK_NULL_HANDLE;
        return -22;
    }

    VkPushConstantRange pcr = {0};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset = 0;
    pcr.size = 4u;

    VkPipelineLayoutCreateInfo plci = {0};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = dsl;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(g_dev, &plci, NULL, pl) != VK_SUCCESS) {
        *fail = 1;
        vkDestroyDescriptorSetLayout(g_dev, *dsl, NULL);
        *dsl = VK_NULL_HANDLE;
        vkDestroyShaderModule(g_dev, *sm, NULL);
        *sm = VK_NULL_HANDLE;
        return -22;
    }

    VkPipelineShaderStageCreateInfo stage = {0};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = *sm;
    stage.pName = "main";

    VkComputePipelineCreateInfo cpci = {0};
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage = stage;
    cpci.layout = *pl;
    if (vkCreateComputePipelines(g_dev, VK_NULL_HANDLE, 1, &cpci, NULL, pipe) != VK_SUCCESS) {
        *fail = 1;
        vkDestroyPipelineLayout(g_dev, *pl, NULL);
        *pl = VK_NULL_HANDLE;
        vkDestroyDescriptorSetLayout(g_dev, *dsl, NULL);
        *dsl = VK_NULL_HANDLE;
        vkDestroyShaderModule(g_dev, *sm, NULL);
        *sm = VK_NULL_HANDLE;
        return -22;
    }
    return 0;
}

static void vk_cmd_push_f64_scale(VkCommandBuffer cmd, VkPipelineLayout layout, uint32_t elem_count,
                                  double scale) {
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &elem_count);
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 8, sizeof(double), &scale);
}

static void vk_cmd_push_f32_scale(VkCommandBuffer cmd, VkPipelineLayout layout, uint32_t elem_count,
                                  float scale) {
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &elem_count);
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 4, sizeof(float), &scale);
}

int yona_gpu_vulkan_float64_buffer_scale_inplace(double* elements, uint32_t count, double scale) {
    if (elements == NULL) {
        return -16;
    }
    if (count == 0) {
        return 0;
    }

    gpu_stub_mtx_lock(&g_mu);
    if (g_dev == VK_NULL_HANDLE || g_queue == VK_NULL_HANDLE || g_cmd_pool == VK_NULL_HANDLE) {
        gpu_stub_mtx_unlock(&g_mu);
        return -9;
    }

    int use_f32 = 0;
    int pe = yona_ensure_f64_mul2_pipeline_unlocked();
    if (pe == -20) {
        pe = yona_ensure_f32_scale_pipeline_unlocked();
        use_f32 = (pe == 0);
    }
    if (pe != 0) {
        gpu_stub_mtx_unlock(&g_mu);
        return pe;
    }

    VkDeviceSize nbytes = (VkDeviceSize)count * (use_f32 ? sizeof(float) : sizeof(double));
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkDescriptorPool dpool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    void* mapped = NULL;
    int err = -30;

    VkBufferCreateInfo bci = {0};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = nbytes;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    YONA_GPU_STUB_VK_SYNC(vkCreateBuffer(g_dev, &bci, NULL, &buf), "vkCreateBuffer (f64 SSBO)", -30);

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g_dev, buf, &req);
    VkDeviceSize alloc_sz = req.size;
    if (req.alignment > 0 && (alloc_sz % req.alignment) != 0) {
        alloc_sz = ((alloc_sz + req.alignment - 1) / req.alignment) * req.alignment;
    }

    uint32_t mt =
        yona_vk_find_memory_type(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryPropertyFlags mem_props = 0;
    if (mt == UINT32_MAX) {
        mt = yona_vk_find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        if (mt == UINT32_MAX) {
            yona_gpu_vulkan_device_set_last_note("float: no HOST_VISIBLE memory type for f64 SSBO");
            err = -31;
            goto cleanup;
        }
        VkPhysicalDeviceMemoryProperties mp;
        vkGetPhysicalDeviceMemoryProperties(g_phys, &mp);
        mem_props = mp.memoryTypes[mt].propertyFlags;
    } else {
        mem_props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }

    VkMemoryAllocateInfo mai = {0};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = alloc_sz;
    mai.memoryTypeIndex = mt;
    YONA_GPU_STUB_VK_SYNC(vkAllocateMemory(g_dev, &mai, NULL, &mem), "vkAllocateMemory (f64 SSBO)", -32);
    YONA_GPU_STUB_VK_SYNC(vkBindBufferMemory(g_dev, buf, mem, 0), "vkBindBufferMemory (f64 SSBO)", -33);

    YONA_GPU_STUB_VK_SYNC(vkMapMemory(g_dev, mem, 0, alloc_sz, 0, &mapped), "vkMapMemory (f64 SSBO)", -34);
    if (use_f32) {
        float* dst = (float*)mapped;
        for (uint32_t i = 0; i < count; i++) dst[i] = (float)elements[i];
    } else {
        memcpy(mapped, elements, (size_t)nbytes);
    }
    if ((mem_props & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
        VkMappedMemoryRange rng = {0};
        rng.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        rng.memory = mem;
        rng.offset = 0;
        rng.size = alloc_sz;
        vkFlushMappedMemoryRanges(g_dev, 1, &rng);
    }

    VkDescriptorPoolSize psz = {0};
    psz.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    psz.descriptorCount = 1;
    VkDescriptorPoolCreateInfo dpci = {0};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &psz;
    YONA_GPU_STUB_VK_SYNC(vkCreateDescriptorPool(g_dev, &dpci, NULL, &dpool),
                          "vkCreateDescriptorPool (f64)", -35);

    VkDescriptorSetAllocateInfo dsai = {0};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = dpool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = use_f32 ? &g_f32_desc_layout : &g_f64_desc_layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    YONA_GPU_STUB_VK_SYNC(vkAllocateDescriptorSets(g_dev, &dsai, &set), "vkAllocateDescriptorSets (f64)",
                          -36);

    VkDescriptorBufferInfo dbi = {0};
    dbi.buffer = buf;
    dbi.offset = 0;
    dbi.range = nbytes;
    VkWriteDescriptorSet wr = {0};
    wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr.dstSet = set;
    wr.dstBinding = 0;
    wr.dstArrayElement = 0;
    wr.descriptorCount = 1;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    wr.pBufferInfo = &dbi;
    vkUpdateDescriptorSets(g_dev, 1, &wr, 0, NULL);

    VkFenceCreateInfo fi = {0};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    YONA_GPU_STUB_VK_SYNC(vkCreateFence(g_dev, &fi, NULL, &fence), "vkCreateFence (sync f64)", -37);

    VkCommandBufferAllocateInfo ai = {0};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = g_cmd_pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    YONA_GPU_STUB_VK_SYNC(vkAllocateCommandBuffers(g_dev, &ai, &cmd), "vkAllocateCommandBuffers (sync f64)",
                          -38);

    VkCommandBufferBeginInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    YONA_GPU_STUB_VK_SYNC(vkBeginCommandBuffer(cmd, &bi), "vkBeginCommandBuffer (sync f64)", -39);

    VkMemoryBarrier pre = {0};
    pre.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    pre.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    pre.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &pre,
        0, NULL, 0, NULL);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, use_f32 ? g_f32_pipeline : g_f64_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            use_f32 ? g_f32_pipe_layout : g_f64_pipe_layout, 0, 1, &set, 0, NULL);
    if (use_f32)
        vk_cmd_push_f32_scale(cmd, g_f32_pipe_layout, count, (float)scale);
    else
        vk_cmd_push_f64_scale(cmd, g_f64_pipe_layout, count, scale);

    uint32_t gx = (count + 63u) / 64u;
    vkCmdDispatch(cmd, gx, 1, 1);

    VkMemoryBarrier post = {0};
    post.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    post.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    post.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &post,
        0, NULL, 0, NULL);

    YONA_GPU_STUB_VK_SYNC(vkEndCommandBuffer(cmd), "vkEndCommandBuffer (sync f64)", -40);

    VkSubmitInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    YONA_GPU_STUB_VK_SYNC(vkQueueSubmit(g_queue, 1, &si, fence), "vkQueueSubmit (sync f64)", -41);
    YONA_GPU_STUB_VK_SYNC(vkWaitForFences(g_dev, 1, &fence, VK_TRUE, UINT64_MAX),
                          "vkWaitForFences (sync f64)", -42);

    if ((mem_props & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
        VkMappedMemoryRange rng = {0};
        rng.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        rng.memory = mem;
        rng.offset = 0;
        rng.size = alloc_sz;
        vkInvalidateMappedMemoryRanges(g_dev, 1, &rng);
    }
    if (use_f32) {
        const float* src = (const float*)mapped;
        for (uint32_t i = 0; i < count; i++) elements[i] = (double)src[i];
    } else {
        memcpy(elements, mapped, (size_t)nbytes);
    }
    err = 0;

cleanup:
    if (cmd != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(g_dev, g_cmd_pool, 1, &cmd);
    }
    if (fence != VK_NULL_HANDLE) {
        vkDestroyFence(g_dev, fence, NULL);
    }
    if (dpool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(g_dev, dpool, NULL);
    }
    if (mapped != NULL) {
        vkUnmapMemory(g_dev, mem);
        mapped = NULL;
    }
    if (mem != VK_NULL_HANDLE) {
        vkFreeMemory(g_dev, mem, NULL);
    }
    if (buf != VK_NULL_HANDLE) {
        vkDestroyBuffer(g_dev, buf, NULL);
    }
    gpu_stub_mtx_unlock(&g_mu);
    return err;
}

static int yona_gpu_stub_alloc_host_ssbo(VkDeviceSize nbytes, VkBuffer* buf, VkDeviceMemory* mem,
                                         void** mapped, VkMemoryPropertyFlags* mem_props,
                                         VkDeviceSize* alloc_sz) {
    *buf = VK_NULL_HANDLE;
    *mem = VK_NULL_HANDLE;
    *mapped = NULL;
    VkBufferCreateInfo bci = {0};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = nbytes;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(g_dev, &bci, NULL, buf) != VK_SUCCESS)
        return -30;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g_dev, *buf, &req);
    *alloc_sz = req.size;
    if (req.alignment > 0 && (*alloc_sz % req.alignment) != 0)
        *alloc_sz = ((*alloc_sz + req.alignment - 1) / req.alignment) * req.alignment;
    uint32_t mt =
        yona_vk_find_memory_type(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == UINT32_MAX) {
        mt = yona_vk_find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        if (mt == UINT32_MAX) {
            vkDestroyBuffer(g_dev, *buf, NULL);
            *buf = VK_NULL_HANDLE;
            return -31;
        }
        VkPhysicalDeviceMemoryProperties mp;
        vkGetPhysicalDeviceMemoryProperties(g_phys, &mp);
        *mem_props = mp.memoryTypes[mt].propertyFlags;
    } else {
        *mem_props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }
    VkMemoryAllocateInfo mai = {0};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = *alloc_sz;
    mai.memoryTypeIndex = mt;
    if (vkAllocateMemory(g_dev, &mai, NULL, mem) != VK_SUCCESS) {
        vkDestroyBuffer(g_dev, *buf, NULL);
        *buf = VK_NULL_HANDLE;
        return -32;
    }
    if (vkBindBufferMemory(g_dev, *buf, *mem, 0) != VK_SUCCESS) {
        vkFreeMemory(g_dev, *mem, NULL);
        *mem = VK_NULL_HANDLE;
        vkDestroyBuffer(g_dev, *buf, NULL);
        *buf = VK_NULL_HANDLE;
        return -33;
    }
    if (vkMapMemory(g_dev, *mem, 0, *alloc_sz, 0, mapped) != VK_SUCCESS) {
        vkFreeMemory(g_dev, *mem, NULL);
        *mem = VK_NULL_HANDLE;
        vkDestroyBuffer(g_dev, *buf, NULL);
        *buf = VK_NULL_HANDLE;
        return -34;
    }
    return 0;
}

int yona_gpu_vulkan_float64_buffer_reduce_sum(const double* elements, uint32_t count, double* out_sum) {
    if (out_sum)
        *out_sum = 0.0;
    if (elements == NULL)
        return -16;
    if (count == 0)
        return 0;
    if (!out_sum)
        return -16;

    gpu_stub_mtx_lock(&g_mu);
    if (g_dev == VK_NULL_HANDLE || g_queue == VK_NULL_HANDLE || g_cmd_pool == VK_NULL_HANDLE) {
        gpu_stub_mtx_unlock(&g_mu);
        return -9;
    }

    int use_f32 = 0;
    int pe = yona_ensure_float_reduce_pipe(0);
    if (pe == -20) {
        pe = yona_ensure_float_reduce_pipe(1);
        use_f32 = (pe == 0);
    }
    if (pe != 0) {
        gpu_stub_mtx_unlock(&g_mu);
        return pe;
    }

    uint32_t groups = (count + 63u) / 64u;
    VkDeviceSize esz = use_f32 ? sizeof(float) : sizeof(double);
    VkDeviceSize nbytes_in = (VkDeviceSize)count * esz;
    VkDeviceSize nbytes_sums = (VkDeviceSize)groups * esz;

    VkBuffer buf_in = VK_NULL_HANDLE, buf_sums = VK_NULL_HANDLE;
    VkDeviceMemory mem_in = VK_NULL_HANDLE, mem_sums = VK_NULL_HANDLE;
    void* mapped_in = NULL;
    void* mapped_sums = NULL;
    VkMemoryPropertyFlags props_in = 0, props_sums = 0;
    VkDeviceSize alloc_in = 0, alloc_sums = 0;
    VkDescriptorPool dpool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    int err = -30;

    err = yona_gpu_stub_alloc_host_ssbo(nbytes_in, &buf_in, &mem_in, &mapped_in, &props_in, &alloc_in);
    if (err != 0) {
        yona_gpu_vulkan_device_set_last_note("float reduce: host SSBO alloc failed (input)");
        goto cleanup;
    }
    err = yona_gpu_stub_alloc_host_ssbo(nbytes_sums, &buf_sums, &mem_sums, &mapped_sums, &props_sums,
                                        &alloc_sums);
    if (err != 0) {
        yona_gpu_vulkan_device_set_last_note("float reduce: host SSBO alloc failed (block sums)");
        goto cleanup;
    }

    if (use_f32) {
        float* dst = (float*)mapped_in;
        for (uint32_t i = 0; i < count; i++)
            dst[i] = (float)elements[i];
    } else {
        memcpy(mapped_in, elements, (size_t)nbytes_in);
    }
    memset(mapped_sums, 0, (size_t)nbytes_sums);

    if ((props_in & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
        VkMappedMemoryRange rng = {0};
        rng.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        rng.memory = mem_in;
        rng.offset = 0;
        rng.size = alloc_in;
        vkFlushMappedMemoryRanges(g_dev, 1, &rng);
    }

    VkDescriptorPoolSize psz = {0};
    psz.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    psz.descriptorCount = 2;
    VkDescriptorPoolCreateInfo dpci = {0};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &psz;
    YONA_GPU_STUB_VK_SYNC(vkCreateDescriptorPool(g_dev, &dpci, NULL, &dpool),
                          "vkCreateDescriptorPool (float reduce)", -35);

    VkDescriptorSetAllocateInfo dsai = {0};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = dpool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = use_f32 ? &g_f32_reduce_dsl : &g_f64_reduce_dsl;
    VkDescriptorSet set = VK_NULL_HANDLE;
    YONA_GPU_STUB_VK_SYNC(vkAllocateDescriptorSets(g_dev, &dsai, &set),
                          "vkAllocateDescriptorSets (float reduce)", -36);

    VkDescriptorBufferInfo dbi[2];
    memset(dbi, 0, sizeof dbi);
    dbi[0].buffer = buf_in;
    dbi[0].range = nbytes_in;
    dbi[1].buffer = buf_sums;
    dbi[1].range = nbytes_sums;
    VkWriteDescriptorSet wr[2];
    memset(wr, 0, sizeof wr);
    wr[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr[0].dstSet = set;
    wr[0].dstBinding = 0;
    wr[0].descriptorCount = 1;
    wr[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    wr[0].pBufferInfo = &dbi[0];
    wr[1] = wr[0];
    wr[1].dstBinding = 1;
    wr[1].pBufferInfo = &dbi[1];
    vkUpdateDescriptorSets(g_dev, 2, wr, 0, NULL);

    VkFenceCreateInfo fi = {0};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    YONA_GPU_STUB_VK_SYNC(vkCreateFence(g_dev, &fi, NULL, &fence), "vkCreateFence (float reduce)", -37);

    VkCommandBufferAllocateInfo ai = {0};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = g_cmd_pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    YONA_GPU_STUB_VK_SYNC(vkAllocateCommandBuffers(g_dev, &ai, &cmd),
                          "vkAllocateCommandBuffers (float reduce)", -38);

    VkCommandBufferBeginInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    YONA_GPU_STUB_VK_SYNC(vkBeginCommandBuffer(cmd, &bi), "vkBeginCommandBuffer (float reduce)", -39);

    VkMemoryBarrier pre = {0};
    pre.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    pre.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    pre.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &pre,
        0, NULL, 0, NULL);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                      use_f32 ? g_f32_reduce_pipe : g_f64_reduce_pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            use_f32 ? g_f32_reduce_layout : g_f64_reduce_layout, 0, 1, &set, 0, NULL);
    vkCmdPushConstants(cmd, use_f32 ? g_f32_reduce_layout : g_f64_reduce_layout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &count);
    vkCmdDispatch(cmd, groups, 1, 1);

    VkMemoryBarrier post = {0};
    post.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    post.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    post.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &post,
        0, NULL, 0, NULL);

    YONA_GPU_STUB_VK_SYNC(vkEndCommandBuffer(cmd), "vkEndCommandBuffer (float reduce)", -40);

    VkSubmitInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    YONA_GPU_STUB_VK_SYNC(vkQueueSubmit(g_queue, 1, &si, fence), "vkQueueSubmit (float reduce)", -41);
    YONA_GPU_STUB_VK_SYNC(vkWaitForFences(g_dev, 1, &fence, VK_TRUE, UINT64_MAX),
                          "vkWaitForFences (float reduce)", -42);

    if ((props_sums & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
        VkMappedMemoryRange rng = {0};
        rng.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        rng.memory = mem_sums;
        rng.offset = 0;
        rng.size = alloc_sums;
        vkInvalidateMappedMemoryRanges(g_dev, 1, &rng);
    }

    double sum = 0.0;
    if (use_f32) {
        const float* src = (const float*)mapped_sums;
        for (uint32_t i = 0; i < groups; i++)
            sum += (double)src[i];
    } else {
        const double* src = (const double*)mapped_sums;
        for (uint32_t i = 0; i < groups; i++)
            sum += src[i];
    }
    *out_sum = sum;
    err = 0;

cleanup:
    if (cmd != VK_NULL_HANDLE)
        vkFreeCommandBuffers(g_dev, g_cmd_pool, 1, &cmd);
    if (fence != VK_NULL_HANDLE)
        vkDestroyFence(g_dev, fence, NULL);
    if (dpool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(g_dev, dpool, NULL);
    if (mapped_in)
        vkUnmapMemory(g_dev, mem_in);
    if (mapped_sums)
        vkUnmapMemory(g_dev, mem_sums);
    if (mem_in != VK_NULL_HANDLE)
        vkFreeMemory(g_dev, mem_in, NULL);
    if (mem_sums != VK_NULL_HANDLE)
        vkFreeMemory(g_dev, mem_sums, NULL);
    if (buf_in != VK_NULL_HANDLE)
        vkDestroyBuffer(g_dev, buf_in, NULL);
    if (buf_sums != VK_NULL_HANDLE)
        vkDestroyBuffer(g_dev, buf_sums, NULL);
    gpu_stub_mtx_unlock(&g_mu);
    return err;
}

int yona_gpu_vulkan_float64_buffer_mul2_inplace(double* elements, uint32_t count) {
    return yona_gpu_vulkan_float64_buffer_scale_inplace(elements, count, 2.0);
}

yona_promise_t* yona_gpu_vulkan_float64_buffer_scale_async(double* elements, uint32_t count, double scale,
                                                           yona_task_group_t* group) {
    yona_promise_t* p = yona_rt_promise_new();
    if (p == NULL) {
        return NULL;
    }
    if (elements == NULL) {
        yona_rt_promise_complete(p, -16, 1, NULL);
        return p;
    }
    if (count == 0) {
        yona_rt_promise_complete(p, 0, 0, NULL);
        return p;
    }

    gpu_stub_mtx_lock(&g_mu);
    if (g_dev == VK_NULL_HANDLE || g_queue == VK_NULL_HANDLE || g_cmd_pool == VK_NULL_HANDLE) {
        gpu_stub_mtx_unlock(&g_mu);
        yona_rt_promise_complete(p, -9, 1, NULL);
        return p;
    }

    int pe = yona_ensure_f64_mul2_pipeline_unlocked();
    if (pe == -20) {
        gpu_stub_mtx_unlock(&g_mu);
        int ir = yona_gpu_vulkan_float64_buffer_scale_inplace(elements, count, scale);
        if (group != NULL) yona_rt_group_register(group, p);
        yona_rt_promise_complete(p, ir, ir != 0 ? 1 : 0, group);
        return p;
    }
    if (pe != 0) {
        gpu_stub_mtx_unlock(&g_mu);
        yona_rt_promise_complete(p, pe, 1, NULL);
        return p;
    }

    VkDeviceSize nbytes = (VkDeviceSize)count * sizeof(double);
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkDescriptorPool dpool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore timeline_sem = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    void* mapped = NULL;
    int err = -30;

    VkBufferCreateInfo bci = {0};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = nbytes;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    YONA_GPU_STUB_VK_ASYNC(vkCreateBuffer(g_dev, &bci, NULL, &buf), "vkCreateBuffer (f64 SSBO async)", -30);

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g_dev, buf, &req);
    VkDeviceSize alloc_sz = req.size;
    if (req.alignment > 0 && (alloc_sz % req.alignment) != 0) {
        alloc_sz = ((alloc_sz + req.alignment - 1) / req.alignment) * req.alignment;
    }

    uint32_t mt =
        yona_vk_find_memory_type(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryPropertyFlags mem_props = 0;
    if (mt == UINT32_MAX) {
        mt = yona_vk_find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        if (mt == UINT32_MAX) {
            yona_gpu_vulkan_device_set_last_note("float: no HOST_VISIBLE memory type for f64 SSBO");
            err = -31;
            goto async_fail;
        }
        VkPhysicalDeviceMemoryProperties mp;
        vkGetPhysicalDeviceMemoryProperties(g_phys, &mp);
        mem_props = mp.memoryTypes[mt].propertyFlags;
    } else {
        mem_props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }

    VkMemoryAllocateInfo mai = {0};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = alloc_sz;
    mai.memoryTypeIndex = mt;
    YONA_GPU_STUB_VK_ASYNC(vkAllocateMemory(g_dev, &mai, NULL, &mem),
                           "vkAllocateMemory (f64 SSBO async)", -32);
    YONA_GPU_STUB_VK_ASYNC(vkBindBufferMemory(g_dev, buf, mem, 0),
                           "vkBindBufferMemory (f64 SSBO async)", -33);

    YONA_GPU_STUB_VK_ASYNC(vkMapMemory(g_dev, mem, 0, alloc_sz, 0, &mapped), "vkMapMemory (f64 SSBO async)",
                           -34);
    memcpy(mapped, elements, (size_t)nbytes);
    if ((mem_props & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
        VkMappedMemoryRange rng = {0};
        rng.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        rng.memory = mem;
        rng.offset = 0;
        rng.size = alloc_sz;
        vkFlushMappedMemoryRanges(g_dev, 1, &rng);
    }

    VkDescriptorPoolSize psz = {0};
    psz.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    psz.descriptorCount = 1;
    VkDescriptorPoolCreateInfo dpci = {0};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &psz;
    YONA_GPU_STUB_VK_ASYNC(vkCreateDescriptorPool(g_dev, &dpci, NULL, &dpool),
                           "vkCreateDescriptorPool (f64 async)", -35);

    VkDescriptorSetAllocateInfo dsai = {0};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = dpool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &g_f64_desc_layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    YONA_GPU_STUB_VK_ASYNC(vkAllocateDescriptorSets(g_dev, &dsai, &set),
                           "vkAllocateDescriptorSets (f64 async)", -36);

    VkDescriptorBufferInfo dbi = {0};
    dbi.buffer = buf;
    dbi.offset = 0;
    dbi.range = nbytes;
    VkWriteDescriptorSet wr = {0};
    wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr.dstSet = set;
    wr.dstBinding = 0;
    wr.dstArrayElement = 0;
    wr.descriptorCount = 1;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    wr.pBufferInfo = &dbi;
    vkUpdateDescriptorSets(g_dev, 1, &wr, 0, NULL);

    VkCommandBufferAllocateInfo ai = {0};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = g_cmd_pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    YONA_GPU_STUB_VK_ASYNC(vkAllocateCommandBuffers(g_dev, &ai, &cmd),
                           "vkAllocateCommandBuffers (async f64)", -38);

    VkCommandBufferBeginInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    YONA_GPU_STUB_VK_ASYNC(vkBeginCommandBuffer(cmd, &bi), "vkBeginCommandBuffer (async f64)", -39);

    VkMemoryBarrier pre = {0};
    pre.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    pre.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    pre.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &pre,
        0, NULL, 0, NULL);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_f64_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_f64_pipe_layout, 0, 1, &set, 0, NULL);
    vk_cmd_push_f64_scale(cmd, g_f64_pipe_layout, count, scale);

    uint32_t gx = (count + 63u) / 64u;
    vkCmdDispatch(cmd, gx, 1, 1);

    VkMemoryBarrier post = {0};
    post.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    post.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    post.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &post,
        0, NULL, 0, NULL);

    YONA_GPU_STUB_VK_ASYNC(vkEndCommandBuffer(cmd), "vkEndCommandBuffer (async f64)", -40);

    int use_tl = yona_gpu_stub_async_use_timeline() && g_pfn_vkQueueSubmit2 != NULL &&
                 g_pfn_vkWaitSemaphores != NULL;
    if (!use_tl) {
        VkFenceCreateInfo fi = {0};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        YONA_GPU_STUB_VK_ASYNC(vkCreateFence(g_dev, &fi, NULL, &fence), "vkCreateFence (async f64)", -37);
    }

    if (group != NULL && yona_rt_group_is_cancelled(group)) {
        vkFreeCommandBuffers(g_dev, g_cmd_pool, 1, &cmd);
        if (fence != VK_NULL_HANDLE) vkDestroyFence(g_dev, fence, NULL);
        vkDestroyDescriptorPool(g_dev, dpool, NULL);
        vkUnmapMemory(g_dev, mem);
        vkFreeMemory(g_dev, mem, NULL);
        vkDestroyBuffer(g_dev, buf, NULL);
        gpu_stub_mtx_unlock(&g_mu);
        yona_rt_promise_complete(p, -887, 1, group);
        return p;
    }

    uint64_t tl_val_s = 0;
    if (use_tl) {
        VkSemaphoreTypeCreateInfo sti = {0};
        sti.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        sti.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        VkSemaphoreCreateInfo sci = {0};
        sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        sci.pNext = &sti;
        YONA_GPU_STUB_VK_ASYNC(vkCreateSemaphore(g_dev, &sci, NULL, &timeline_sem),
            "vkCreateSemaphore (async f64 timeline)", -43);
        tl_val_s = ++g_stub_timeline_next;
        if (tl_val_s == 0) tl_val_s = ++g_stub_timeline_next;

        VkCommandBufferSubmitInfo cbs = {0};
        cbs.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cbs.commandBuffer = cmd;

        VkSemaphoreSubmitInfo sig = {0};
        sig.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        sig.semaphore = timeline_sem;
        sig.value = tl_val_s;
        sig.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

        VkTimelineSemaphoreSubmitInfo tlsi = {0};
        tlsi.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        tlsi.signalSemaphoreValueCount = 1;
        tlsi.pSignalSemaphoreValues = &tl_val_s;

        VkSubmitInfo2 si2 = {0};
        si2.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        si2.pNext = &tlsi;
        si2.commandBufferInfoCount = 1;
        si2.pCommandBufferInfos = &cbs;
        si2.signalSemaphoreInfoCount = 1;
        si2.pSignalSemaphoreInfos = &sig;

        YONA_GPU_STUB_VK_ASYNC(g_pfn_vkQueueSubmit2(g_queue, 1, &si2, VK_NULL_HANDLE),
            "vkQueueSubmit2 (async f64)", -44);
    } else {
        VkSubmitInfo si = {0};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        YONA_GPU_STUB_VK_ASYNC(vkQueueSubmit(g_queue, 1, &si, fence), "vkQueueSubmit (async f64)", -41);
    }

    gpu_fence_job_t* job = (gpu_fence_job_t*)calloc(1, sizeof(gpu_fence_job_t));
    if (job == NULL) {
        if (use_tl && timeline_sem != VK_NULL_HANDLE && g_pfn_vkWaitSemaphores != NULL) {
            VkSemaphoreWaitInfo wi = {0};
            wi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
            wi.semaphoreCount = 1;
            wi.pSemaphores = &timeline_sem;
            wi.pValues = &tl_val_s;
            VkResult wr = g_pfn_vkWaitSemaphores(g_dev, &wi, UINT64_MAX);
            char nbuf[256];
            if (wr != VK_SUCCESS)
                snprintf(nbuf, sizeof(nbuf),
                    "float-async: calloc(gpu_fence_job) after submit; drain timeline VkResult %d",
                    (int)wr);
            else
                snprintf(nbuf, sizeof(nbuf),
                    "float-async: calloc(gpu_fence_job) after submit (timeline OK; likely host OOM)");
            yona_gpu_vulkan_device_set_last_note(nbuf);
            vkDestroySemaphore(g_dev, timeline_sem, NULL);
        } else {
            VkResult wr = vkWaitForFences(g_dev, 1, &fence, VK_TRUE, UINT64_MAX);
            char nbuf[256];
            if (wr != VK_SUCCESS)
                snprintf(nbuf, sizeof(nbuf),
                    "float-async: calloc(gpu_fence_job) after submit; drain fence VkResult %d",
                    (int)wr);
            else
                snprintf(nbuf, sizeof(nbuf),
                    "float-async: calloc(gpu_fence_job) after submit (fence OK; likely host OOM)");
            yona_gpu_vulkan_device_set_last_note(nbuf);
        }
        err = -50;
        goto async_fail;
    }
    job->dev = g_dev;
    job->async_timeline = use_tl ? 1 : 0;
    if (use_tl) {
        job->fence = VK_NULL_HANDLE;
        job->timeline_sem = timeline_sem;
        job->timeline_value = tl_val_s;
        timeline_sem = VK_NULL_HANDLE;
    } else {
        job->fence = fence;
        job->timeline_sem = VK_NULL_HANDLE;
        job->timeline_value = 0;
        fence = VK_NULL_HANDLE;
    }
    job->promise = p;
    job->group = group;
    job->cmd = cmd;
    job->cmd_pool = g_cmd_pool;
    job->dpool = dpool;
    job->host_elements = elements;
    job->count = count;
    job->mapped = mapped;
    job->mem = mem;
    job->buf = buf;
    job->alloc_sz = alloc_sz;
    job->mem_props = mem_props;

    if (group != NULL) {
        yona_rt_group_register(group, p);
    }

    cmd = VK_NULL_HANDLE;
    dpool = VK_NULL_HANDLE;
    mapped = NULL;
    mem = VK_NULL_HANDLE;
    buf = VK_NULL_HANDLE;

    gpu_stub_mtx_unlock(&g_mu);
    yona_gpu_fence_job_enqueue(job);
    return p;

async_fail:
    if (cmd != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(g_dev, g_cmd_pool, 1, &cmd);
    }
    if (fence != VK_NULL_HANDLE) {
        vkDestroyFence(g_dev, fence, NULL);
    }
    if (timeline_sem != VK_NULL_HANDLE) {
        vkDestroySemaphore(g_dev, timeline_sem, NULL);
    }
    if (dpool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(g_dev, dpool, NULL);
    }
    if (mapped != NULL) {
        vkUnmapMemory(g_dev, mem);
    }
    if (mem != VK_NULL_HANDLE) {
        vkFreeMemory(g_dev, mem, NULL);
    }
    if (buf != VK_NULL_HANDLE) {
        vkDestroyBuffer(g_dev, buf, NULL);
    }
    gpu_stub_mtx_unlock(&g_mu);
    yona_rt_promise_complete(p, err, 1, NULL);
    return p;
}

yona_promise_t* yona_gpu_vulkan_float64_buffer_mul2_async(double* elements, uint32_t count,
                                                          yona_task_group_t* group) {
    return yona_gpu_vulkan_float64_buffer_scale_async(elements, count, 2.0, group);
}

int yona_gpu_vulkan_dispatch_nop_once(void) {
    gpu_stub_mtx_lock(&g_mu);
    if (g_dev == VK_NULL_HANDLE || g_queue == VK_NULL_HANDLE || g_cmd_pool == VK_NULL_HANDLE ||
        g_compute_pipe == VK_NULL_HANDLE) {
        gpu_stub_mtx_unlock(&g_mu);
        return -9;
    }

    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fi = {0};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    {
        VkResult vr = vkCreateFence(g_dev, &fi, NULL, &fence);
        if (vr != VK_SUCCESS) {
            yona_gpu_stub_note_vk("vkCreateFence (dispatch nop)", vr);
            gpu_stub_mtx_unlock(&g_mu);
            return -10;
        }
    }

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo ai = {0};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = g_cmd_pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    {
        VkResult vr = vkAllocateCommandBuffers(g_dev, &ai, &cmd);
        if (vr != VK_SUCCESS) {
            yona_gpu_stub_note_vk("vkAllocateCommandBuffers (dispatch nop)", vr);
            vkDestroyFence(g_dev, fence, NULL);
            gpu_stub_mtx_unlock(&g_mu);
            return -11;
        }
    }

    VkCommandBufferBeginInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    {
        VkResult vr = vkBeginCommandBuffer(cmd, &bi);
        if (vr != VK_SUCCESS) {
            yona_gpu_stub_note_vk("vkBeginCommandBuffer (dispatch nop)", vr);
            vkFreeCommandBuffers(g_dev, g_cmd_pool, 1, &cmd);
            vkDestroyFence(g_dev, fence, NULL);
            gpu_stub_mtx_unlock(&g_mu);
            return -12;
        }
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_compute_pipe);
    vkCmdDispatch(cmd, 1, 1, 1);
    {
        const char* dde = getenv("YONA_GPU_TEST_DUAL_DISPATCH");
        if (dde && dde[0] == '1' && dde[1] == 0) {
            vkCmdDispatch(cmd, 1, 1, 1);
        }
    }

    {
        VkResult vr = vkEndCommandBuffer(cmd);
        if (vr != VK_SUCCESS) {
            yona_gpu_stub_note_vk("vkEndCommandBuffer (dispatch nop)", vr);
            vkFreeCommandBuffers(g_dev, g_cmd_pool, 1, &cmd);
            vkDestroyFence(g_dev, fence, NULL);
            gpu_stub_mtx_unlock(&g_mu);
            return -13;
        }
    }

    VkSubmitInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;

    {
        VkResult vr = vkQueueSubmit(g_queue, 1, &si, fence);
        if (vr != VK_SUCCESS) {
            yona_gpu_stub_note_vk("vkQueueSubmit (dispatch nop)", vr);
            vkFreeCommandBuffers(g_dev, g_cmd_pool, 1, &cmd);
            vkDestroyFence(g_dev, fence, NULL);
            gpu_stub_mtx_unlock(&g_mu);
            return -14;
        }
    }

    {
        VkResult vr = vkWaitForFences(g_dev, 1, &fence, VK_TRUE, UINT64_MAX);
        if (vr != VK_SUCCESS) {
            yona_gpu_stub_note_vk("vkWaitForFences (dispatch nop)", vr);
            vkFreeCommandBuffers(g_dev, g_cmd_pool, 1, &cmd);
            vkDestroyFence(g_dev, fence, NULL);
            gpu_stub_mtx_unlock(&g_mu);
            return -15;
        }
    }

    vkFreeCommandBuffers(g_dev, g_cmd_pool, 1, &cmd);
    vkDestroyFence(g_dev, fence, NULL);
    gpu_stub_mtx_unlock(&g_mu);
    return 0;
}

typedef struct yona_gpu_vk_pinned {
    VkBuffer buf;
    VkDeviceMemory mem;
    VkDeviceSize alloc_sz;
    double* mapped;
    int64_t count;
} yona_gpu_vk_pinned_t;

int yona_gpu_vulkan_alloc_pinned_floats(int64_t count, double** out_host, void** out_opaque) {
    if (!out_host || !out_opaque)
        return -1;
    *out_host = NULL;
    *out_opaque = NULL;
    if (count < 0)
        count = 0;
    if (yona_gpu_vulkan_ctx_init() != 0)
        return -1;
    gpu_stub_mtx_lock(&g_mu);
    if (g_dev == VK_NULL_HANDLE || g_phys == VK_NULL_HANDLE) {
        gpu_stub_mtx_unlock(&g_mu);
        return -1;
    }

    yona_gpu_vk_pinned_t* p = (yona_gpu_vk_pinned_t*)calloc(1, sizeof(*p));
    if (!p) {
        gpu_stub_mtx_unlock(&g_mu);
        return -1;
    }
    p->count = count;
    VkDeviceSize nbytes = (VkDeviceSize)count * (VkDeviceSize)sizeof(double);
    if (nbytes == 0)
        nbytes = sizeof(double); /* Vulkan forbids zero-sized buffers */

    VkBufferCreateInfo bci = {0};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = nbytes;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult vr = vkCreateBuffer(g_dev, &bci, NULL, &p->buf);
    if (vr != VK_SUCCESS) {
        yona_gpu_stub_note_vk("pinned: vkCreateBuffer", vr);
        free(p);
        gpu_stub_mtx_unlock(&g_mu);
        return -1;
    }

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g_dev, p->buf, &req);
    p->alloc_sz = req.size;
    uint32_t mt = yona_vk_find_memory_type(req.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == UINT32_MAX) {
        mt = yona_vk_find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        if (mt == UINT32_MAX) {
            yona_gpu_vulkan_device_set_last_note("pinned: no HOST_VISIBLE memory type");
            vkDestroyBuffer(g_dev, p->buf, NULL);
            free(p);
            gpu_stub_mtx_unlock(&g_mu);
            return -1;
        }
    }

    VkMemoryAllocateInfo mai = {0};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = mt;
    vr = vkAllocateMemory(g_dev, &mai, NULL, &p->mem);
    if (vr != VK_SUCCESS) {
        yona_gpu_stub_note_vk("pinned: vkAllocateMemory", vr);
        vkDestroyBuffer(g_dev, p->buf, NULL);
        free(p);
        gpu_stub_mtx_unlock(&g_mu);
        return -1;
    }
    vr = vkBindBufferMemory(g_dev, p->buf, p->mem, 0);
    if (vr != VK_SUCCESS) {
        yona_gpu_stub_note_vk("pinned: vkBindBufferMemory", vr);
        vkFreeMemory(g_dev, p->mem, NULL);
        vkDestroyBuffer(g_dev, p->buf, NULL);
        free(p);
        gpu_stub_mtx_unlock(&g_mu);
        return -1;
    }
    void* mapped = NULL;
    vr = vkMapMemory(g_dev, p->mem, 0, p->alloc_sz, 0, &mapped);
    if (vr != VK_SUCCESS) {
        yona_gpu_stub_note_vk("pinned: vkMapMemory", vr);
        vkFreeMemory(g_dev, p->mem, NULL);
        vkDestroyBuffer(g_dev, p->buf, NULL);
        free(p);
        gpu_stub_mtx_unlock(&g_mu);
        return -1;
    }
    p->mapped = (double*)mapped;
    if (count > 0)
        memset(p->mapped, 0, (size_t)count * sizeof(double));
    else
        memset(p->mapped, 0, sizeof(double));

    *out_host = p->mapped;
    *out_opaque = p;
    gpu_stub_mtx_unlock(&g_mu);
    return 0;
}

void yona_gpu_vulkan_free_pinned_floats(void* opaque) {
    if (!opaque)
        return;
    yona_gpu_vk_pinned_t* p = (yona_gpu_vk_pinned_t*)opaque;
    gpu_stub_mtx_lock(&g_mu);
    if (g_dev != VK_NULL_HANDLE) {
        if (p->mapped && p->mem != VK_NULL_HANDLE)
            vkUnmapMemory(g_dev, p->mem);
        if (p->buf != VK_NULL_HANDLE)
            vkDestroyBuffer(g_dev, p->buf, NULL);
        if (p->mem != VK_NULL_HANDLE)
            vkFreeMemory(g_dev, p->mem, NULL);
    }
    gpu_stub_mtx_unlock(&g_mu);
    free(p);
}

#else /* !YONA_HAS_VULKAN */

void yona_gpu_vulkan_ctx_shutdown(void) {}

int yona_gpu_vulkan_ctx_init(void) {
    return -1;
}

int yona_gpu_vulkan_dispatch_nop_once(void) {
    return -1;
}

int yona_gpu_vulkan_float64_buffer_mul2_inplace(double* elements, uint32_t count) {
    (void)elements;
    (void)count;
    return -1;
}

int yona_gpu_vulkan_float64_buffer_scale_inplace(double* elements, uint32_t count, double scale) {
    (void)elements;
    (void)count;
    (void)scale;
    return -1;
}

int yona_gpu_vulkan_float64_buffer_reduce_sum(const double* elements, uint32_t count, double* out_sum) {
    (void)elements;
    (void)count;
    if (out_sum)
        *out_sum = 0.0;
    return -1;
}

yona_promise_t* yona_gpu_vulkan_float64_buffer_scale_async(double* elements, uint32_t count, double scale,
                                                            yona_task_group_t* group) {
    (void)elements;
    (void)count;
    (void)scale;
    (void)group;
    yona_promise_t* p = yona_rt_promise_new();
    if (p != NULL)
        yona_rt_promise_complete(p, -1, 1, group);
    return p;
}

yona_promise_t* yona_gpu_vulkan_float64_buffer_mul2_async(double* elements, uint32_t count,
                                                          yona_task_group_t* group) {
    return yona_gpu_vulkan_float64_buffer_scale_async(elements, count, 2.0, group);
}

int64_t yona_Std_GPU__available(int64_t unit) {
    (void)unit;
    return 0;
}

int64_t yona_Std_GPU__apiVersion(int64_t unit) {
    (void)unit;
    return 1;
}

int64_t yona_Std_GPU__physicalDeviceCount(int64_t unit) {
    (void)unit;
    return 0;
}

yona_promise_t* yona_Std_GPU__floatArrayMul2Async(double* arr) {
    if (arr == NULL) {
        yona_promise_t* p = yona_rt_promise_new();
        if (p) yona_rt_promise_complete(p, -16, 1, NULL);
        return p;
    }
    int64_t len64 = yona_rt_float_array_length(arr);
    if (len64 < 0) len64 = 0;
    if (len64 > (int64_t)UINT32_MAX) len64 = (int64_t)UINT32_MAX;
    uint32_t count = (uint32_t)len64;
    return yona_gpu_vulkan_float64_buffer_mul2_async(arr, count, NULL);
}

yona_promise_t* yona_Std_GPU__floatArrayScaleAsync(double scale, double* arr) {
    if (arr == NULL) {
        yona_promise_t* p = yona_rt_promise_new();
        if (p) yona_rt_promise_complete(p, -16, 1, NULL);
        return p;
    }
    int64_t len64 = yona_rt_float_array_length(arr);
    if (len64 < 0) len64 = 0;
    if (len64 > (int64_t)UINT32_MAX) len64 = (int64_t)UINT32_MAX;
    uint32_t count = (uint32_t)len64;
    return yona_gpu_vulkan_float64_buffer_scale_async(arr, count, scale, NULL);
}

int yona_gpu_vulkan_alloc_pinned_floats(int64_t count, double** out_host, void** out_opaque) {
    (void)count;
    if (out_host)
        *out_host = NULL;
    if (out_opaque)
        *out_opaque = NULL;
    return -1;
}

void yona_gpu_vulkan_free_pinned_floats(void* opaque) {
    (void)opaque;
}

#endif
