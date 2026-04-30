/* Vulkan compute: mapAdd, mapMul, reduceSum, filter — included from gpu_vulkan_device.c */

#include "yona/runtime/gpu_build_config.h"

#if !YONA_GPU_VULKAN_ENABLED

#include <stdint.h>

int yona_gpu_vulkan_try_map_add_int64(int64_t delta, int64_t* arr, int64_t** out) {
    (void)delta;
    (void)arr;
    (void)out;
    return 0;
}

int yona_gpu_vulkan_try_map_mul_int64(int64_t factor, int64_t* arr, int64_t** out) {
    (void)factor;
    (void)arr;
    (void)out;
    return 0;
}

int yona_gpu_vulkan_try_reduce_sum_int64(int64_t* arr, int64_t* out_sum) {
    (void)arr;
    (void)out_sum;
    return 0;
}

int yona_gpu_vulkan_try_filter_greater_than_int64(int64_t threshold, int64_t* arr, int64_t** out) {
    (void)threshold;
    (void)arr;
    (void)out;
    return 0;
}

#else /* YONA_GPU_VULKAN_ENABLED */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

typedef struct YonaVkSimplePipe YonaVkSimplePipe;
typedef struct YonaVkReducePipe YonaVkReducePipe;
typedef struct YonaVkScatterPipe YonaVkScatterPipe;

extern int64_t* yona_rt_int_array_alloc(int64_t count);

#define YONA_VK_DPA(name) ((PFN_##name)(void*)yona_pfn_vkGetDeviceProcAddr(yona_vk_dev, #name))

VkResult yona_vk_compute_ensure_mapadd_pipe(void);
VkResult yona_vk_compute_ensure_mapmul_pipe(void);
VkResult yona_vk_compute_ensure_reduce_pipe(void);
VkResult yona_vk_compute_ensure_filter_mark_pipe(void);
VkResult yona_vk_compute_ensure_filter_scatter_pipe(void);
VkResult yona_vk_compute_ensure_filter_flags_to_i64_pipe(void);
VkResult yona_vk_compute_ensure_filter_prefix_pipe(void);
VkResult yona_vk_compute_ensure_filter_inc_to_exc_pipe(void);
YonaVkSimplePipe* yona_vk_compute_mapadd_pipe(void);
YonaVkSimplePipe* yona_vk_compute_mapmul_pipe(void);
YonaVkReducePipe* yona_vk_compute_reduce_pipe(void);
YonaVkReducePipe* yona_vk_compute_filter_mark_pipe(void);
YonaVkScatterPipe* yona_vk_compute_filter_scatter_pipe(void);
YonaVkReducePipe* yona_vk_compute_filter_flags_to_i64_pipe(void);
YonaVkReducePipe* yona_vk_compute_filter_prefix_pipe(void);
YonaVkScatterPipe* yona_vk_compute_filter_inc_to_exc_pipe(void);
void yona_vk_compute_submit_lock(void);
void yona_vk_compute_submit_unlock(void);

static int yona_vk_env_compute(void) {
    const char* c = getenv("YONA_GPU_VULKAN_COMPUTE");
    return c && strcmp(c, "1") == 0;
}

static int yona_vk_env_mapadd(void) {
    if (yona_vk_env_compute()) return 1;
    const char* m = getenv("YONA_GPU_VULKAN_MAPADD");
    return m && strcmp(m, "1") == 0;
}

static int yona_vk_env_mapmul(void) {
    if (yona_vk_env_compute()) return 1;
    const char* m = getenv("YONA_GPU_VULKAN_MAPMUL");
    return m && strcmp(m, "1") == 0;
}

static int yona_vk_env_reduce(void) {
    if (yona_vk_env_compute()) return 1;
    const char* m = getenv("YONA_GPU_VULKAN_REDUCE");
    return m && strcmp(m, "1") == 0;
}

static int yona_vk_env_filter(void) {
    if (yona_vk_env_compute()) return 1;
    const char* m = getenv("YONA_GPU_VULKAN_FILTER");
    return m && strcmp(m, "1") == 0;
}

/** Set to `1` to use the legacy host-side exclusive-prefix loop (debug / regression). */
static int yona_vk_filter_use_cpu_prefix(void) {
    const char* s = getenv("YONA_GPU_VULKAN_FILTER_CPU_PREFIX");
    return s && strcmp(s, "1") == 0;
}

static int64_t yona_vk_min_len_two(const char* global_key, const char* legacy_key) {
    const char* s = getenv(global_key);
    if (s && s[0]) {
        int64_t v = (int64_t)strtoll(s, NULL, 10);
        return v < 1 ? 1 : v;
    }
    s = getenv(legacy_key);
    if (s && s[0]) {
        int64_t v = (int64_t)strtoll(s, NULL, 10);
        return v < 1 ? 1 : v;
    }
    return 4096;
}

static int yona_vk_common_precheck(int64_t* arr, const char* op_tag) {
    if (!arr) return 0;
    yona_vk_note_clear();

    {
        int ti = yona_gpu_vulkan_device_try_init();
        if (ti != 0) {
            char b[280];
            const char* devn = yona_gpu_vulkan_device_last_note();
            if (devn && devn[0])
                snprintf(b, sizeof b, "%s: try_init returned %d; %s", op_tag, ti, devn);
            else
                snprintf(b, sizeof b, "%s: try_init returned %d", op_tag, ti);
            yona_vk_note_cpy(b);
            return 0;
        }
    }
    if (yona_vk_dev == VK_NULL_HANDLE || yona_vk_queue == VK_NULL_HANDLE) {
        char b[80];
        snprintf(b, sizeof b, "%s: no VkDevice/VkQueue after try_init", op_tag);
        yona_vk_note_cpy(b);
        return 0;
    }
    if (!yona_gpu_vulkan_device_shader_int64()) {
        yona_vk_note_cpy(
            "gpu: logical device has no shaderInt64 (physical device may lack it or "
            "vkCreateDevice retried without it)");
        return 0;
    }
    return 1;
}

static int yona_vk_load_dispatch_pfns(
    PFN_vkCreateDescriptorPool* o_dpool, PFN_vkDestroyDescriptorPool* o_dpoold,
    PFN_vkAllocateDescriptorSets* o_allocds, PFN_vkUpdateDescriptorSets* o_upds,
    PFN_vkCreateBuffer* o_buf, PFN_vkDestroyBuffer* o_bufd,
    PFN_vkGetBufferMemoryRequirements* o_gbmr, PFN_vkAllocateMemory* o_allocm,
    PFN_vkFreeMemory* o_freem, PFN_vkBindBufferMemory* o_bind,
    PFN_vkMapMemory* o_map, PFN_vkUnmapMemory* o_unmap,
    PFN_vkInvalidateMappedMemoryRanges* o_inv, PFN_vkCreateCommandPool* o_ccp,
    PFN_vkDestroyCommandPool* o_dcp, PFN_vkAllocateCommandBuffers* o_acb,
    PFN_vkFreeCommandBuffers* o_fcb, PFN_vkBeginCommandBuffer* o_bcb,
    PFN_vkEndCommandBuffer* o_ecb, PFN_vkCmdBindPipeline* o_cbp,
    PFN_vkCmdBindDescriptorSets* o_cbds, PFN_vkCmdPushConstants* o_cpc,
    PFN_vkCmdDispatch* o_cd, PFN_vkCmdPipelineBarrier* o_cpb,
    PFN_vkCreateFence* o_cf, PFN_vkDestroyFence* o_df, PFN_vkQueueSubmit* o_qs,
    PFN_vkWaitForFences* o_wff, PFN_vkResetFences* o_rf) {
    *o_dpool = YONA_VK_DPA(vkCreateDescriptorPool);
    *o_dpoold = YONA_VK_DPA(vkDestroyDescriptorPool);
    *o_allocds = YONA_VK_DPA(vkAllocateDescriptorSets);
    *o_upds = YONA_VK_DPA(vkUpdateDescriptorSets);
    *o_buf = YONA_VK_DPA(vkCreateBuffer);
    *o_bufd = YONA_VK_DPA(vkDestroyBuffer);
    *o_gbmr = YONA_VK_DPA(vkGetBufferMemoryRequirements);
    *o_allocm = YONA_VK_DPA(vkAllocateMemory);
    *o_freem = YONA_VK_DPA(vkFreeMemory);
    *o_bind = YONA_VK_DPA(vkBindBufferMemory);
    *o_map = YONA_VK_DPA(vkMapMemory);
    *o_unmap = YONA_VK_DPA(vkUnmapMemory);
    *o_inv = YONA_VK_DPA(vkInvalidateMappedMemoryRanges);
    *o_ccp = YONA_VK_DPA(vkCreateCommandPool);
    *o_dcp = YONA_VK_DPA(vkDestroyCommandPool);
    *o_acb = YONA_VK_DPA(vkAllocateCommandBuffers);
    *o_fcb = YONA_VK_DPA(vkFreeCommandBuffers);
    *o_bcb = YONA_VK_DPA(vkBeginCommandBuffer);
    *o_ecb = YONA_VK_DPA(vkEndCommandBuffer);
    *o_cbp = YONA_VK_DPA(vkCmdBindPipeline);
    *o_cbds = YONA_VK_DPA(vkCmdBindDescriptorSets);
    *o_cpc = YONA_VK_DPA(vkCmdPushConstants);
    *o_cd = YONA_VK_DPA(vkCmdDispatch);
    *o_cpb = YONA_VK_DPA(vkCmdPipelineBarrier);
    *o_cf = YONA_VK_DPA(vkCreateFence);
    *o_df = YONA_VK_DPA(vkDestroyFence);
    *o_qs = YONA_VK_DPA(vkQueueSubmit);
    *o_wff = YONA_VK_DPA(vkWaitForFences);
    *o_rf = YONA_VK_DPA(vkResetFences);
    return *o_dpool && *o_dpoold && *o_allocds && *o_upds && *o_buf && *o_bufd && *o_gbmr &&
           *o_allocm && *o_freem && *o_bind && *o_map && *o_unmap && *o_ccp && *o_dcp &&
           *o_acb && *o_fcb && *o_bcb && *o_ecb && *o_cbp && *o_cbds && *o_cpc && *o_cd &&
           *o_cpb && *o_cf && *o_df && *o_qs && *o_wff && *o_rf;
}

/** Prefer a memory type that is device-local and not host-visible (discrete VRAM). */
static int yona_vk_memory_type_device_local_only(uint32_t type_bits, uint32_t* out_index) {
    VkPhysicalDeviceMemoryProperties mp;
    yona_pfn_vkGetPhysicalDeviceMemoryProperties(yona_vk_phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if (!(type_bits & (1u << i))) continue;
        VkMemoryPropertyFlags f = mp.memoryTypes[i].propertyFlags;
        if ((f & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
            !(f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            *out_index = i;
            return 1;
        }
    }
    return 0;
}

static int yona_vk_force_host_ssbo(void) {
    const char* s = getenv("YONA_GPU_VULKAN_HOST_SSBO");
    return s && strcmp(s, "1") == 0;
}

/** Returns 1 and fills device and staging buffers when a discrete device-local heap exists. */
static int yona_vk_try_dev_stg_pair(
    PFN_vkCreateBuffer vkCreateBuffer, PFN_vkDestroyBuffer vkDestroyBuffer,
    PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements,
    PFN_vkAllocateMemory vkAllocateMemory, PFN_vkFreeMemory vkFreeMemory,
    PFN_vkBindBufferMemory vkBindBufferMemory, VkDeviceSize nbytes, VkBuffer* dev_buf,
    VkDeviceMemory* dev_mem, VkBuffer* stg_buf, VkDeviceMemory* stg_mem) {
    *dev_buf = *stg_buf = VK_NULL_HANDLE;
    *dev_mem = *stg_mem = VK_NULL_HANDLE;
    VkBufferCreateInfo bd = {0};
    bd.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bd.size = nbytes;
    bd.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bd.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult r = vkCreateBuffer(yona_vk_dev, &bd, NULL, dev_buf);
    if (r != VK_SUCCESS) return 0;
    VkMemoryRequirements rq_dev;
    vkGetBufferMemoryRequirements(yona_vk_dev, *dev_buf, &rq_dev);
    uint32_t mt_dev = 0;
    if (!yona_vk_memory_type_device_local_only(rq_dev.memoryTypeBits, &mt_dev)) goto fail_dev;
    VkMemoryAllocateInfo mai_d = {0};
    mai_d.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai_d.allocationSize = rq_dev.size;
    mai_d.memoryTypeIndex = mt_dev;
    r = vkAllocateMemory(yona_vk_dev, &mai_d, NULL, dev_mem);
    if (r != VK_SUCCESS) goto fail_dev;
    r = vkBindBufferMemory(yona_vk_dev, *dev_buf, *dev_mem, 0);
    if (r != VK_SUCCESS) goto fail_dev_mem;

    VkBufferCreateInfo bs = {0};
    bs.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bs.size = nbytes;
    bs.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bs.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    r = vkCreateBuffer(yona_vk_dev, &bs, NULL, stg_buf);
    if (r != VK_SUCCESS) goto fail_dev_mem;
    VkMemoryRequirements rq_st;
    vkGetBufferMemoryRequirements(yona_vk_dev, *stg_buf, &rq_st);
    uint32_t mt_st = 0;
    if (yona_vk_pick_memory_type(rq_st.memoryTypeBits,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 &mt_st) != 0)
        goto fail_stg_buf;
    VkMemoryAllocateInfo mai_s = {0};
    mai_s.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai_s.allocationSize = rq_st.size;
    mai_s.memoryTypeIndex = mt_st;
    r = vkAllocateMemory(yona_vk_dev, &mai_s, NULL, stg_mem);
    if (r != VK_SUCCESS) goto fail_stg_buf;
    r = vkBindBufferMemory(yona_vk_dev, *stg_buf, *stg_mem, 0);
    if (r != VK_SUCCESS) goto fail_stg_mem;
    return 1;

fail_stg_mem:
    vkFreeMemory(yona_vk_dev, *stg_mem, NULL);
    *stg_mem = VK_NULL_HANDLE;
fail_stg_buf:
    vkDestroyBuffer(yona_vk_dev, *stg_buf, NULL);
    *stg_buf = VK_NULL_HANDLE;
fail_dev_mem:
    vkFreeMemory(yona_vk_dev, *dev_mem, NULL);
    *dev_mem = VK_NULL_HANDLE;
fail_dev:
    vkDestroyBuffer(yona_vk_dev, *dev_buf, NULL);
    *dev_buf = VK_NULL_HANDLE;
    return 0;
}

static VkResult yona_vk_create_device_local_ssbo(
    PFN_vkCreateBuffer vkCreateBuffer, PFN_vkDestroyBuffer vkDestroyBuffer,
    PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements,
    PFN_vkAllocateMemory vkAllocateMemory, PFN_vkFreeMemory vkFreeMemory,
    PFN_vkBindBufferMemory vkBindBufferMemory, VkDeviceSize nbytes, VkBuffer* out_buf,
    VkDeviceMemory* out_mem) {
    *out_buf = VK_NULL_HANDLE;
    *out_mem = VK_NULL_HANDLE;
    VkBufferCreateInfo bd = {0};
    bd.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bd.size = nbytes;
    bd.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
               VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bd.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult r = vkCreateBuffer(yona_vk_dev, &bd, NULL, out_buf);
    if (r != VK_SUCCESS) return r;
    VkMemoryRequirements rq;
    vkGetBufferMemoryRequirements(yona_vk_dev, *out_buf, &rq);
    uint32_t mt = 0;
    if (!yona_vk_memory_type_device_local_only(rq.memoryTypeBits, &mt)) {
        vkDestroyBuffer(yona_vk_dev, *out_buf, NULL);
        *out_buf = VK_NULL_HANDLE;
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    VkMemoryAllocateInfo mai = {0};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = rq.size;
    mai.memoryTypeIndex = mt;
    r = vkAllocateMemory(yona_vk_dev, &mai, NULL, out_mem);
    if (r != VK_SUCCESS) goto bad_buf;
    r = vkBindBufferMemory(yona_vk_dev, *out_buf, *out_mem, 0);
    if (r != VK_SUCCESS) goto bad_mem;
    return VK_SUCCESS;
bad_mem:
    vkFreeMemory(yona_vk_dev, *out_mem, NULL);
    *out_mem = VK_NULL_HANDLE;
bad_buf:
    vkDestroyBuffer(yona_vk_dev, *out_buf, NULL);
    *out_buf = VK_NULL_HANDLE;
    return r;
}

static void yona_vk_barrier_buffer(PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier,
                                   VkCommandBuffer cmd, VkBuffer buf, VkAccessFlags src_acc,
                                   VkAccessFlags dst_acc, VkPipelineStageFlags src_st,
                                   VkPipelineStageFlags dst_st) {
    VkBufferMemoryBarrier b = {0};
    b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    b.srcAccessMask = src_acc;
    b.dstAccessMask = dst_acc;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.buffer = buf;
    b.offset = 0;
    b.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, src_st, dst_st, 0, 0, NULL, 1, &b, 0, NULL);
}

static int yona_vk_run_simple_map(const char* fail_tag, VkResult (*ensure)(void),
                                  YonaVkSimplePipe* pipe, int64_t scalar, int64_t* arr,
                                  int64_t** out) {
    *out = NULL;
    VkResult r = VK_SUCCESS;
    int use_staging = 0;

    PFN_vkCreateDescriptorPool vkCreateDescriptorPool;
    PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool;
    PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets;
    PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets;
    PFN_vkCreateBuffer vkCreateBuffer;
    PFN_vkDestroyBuffer vkDestroyBuffer;
    PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements;
    PFN_vkAllocateMemory vkAllocateMemory;
    PFN_vkFreeMemory vkFreeMemory;
    PFN_vkBindBufferMemory vkBindBufferMemory;
    PFN_vkMapMemory vkMapMemory;
    PFN_vkUnmapMemory vkUnmapMemory;
    PFN_vkInvalidateMappedMemoryRanges vkInvalidateMappedMemoryRanges;
    PFN_vkCreateCommandPool vkCreateCommandPool;
    PFN_vkDestroyCommandPool vkDestroyCommandPool;
    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
    PFN_vkFreeCommandBuffers vkFreeCommandBuffers;
    PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
    PFN_vkEndCommandBuffer vkEndCommandBuffer;
    PFN_vkCmdBindPipeline vkCmdBindPipeline;
    PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets;
    PFN_vkCmdPushConstants vkCmdPushConstants;
    PFN_vkCmdDispatch vkCmdDispatch;
    PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;
    PFN_vkCreateFence vkCreateFence;
    PFN_vkDestroyFence vkDestroyFence;
    PFN_vkQueueSubmit vkQueueSubmit;
    PFN_vkWaitForFences vkWaitForFences;
    PFN_vkResetFences vkResetFences;
    PFN_vkCmdCopyBuffer vkCmdCopyBuffer = YONA_VK_DPA(vkCmdCopyBuffer);

    if (!yona_vk_load_dispatch_pfns(
            &vkCreateDescriptorPool, &vkDestroyDescriptorPool, &vkAllocateDescriptorSets,
            &vkUpdateDescriptorSets, &vkCreateBuffer, &vkDestroyBuffer,
            &vkGetBufferMemoryRequirements, &vkAllocateMemory, &vkFreeMemory, &vkBindBufferMemory,
            &vkMapMemory, &vkUnmapMemory, &vkInvalidateMappedMemoryRanges, &vkCreateCommandPool,
            &vkDestroyCommandPool, &vkAllocateCommandBuffers, &vkFreeCommandBuffers,
            &vkBeginCommandBuffer, &vkEndCommandBuffer, &vkCmdBindPipeline, &vkCmdBindDescriptorSets,
            &vkCmdPushConstants, &vkCmdDispatch, &vkCmdPipelineBarrier, &vkCreateFence,
            &vkDestroyFence, &vkQueueSubmit, &vkWaitForFences, &vkResetFences)) {
        yona_vk_note_cpy("gpu: vkGetDeviceProcAddr returned null for a required entry point");
        return 0;
    }

    r = ensure();
    if (r != VK_SUCCESS) {
        char b[120];
        snprintf(b, sizeof b, "%s: pipeline ensure VkResult=%d", fail_tag, (int)r);
        yona_vk_note_cpy(b);
        return 0;
    }

    int64_t len = arr[0];
    VkDeviceSize nbytes = (VkDeviceSize)((size_t)len * sizeof(int64_t));

    VkDescriptorPool dpool = VK_NULL_HANDLE;
    VkDescriptorSet dset = VK_NULL_HANDLE;
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkBuffer buf_dev = VK_NULL_HANDLE;
    VkBuffer buf_stg = VK_NULL_HANDLE;
    VkDeviceMemory mem_dev = VK_NULL_HANDLE;
    VkDeviceMemory mem_stg = VK_NULL_HANDLE;
    VkCommandPool cpool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    void* mapped = NULL;

    VkDescriptorPoolSize dps = {0};
    dps.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    dps.descriptorCount = 1;

    VkDescriptorPoolCreateInfo dpci = {0};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &dps;
    r = vkCreateDescriptorPool(yona_vk_dev, &dpci, NULL, &dpool);
    if (r != VK_SUCCESS) goto fail;

    VkDescriptorSetAllocateInfo dsai = {0};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = dpool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &pipe->dsl;
    r = vkAllocateDescriptorSets(yona_vk_dev, &dsai, &dset);
    if (r != VK_SUCCESS) goto fail;

    if (!yona_vk_force_host_ssbo() && vkCmdCopyBuffer) {
        VkBufferCreateInfo bd = {0};
        bd.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bd.size = nbytes;
        bd.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bd.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        r = vkCreateBuffer(yona_vk_dev, &bd, NULL, &buf_dev);
        if (r == VK_SUCCESS) {
            VkMemoryRequirements rq_dev;
            vkGetBufferMemoryRequirements(yona_vk_dev, buf_dev, &rq_dev);
            uint32_t mt_dev = 0;
            if (yona_vk_memory_type_device_local_only(rq_dev.memoryTypeBits, &mt_dev)) {
                VkMemoryAllocateInfo mai_d = {0};
                mai_d.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                mai_d.allocationSize = rq_dev.size;
                mai_d.memoryTypeIndex = mt_dev;
                r = vkAllocateMemory(yona_vk_dev, &mai_d, NULL, &mem_dev);
                if (r == VK_SUCCESS) {
                    r = vkBindBufferMemory(yona_vk_dev, buf_dev, mem_dev, 0);
                    if (r != VK_SUCCESS) {
                        vkFreeMemory(yona_vk_dev, mem_dev, NULL);
                        mem_dev = VK_NULL_HANDLE;
                    }
                }
                if (r == VK_SUCCESS) {
                    VkBufferCreateInfo bs = {0};
                    bs.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                    bs.size = nbytes;
                    bs.usage =
                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                    bs.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                    r = vkCreateBuffer(yona_vk_dev, &bs, NULL, &buf_stg);
                    if (r == VK_SUCCESS) {
                        VkMemoryRequirements rq_st;
                        vkGetBufferMemoryRequirements(yona_vk_dev, buf_stg, &rq_st);
                        uint32_t mt_st = 0;
                        if (yona_vk_pick_memory_type(rq_st.memoryTypeBits,
                                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                                     &mt_st) == 0) {
                            VkMemoryAllocateInfo mai_s = {0};
                            mai_s.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                            mai_s.allocationSize = rq_st.size;
                            mai_s.memoryTypeIndex = mt_st;
                            r = vkAllocateMemory(yona_vk_dev, &mai_s, NULL, &mem_stg);
                            if (r == VK_SUCCESS) {
                                r = vkBindBufferMemory(yona_vk_dev, buf_stg, mem_stg, 0);
                                if (r != VK_SUCCESS) {
                                    vkFreeMemory(yona_vk_dev, mem_stg, NULL);
                                    mem_stg = VK_NULL_HANDLE;
                                }
                            }
                            if (r == VK_SUCCESS) use_staging = 1;
                        }
                    }
                }
            }
        }
        if (!use_staging) {
            if (buf_stg != VK_NULL_HANDLE) {
                vkDestroyBuffer(yona_vk_dev, buf_stg, NULL);
                buf_stg = VK_NULL_HANDLE;
            }
            if (mem_stg != VK_NULL_HANDLE) {
                vkFreeMemory(yona_vk_dev, mem_stg, NULL);
                mem_stg = VK_NULL_HANDLE;
            }
            if (mem_dev != VK_NULL_HANDLE) {
                vkFreeMemory(yona_vk_dev, mem_dev, NULL);
                mem_dev = VK_NULL_HANDLE;
            }
            if (buf_dev != VK_NULL_HANDLE) {
                vkDestroyBuffer(yona_vk_dev, buf_dev, NULL);
                buf_dev = VK_NULL_HANDLE;
            }
        }
    }

    if (!use_staging) {
        VkBufferCreateInfo bci = {0};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = nbytes;
        bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        r = vkCreateBuffer(yona_vk_dev, &bci, NULL, &buf);
        if (r != VK_SUCCESS) goto fail;

        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(yona_vk_dev, buf, &req);
        uint32_t mt = 0;
        if (yona_vk_pick_memory_type(req.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                     &mt) != 0) {
            yona_vk_note_cpy("gpu: no memory type with HOST_VISIBLE|HOST_COHERENT for SSBO");
            goto fail;
        }

        VkMemoryAllocateInfo mai = {0};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = mt;
        r = vkAllocateMemory(yona_vk_dev, &mai, NULL, &mem);
        if (r != VK_SUCCESS) goto fail;
        r = vkBindBufferMemory(yona_vk_dev, buf, mem, 0);
        if (r != VK_SUCCESS) goto fail;

        r = vkMapMemory(yona_vk_dev, mem, 0, nbytes, 0, &mapped);
        if (r != VK_SUCCESS) goto fail;
        memcpy(mapped, arr + 1, (size_t)nbytes);
    } else {
        r = vkMapMemory(yona_vk_dev, mem_stg, 0, nbytes, 0, &mapped);
        if (r != VK_SUCCESS) goto fail;
        memcpy(mapped, arr + 1, (size_t)nbytes);
    }

    VkDescriptorBufferInfo dbi = {0};
    dbi.buffer = use_staging ? buf_dev : buf;
    dbi.offset = 0;
    dbi.range = nbytes;

    VkWriteDescriptorSet w = {0};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = dset;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w.pBufferInfo = &dbi;
    vkUpdateDescriptorSets(yona_vk_dev, 1, &w, 0, NULL);

    VkCommandPoolCreateInfo cpci0 = {0};
    cpci0.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci0.queueFamilyIndex = yona_vk_queue_family;
    cpci0.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    r = vkCreateCommandPool(yona_vk_dev, &cpci0, NULL, &cpool);
    if (r != VK_SUCCESS) goto fail;

    VkCommandBufferAllocateInfo cbai = {0};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = cpool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    r = vkAllocateCommandBuffers(yona_vk_dev, &cbai, &cmd);
    if (r != VK_SUCCESS) goto fail;

    VkFenceCreateInfo fci = {0};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    r = vkCreateFence(yona_vk_dev, &fci, NULL, &fence);
    if (r != VK_SUCCESS) goto fail;

    VkCommandBufferBeginInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    r = vkBeginCommandBuffer(cmd, &bi);
    if (r != VK_SUCCESS) goto fail;

    if (use_staging) {
        VkBufferCopy region = {0};
        region.size = nbytes;
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmd, buf_stg, VK_ACCESS_HOST_WRITE_BIT,
                               VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT);
        vkCmdCopyBuffer(cmd, buf_stg, buf_dev, 1, &region);
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmd, buf_dev, VK_ACCESS_TRANSFER_WRITE_BIT,
                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe->pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe->pl, 0, 1, &dset, 0, NULL);

    char pc[16];
    memcpy(pc, &scalar, sizeof(int64_t));
    uint32_t ulen = (uint32_t)len;
    memcpy(pc + sizeof(int64_t), &ulen, sizeof(uint32_t));
    vkCmdPushConstants(cmd, pipe->pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 12, pc);

    uint32_t groups = ((uint32_t)len + 63u) / 64u;
    vkCmdDispatch(cmd, groups, 1, 1);

    if (use_staging) {
        VkBufferCopy region = {0};
        region.size = nbytes;
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmd, buf_dev, VK_ACCESS_SHADER_WRITE_BIT,
                               VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT);
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmd, buf_stg, VK_ACCESS_TRANSFER_READ_BIT,
                               VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT);
        vkCmdCopyBuffer(cmd, buf_dev, buf_stg, 1, &region);
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmd, buf_stg, VK_ACCESS_TRANSFER_WRITE_BIT,
                               VK_ACCESS_HOST_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_HOST_BIT);
    } else {
        VkMemoryBarrier mb = {0};
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                             0, 1, &mb, 0, NULL, 0, NULL);
    }

    r = vkEndCommandBuffer(cmd);
    if (r != VK_SUCCESS) goto fail;

    VkSubmitInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    r = vkQueueSubmit(yona_vk_queue, 1, &si, fence);
    if (r != VK_SUCCESS) goto fail;

    r = vkWaitForFences(yona_vk_dev, 1, &fence, VK_TRUE, UINT64_MAX);
    if (r != VK_SUCCESS) goto fail;

    if (vkInvalidateMappedMemoryRanges) {
        VkMappedMemoryRange inv = {0};
        inv.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        inv.memory = use_staging ? mem_stg : mem;
        inv.offset = 0;
        inv.size = nbytes;
        vkInvalidateMappedMemoryRanges(yona_vk_dev, 1, &inv);
    }

    int64_t* result = yona_rt_int_array_alloc(len);
    if (!result) {
        yona_vk_note_cpy("gpu: yona_rt_int_array_alloc failed");
        goto fail;
    }
    memcpy(result + 1, mapped, (size_t)nbytes);

    vkUnmapMemory(yona_vk_dev, use_staging ? mem_stg : mem);
    mapped = NULL;

    vkDestroyFence(yona_vk_dev, fence, NULL);
    fence = VK_NULL_HANDLE;
    vkFreeCommandBuffers(yona_vk_dev, cpool, 1, &cmd);
    cmd = VK_NULL_HANDLE;
    vkDestroyCommandPool(yona_vk_dev, cpool, NULL);
    cpool = VK_NULL_HANDLE;
    if (use_staging) {
        vkDestroyBuffer(yona_vk_dev, buf_stg, NULL);
        buf_stg = VK_NULL_HANDLE;
        vkFreeMemory(yona_vk_dev, mem_stg, NULL);
        mem_stg = VK_NULL_HANDLE;
        vkDestroyBuffer(yona_vk_dev, buf_dev, NULL);
        buf_dev = VK_NULL_HANDLE;
        vkFreeMemory(yona_vk_dev, mem_dev, NULL);
        mem_dev = VK_NULL_HANDLE;
    } else {
        vkDestroyBuffer(yona_vk_dev, buf, NULL);
        buf = VK_NULL_HANDLE;
        vkFreeMemory(yona_vk_dev, mem, NULL);
        mem = VK_NULL_HANDLE;
    }
    vkDestroyDescriptorPool(yona_vk_dev, dpool, NULL);
    dpool = VK_NULL_HANDLE;

    *out = result;
    return 1;

fail:
    if (!yona_vk_last_note[0]) {
        char b[120];
        snprintf(b, sizeof b, "%s: Vulkan failure VkResult=%d", fail_tag, (int)r);
        yona_vk_note_cpy(b);
    }
    if (mapped)
        vkUnmapMemory(yona_vk_dev, use_staging ? mem_stg : mem);
    if (fence != VK_NULL_HANDLE) vkDestroyFence(yona_vk_dev, fence, NULL);
    if (cmd != VK_NULL_HANDLE && cpool != VK_NULL_HANDLE)
        vkFreeCommandBuffers(yona_vk_dev, cpool, 1, &cmd);
    if (cpool != VK_NULL_HANDLE) vkDestroyCommandPool(yona_vk_dev, cpool, NULL);
    if (buf_stg != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_stg, NULL);
    if (mem_stg != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_stg, NULL);
    if (buf_dev != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_dev, NULL);
    if (mem_dev != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_dev, NULL);
    if (buf != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf, NULL);
    if (mem != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem, NULL);
    if (dpool != VK_NULL_HANDLE) vkDestroyDescriptorPool(yona_vk_dev, dpool, NULL);
    return 0;
}

int yona_gpu_vulkan_try_map_add_int64(int64_t delta, int64_t* arr, int64_t** out) {
    *out = NULL;
    if (!yona_vk_env_mapadd()) {
        yona_vk_note_cpy("mapadd: set YONA_GPU_VULKAN_MAPADD=1 or YONA_GPU_VULKAN_COMPUTE=1");
        return 0;
    }
    if (!yona_vk_common_precheck(arr, "mapadd")) return 0;

    int64_t min_len =
        yona_vk_min_len_two("YONA_GPU_VULKAN_MIN_LEN", "YONA_GPU_VULKAN_MAPADD_MIN_LEN");
    int64_t len = arr[0];
    if (len < min_len) {
        yona_vk_note_cpy("mapadd: IntArray shorter than configured GPU min length");
        return 0;
    }
    if (len > (int64_t)0x7fffffff) {
        yona_vk_note_cpy("mapadd: IntArray length exceeds supported range");
        return 0;
    }

    int ok = 0;
    yona_vk_compute_submit_lock();
    ok = yona_vk_run_simple_map("mapadd", yona_vk_compute_ensure_mapadd_pipe,
                               yona_vk_compute_mapadd_pipe(), delta, arr, out);
    yona_vk_compute_submit_unlock();
    return ok;
}

int yona_gpu_vulkan_try_map_mul_int64(int64_t factor, int64_t* arr, int64_t** out) {
    *out = NULL;
    if (!yona_vk_env_mapmul()) {
        yona_vk_note_cpy("mapmul: set YONA_GPU_VULKAN_MAPMUL=1 or YONA_GPU_VULKAN_COMPUTE=1");
        return 0;
    }
    if (!yona_vk_common_precheck(arr, "mapmul")) return 0;

    int64_t min_len =
        yona_vk_min_len_two("YONA_GPU_VULKAN_MIN_LEN", "YONA_GPU_VULKAN_MAPMUL_MIN_LEN");
    int64_t len = arr[0];
    if (len < min_len) {
        yona_vk_note_cpy("mapmul: IntArray shorter than configured GPU min length");
        return 0;
    }
    if (len > (int64_t)0x7fffffff) {
        yona_vk_note_cpy("mapmul: IntArray length exceeds supported range");
        return 0;
    }

    int ok = 0;
    yona_vk_compute_submit_lock();
    ok = yona_vk_run_simple_map("mapmul", yona_vk_compute_ensure_mapmul_pipe,
                               yona_vk_compute_mapmul_pipe(), factor, arr, out);
    yona_vk_compute_submit_unlock();
    return ok;
}

int yona_gpu_vulkan_try_reduce_sum_int64(int64_t* arr, int64_t* out_sum) {
    *out_sum = 0;
    if (!yona_vk_env_reduce()) {
        yona_vk_note_cpy("reduce: set YONA_GPU_VULKAN_REDUCE=1 or YONA_GPU_VULKAN_COMPUTE=1");
        return 0;
    }
    if (!yona_vk_common_precheck(arr, "reduce")) return 0;

    int64_t min_len =
        yona_vk_min_len_two("YONA_GPU_VULKAN_MIN_LEN", "YONA_GPU_VULKAN_REDUCE_MIN_LEN");
    int64_t len = arr[0];
    if (len < min_len) {
        yona_vk_note_cpy("reduce: IntArray shorter than configured GPU min length");
        return 0;
    }
    if (len > (int64_t)0x7fffffff) {
        yona_vk_note_cpy("reduce: IntArray length exceeds supported range");
        return 0;
    }

    PFN_vkCreateDescriptorPool vkCreateDescriptorPool;
    PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool;
    PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets;
    PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets;
    PFN_vkCreateBuffer vkCreateBuffer;
    PFN_vkDestroyBuffer vkDestroyBuffer;
    PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements;
    PFN_vkAllocateMemory vkAllocateMemory;
    PFN_vkFreeMemory vkFreeMemory;
    PFN_vkBindBufferMemory vkBindBufferMemory;
    PFN_vkMapMemory vkMapMemory;
    PFN_vkUnmapMemory vkUnmapMemory;
    PFN_vkInvalidateMappedMemoryRanges vkInvalidateMappedMemoryRanges;
    PFN_vkCreateCommandPool vkCreateCommandPool;
    PFN_vkDestroyCommandPool vkDestroyCommandPool;
    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
    PFN_vkFreeCommandBuffers vkFreeCommandBuffers;
    PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
    PFN_vkEndCommandBuffer vkEndCommandBuffer;
    PFN_vkCmdBindPipeline vkCmdBindPipeline;
    PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets;
    PFN_vkCmdPushConstants vkCmdPushConstants;
    PFN_vkCmdDispatch vkCmdDispatch;
    PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;
    PFN_vkCmdCopyBuffer vkCmdCopyBuffer;
    PFN_vkCreateFence vkCreateFence;
    PFN_vkDestroyFence vkDestroyFence;
    PFN_vkQueueSubmit vkQueueSubmit;
    PFN_vkWaitForFences vkWaitForFences;
    PFN_vkResetFences vkResetFences;

    if (!yona_vk_load_dispatch_pfns(
            &vkCreateDescriptorPool, &vkDestroyDescriptorPool, &vkAllocateDescriptorSets,
            &vkUpdateDescriptorSets, &vkCreateBuffer, &vkDestroyBuffer,
            &vkGetBufferMemoryRequirements, &vkAllocateMemory, &vkFreeMemory, &vkBindBufferMemory,
            &vkMapMemory, &vkUnmapMemory, &vkInvalidateMappedMemoryRanges, &vkCreateCommandPool,
            &vkDestroyCommandPool, &vkAllocateCommandBuffers, &vkFreeCommandBuffers,
            &vkBeginCommandBuffer, &vkEndCommandBuffer, &vkCmdBindPipeline, &vkCmdBindDescriptorSets,
            &vkCmdPushConstants, &vkCmdDispatch, &vkCmdPipelineBarrier, &vkCreateFence,
            &vkDestroyFence, &vkQueueSubmit, &vkWaitForFences, &vkResetFences)) {
        yona_vk_note_cpy("gpu: vkGetDeviceProcAddr returned null for a required entry point");
        return 0;
    }
    vkCmdCopyBuffer = YONA_VK_DPA(vkCmdCopyBuffer);

    yona_vk_compute_submit_lock();

    VkResult r = VK_SUCCESS;
    YonaVkReducePipe* rp = yona_vk_compute_reduce_pipe();
    r = yona_vk_compute_ensure_reduce_pipe();
    if (r != VK_SUCCESS) {
        char b[120];
        snprintf(b, sizeof b, "reduce: pipeline ensure VkResult=%d", (int)r);
        yona_vk_note_cpy(b);
        yona_vk_compute_submit_unlock();
        return 0;
    }

    uint32_t ulen = (uint32_t)len;
    uint32_t groups = (ulen + 63u) / 64u;
    VkDeviceSize nbytes_in = (VkDeviceSize)((size_t)len * sizeof(int64_t));
    VkDeviceSize nbytes_sums = (VkDeviceSize)((size_t)groups * sizeof(int64_t));

    int use_staging = 0;
    VkDescriptorPool dpool = VK_NULL_HANDLE;
    VkDescriptorSet dset = VK_NULL_HANDLE;
    VkBuffer buf_in = VK_NULL_HANDLE;
    VkBuffer buf_sums = VK_NULL_HANDLE;
    VkDeviceMemory mem_in = VK_NULL_HANDLE;
    VkDeviceMemory mem_sums = VK_NULL_HANDLE;
    VkBuffer buf_in_dev = VK_NULL_HANDLE;
    VkBuffer buf_in_stg = VK_NULL_HANDLE;
    VkDeviceMemory mem_in_dev = VK_NULL_HANDLE;
    VkDeviceMemory mem_in_stg = VK_NULL_HANDLE;
    VkBuffer buf_sums_dev = VK_NULL_HANDLE;
    VkBuffer buf_sums_stg = VK_NULL_HANDLE;
    VkDeviceMemory mem_sums_dev = VK_NULL_HANDLE;
    VkDeviceMemory mem_sums_stg = VK_NULL_HANDLE;
    VkCommandPool cpool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    void* mapped_in = NULL;
    void* mapped_sums = NULL;
    VkDeviceMemory mapped_mem_in = VK_NULL_HANDLE;
    VkDeviceMemory mapped_mem_sums = VK_NULL_HANDLE;

    if (!yona_vk_force_host_ssbo() && vkCmdCopyBuffer) {
        if (yona_vk_try_dev_stg_pair(vkCreateBuffer, vkDestroyBuffer, vkGetBufferMemoryRequirements,
                                     vkAllocateMemory, vkFreeMemory, vkBindBufferMemory, nbytes_in,
                                     &buf_in_dev, &mem_in_dev, &buf_in_stg, &mem_in_stg) &&
            yona_vk_try_dev_stg_pair(vkCreateBuffer, vkDestroyBuffer, vkGetBufferMemoryRequirements,
                                     vkAllocateMemory, vkFreeMemory, vkBindBufferMemory,
                                     nbytes_sums, &buf_sums_dev, &mem_sums_dev, &buf_sums_stg,
                                     &mem_sums_stg))
            use_staging = 1;
        else {
            if (buf_sums_stg != VK_NULL_HANDLE) {
                vkDestroyBuffer(yona_vk_dev, buf_sums_stg, NULL);
                buf_sums_stg = VK_NULL_HANDLE;
            }
            if (mem_sums_stg != VK_NULL_HANDLE) {
                vkFreeMemory(yona_vk_dev, mem_sums_stg, NULL);
                mem_sums_stg = VK_NULL_HANDLE;
            }
            if (buf_sums_dev != VK_NULL_HANDLE) {
                vkDestroyBuffer(yona_vk_dev, buf_sums_dev, NULL);
                buf_sums_dev = VK_NULL_HANDLE;
            }
            if (mem_sums_dev != VK_NULL_HANDLE) {
                vkFreeMemory(yona_vk_dev, mem_sums_dev, NULL);
                mem_sums_dev = VK_NULL_HANDLE;
            }
            if (buf_in_stg != VK_NULL_HANDLE) {
                vkDestroyBuffer(yona_vk_dev, buf_in_stg, NULL);
                buf_in_stg = VK_NULL_HANDLE;
            }
            if (mem_in_stg != VK_NULL_HANDLE) {
                vkFreeMemory(yona_vk_dev, mem_in_stg, NULL);
                mem_in_stg = VK_NULL_HANDLE;
            }
            if (buf_in_dev != VK_NULL_HANDLE) {
                vkDestroyBuffer(yona_vk_dev, buf_in_dev, NULL);
                buf_in_dev = VK_NULL_HANDLE;
            }
            if (mem_in_dev != VK_NULL_HANDLE) {
                vkFreeMemory(yona_vk_dev, mem_in_dev, NULL);
                mem_in_dev = VK_NULL_HANDLE;
            }
        }
    }

    VkDescriptorPoolSize dps = {0};
    dps.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    dps.descriptorCount = 2;

    VkDescriptorPoolCreateInfo dpci = {0};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &dps;
    r = vkCreateDescriptorPool(yona_vk_dev, &dpci, NULL, &dpool);
    if (r != VK_SUCCESS) goto reduce_fail;

    VkDescriptorSetAllocateInfo dsai = {0};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = dpool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &rp->dsl;
    r = vkAllocateDescriptorSets(yona_vk_dev, &dsai, &dset);
    if (r != VK_SUCCESS) goto reduce_fail;

    if (!use_staging) {
        VkBufferCreateInfo bci = {0};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = nbytes_in;
        bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        r = vkCreateBuffer(yona_vk_dev, &bci, NULL, &buf_in);
        if (r != VK_SUCCESS) goto reduce_fail;

        bci.size = nbytes_sums;
        r = vkCreateBuffer(yona_vk_dev, &bci, NULL, &buf_sums);
        if (r != VK_SUCCESS) goto reduce_fail;

        VkMemoryRequirements req_in, req_s;
        vkGetBufferMemoryRequirements(yona_vk_dev, buf_in, &req_in);
        vkGetBufferMemoryRequirements(yona_vk_dev, buf_sums, &req_s);
        uint32_t mt_in = 0, mt_s = 0;
        if (yona_vk_pick_memory_type(req_in.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                     &mt_in) != 0 ||
            yona_vk_pick_memory_type(req_s.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                     &mt_s) != 0) {
            yona_vk_note_cpy("reduce: no HOST_VISIBLE|HOST_COHERENT memory for buffers");
            goto reduce_fail;
        }

        VkMemoryAllocateInfo mai = {0};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = req_in.size;
        mai.memoryTypeIndex = mt_in;
        r = vkAllocateMemory(yona_vk_dev, &mai, NULL, &mem_in);
        if (r != VK_SUCCESS) goto reduce_fail;
        r = vkBindBufferMemory(yona_vk_dev, buf_in, mem_in, 0);
        if (r != VK_SUCCESS) goto reduce_fail;

        mai.allocationSize = req_s.size;
        mai.memoryTypeIndex = mt_s;
        r = vkAllocateMemory(yona_vk_dev, &mai, NULL, &mem_sums);
        if (r != VK_SUCCESS) goto reduce_fail;
        r = vkBindBufferMemory(yona_vk_dev, buf_sums, mem_sums, 0);
        if (r != VK_SUCCESS) goto reduce_fail;

        r = vkMapMemory(yona_vk_dev, mem_in, 0, nbytes_in, 0, &mapped_in);
        if (r != VK_SUCCESS) goto reduce_fail;
        mapped_mem_in = mem_in;
        r = vkMapMemory(yona_vk_dev, mem_sums, 0, nbytes_sums, 0, &mapped_sums);
        if (r != VK_SUCCESS) goto reduce_fail;
        mapped_mem_sums = mem_sums;
        memcpy(mapped_in, arr + 1, (size_t)nbytes_in);
        memset(mapped_sums, 0, (size_t)nbytes_sums);
    } else {
        r = vkMapMemory(yona_vk_dev, mem_in_stg, 0, nbytes_in, 0, &mapped_in);
        if (r != VK_SUCCESS) goto reduce_fail;
        mapped_mem_in = mem_in_stg;
        r = vkMapMemory(yona_vk_dev, mem_sums_stg, 0, nbytes_sums, 0, &mapped_sums);
        if (r != VK_SUCCESS) goto reduce_fail;
        mapped_mem_sums = mem_sums_stg;
        memcpy(mapped_in, arr + 1, (size_t)nbytes_in);
        memset(mapped_sums, 0, (size_t)nbytes_sums);
    }

    VkDescriptorBufferInfo dbi[2] = {0};
    dbi[0].buffer = use_staging ? buf_in_dev : buf_in;
    dbi[0].offset = 0;
    dbi[0].range = nbytes_in;
    dbi[1].buffer = use_staging ? buf_sums_dev : buf_sums;
    dbi[1].offset = 0;
    dbi[1].range = nbytes_sums;

    VkWriteDescriptorSet w[2] = {0};
    w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].dstSet = dset;
    w[0].dstBinding = 0;
    w[0].descriptorCount = 1;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[0].pBufferInfo = &dbi[0];
    w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[1].dstSet = dset;
    w[1].dstBinding = 1;
    w[1].descriptorCount = 1;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[1].pBufferInfo = &dbi[1];
    vkUpdateDescriptorSets(yona_vk_dev, 2, w, 0, NULL);

    VkCommandPoolCreateInfo cpci0 = {0};
    cpci0.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci0.queueFamilyIndex = yona_vk_queue_family;
    cpci0.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    r = vkCreateCommandPool(yona_vk_dev, &cpci0, NULL, &cpool);
    if (r != VK_SUCCESS) goto reduce_fail;

    VkCommandBufferAllocateInfo cbai = {0};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = cpool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    r = vkAllocateCommandBuffers(yona_vk_dev, &cbai, &cmd);
    if (r != VK_SUCCESS) goto reduce_fail;

    VkFenceCreateInfo fci = {0};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    r = vkCreateFence(yona_vk_dev, &fci, NULL, &fence);
    if (r != VK_SUCCESS) goto reduce_fail;

    VkCommandBufferBeginInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    r = vkBeginCommandBuffer(cmd, &bi);
    if (r != VK_SUCCESS) goto reduce_fail;

    if (use_staging) {
        VkBufferCopy c0 = {0};
        c0.size = nbytes_in;
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmd, buf_in_stg, VK_ACCESS_HOST_WRITE_BIT,
                               VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT);
        vkCmdCopyBuffer(cmd, buf_in_stg, buf_in_dev, 1, &c0);
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmd, buf_in_dev, VK_ACCESS_TRANSFER_WRITE_BIT,
                               VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmd, buf_sums_dev,
                               0u, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rp->pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rp->pl, 0, 1, &dset, 0, NULL);
    vkCmdPushConstants(cmd, rp->pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &ulen);
    vkCmdDispatch(cmd, groups, 1, 1);

    if (use_staging) {
        VkBufferCopy c1 = {0};
        c1.size = nbytes_sums;
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmd, buf_sums_dev, VK_ACCESS_SHADER_WRITE_BIT,
                               VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT);
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmd, buf_sums_stg, VK_ACCESS_TRANSFER_READ_BIT,
                               VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT);
        vkCmdCopyBuffer(cmd, buf_sums_dev, buf_sums_stg, 1, &c1);
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmd, buf_sums_stg, VK_ACCESS_TRANSFER_WRITE_BIT,
                               VK_ACCESS_HOST_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_HOST_BIT);
    } else {
        VkMemoryBarrier mb = {0};
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                             0, 1, &mb, 0, NULL, 0, NULL);
    }

    r = vkEndCommandBuffer(cmd);
    if (r != VK_SUCCESS) goto reduce_fail;

    VkSubmitInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    r = vkQueueSubmit(yona_vk_queue, 1, &si, fence);
    if (r != VK_SUCCESS) goto reduce_fail;

    r = vkWaitForFences(yona_vk_dev, 1, &fence, VK_TRUE, UINT64_MAX);
    if (r != VK_SUCCESS) goto reduce_fail;

    if (vkInvalidateMappedMemoryRanges) {
        if (use_staging) {
            VkMappedMemoryRange inv = {0};
            inv.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            inv.memory = mem_sums_stg;
            inv.offset = 0;
            inv.size = nbytes_sums;
            vkInvalidateMappedMemoryRanges(yona_vk_dev, 1, &inv);
        } else {
            VkMappedMemoryRange inv[2] = {0};
            inv[0].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            inv[0].memory = mem_in;
            inv[0].offset = 0;
            inv[0].size = nbytes_in;
            inv[1].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            inv[1].memory = mem_sums;
            inv[1].offset = 0;
            inv[1].size = nbytes_sums;
            vkInvalidateMappedMemoryRanges(yona_vk_dev, 2, inv);
        }
    }

    int64_t total = 0;
    int64_t* lanes = (int64_t*)mapped_sums;
    for (uint32_t i = 0; i < groups; i++) total += lanes[i];
    *out_sum = total;

    vkUnmapMemory(yona_vk_dev, mapped_mem_in);
    vkUnmapMemory(yona_vk_dev, mapped_mem_sums);
    mapped_in = NULL;
    mapped_sums = NULL;

    vkDestroyFence(yona_vk_dev, fence, NULL);
    fence = VK_NULL_HANDLE;
    vkFreeCommandBuffers(yona_vk_dev, cpool, 1, &cmd);
    cmd = VK_NULL_HANDLE;
    vkDestroyCommandPool(yona_vk_dev, cpool, NULL);
    cpool = VK_NULL_HANDLE;
    if (use_staging) {
        vkDestroyBuffer(yona_vk_dev, buf_in_stg, NULL);
        buf_in_stg = VK_NULL_HANDLE;
        vkFreeMemory(yona_vk_dev, mem_in_stg, NULL);
        mem_in_stg = VK_NULL_HANDLE;
        vkDestroyBuffer(yona_vk_dev, buf_in_dev, NULL);
        buf_in_dev = VK_NULL_HANDLE;
        vkFreeMemory(yona_vk_dev, mem_in_dev, NULL);
        mem_in_dev = VK_NULL_HANDLE;
        vkDestroyBuffer(yona_vk_dev, buf_sums_stg, NULL);
        buf_sums_stg = VK_NULL_HANDLE;
        vkFreeMemory(yona_vk_dev, mem_sums_stg, NULL);
        mem_sums_stg = VK_NULL_HANDLE;
        vkDestroyBuffer(yona_vk_dev, buf_sums_dev, NULL);
        buf_sums_dev = VK_NULL_HANDLE;
        vkFreeMemory(yona_vk_dev, mem_sums_dev, NULL);
        mem_sums_dev = VK_NULL_HANDLE;
    } else {
        vkDestroyBuffer(yona_vk_dev, buf_in, NULL);
        vkDestroyBuffer(yona_vk_dev, buf_sums, NULL);
        buf_in = VK_NULL_HANDLE;
        buf_sums = VK_NULL_HANDLE;
        vkFreeMemory(yona_vk_dev, mem_in, NULL);
        vkFreeMemory(yona_vk_dev, mem_sums, NULL);
        mem_in = VK_NULL_HANDLE;
        mem_sums = VK_NULL_HANDLE;
    }
    vkDestroyDescriptorPool(yona_vk_dev, dpool, NULL);
    dpool = VK_NULL_HANDLE;

    yona_vk_compute_submit_unlock();
    return 1;

reduce_fail:
    if (!yona_vk_last_note[0]) {
        char b[120];
        snprintf(b, sizeof b, "reduce: Vulkan failure VkResult=%d", (int)r);
        yona_vk_note_cpy(b);
    }
    if (mapped_mem_in != VK_NULL_HANDLE) vkUnmapMemory(yona_vk_dev, mapped_mem_in);
    if (mapped_mem_sums != VK_NULL_HANDLE) vkUnmapMemory(yona_vk_dev, mapped_mem_sums);
    if (fence != VK_NULL_HANDLE) vkDestroyFence(yona_vk_dev, fence, NULL);
    if (cmd != VK_NULL_HANDLE && cpool != VK_NULL_HANDLE)
        vkFreeCommandBuffers(yona_vk_dev, cpool, 1, &cmd);
    if (cpool != VK_NULL_HANDLE) vkDestroyCommandPool(yona_vk_dev, cpool, NULL);
    if (use_staging) {
        if (buf_in_stg != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_in_stg, NULL);
        if (mem_in_stg != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_in_stg, NULL);
        if (buf_in_dev != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_in_dev, NULL);
        if (mem_in_dev != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_in_dev, NULL);
        if (buf_sums_stg != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_sums_stg, NULL);
        if (mem_sums_stg != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_sums_stg, NULL);
        if (buf_sums_dev != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_sums_dev, NULL);
        if (mem_sums_dev != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_sums_dev, NULL);
    } else {
        if (buf_in != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_in, NULL);
        if (buf_sums != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_sums, NULL);
        if (mem_in != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_in, NULL);
        if (mem_sums != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_sums, NULL);
    }
    if (dpool != VK_NULL_HANDLE) vkDestroyDescriptorPool(yona_vk_dev, dpool, NULL);
    yona_vk_compute_submit_unlock();
    return 0;
}

int yona_gpu_vulkan_try_filter_greater_than_int64(int64_t threshold, int64_t* arr, int64_t** out) {
    *out = NULL;
    if (!yona_vk_env_filter()) {
        yona_vk_note_cpy("filter: set YONA_GPU_VULKAN_FILTER=1 or YONA_GPU_VULKAN_COMPUTE=1");
        return 0;
    }
    if (!yona_vk_common_precheck(arr, "filter")) return 0;

    int64_t min_len =
        yona_vk_min_len_two("YONA_GPU_VULKAN_MIN_LEN", "YONA_GPU_VULKAN_FILTER_MIN_LEN");
    int64_t len = arr[0];
    if (len < min_len) {
        yona_vk_note_cpy("filter: IntArray shorter than configured GPU min length");
        return 0;
    }
    if (len > (int64_t)0x7fffffff) {
        yona_vk_note_cpy("filter: IntArray length exceeds supported range");
        return 0;
    }

    PFN_vkCreateDescriptorPool vkCreateDescriptorPool;
    PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool;
    PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets;
    PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets;
    PFN_vkCreateBuffer vkCreateBuffer;
    PFN_vkDestroyBuffer vkDestroyBuffer;
    PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements;
    PFN_vkAllocateMemory vkAllocateMemory;
    PFN_vkFreeMemory vkFreeMemory;
    PFN_vkBindBufferMemory vkBindBufferMemory;
    PFN_vkMapMemory vkMapMemory;
    PFN_vkUnmapMemory vkUnmapMemory;
    PFN_vkInvalidateMappedMemoryRanges vkInvalidateMappedMemoryRanges;
    PFN_vkFlushMappedMemoryRanges vkFlushMappedMemoryRanges;
    PFN_vkCreateCommandPool vkCreateCommandPool;
    PFN_vkDestroyCommandPool vkDestroyCommandPool;
    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
    PFN_vkFreeCommandBuffers vkFreeCommandBuffers;
    PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
    PFN_vkEndCommandBuffer vkEndCommandBuffer;
    PFN_vkCmdBindPipeline vkCmdBindPipeline;
    PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets;
    PFN_vkCmdPushConstants vkCmdPushConstants;
    PFN_vkCmdDispatch vkCmdDispatch;
    PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;
    PFN_vkCreateFence vkCreateFence;
    PFN_vkDestroyFence vkDestroyFence;
    PFN_vkQueueSubmit vkQueueSubmit;
    PFN_vkWaitForFences vkWaitForFences;
    PFN_vkResetFences vkResetFences;

    if (!yona_vk_load_dispatch_pfns(
            &vkCreateDescriptorPool, &vkDestroyDescriptorPool, &vkAllocateDescriptorSets,
            &vkUpdateDescriptorSets, &vkCreateBuffer, &vkDestroyBuffer,
            &vkGetBufferMemoryRequirements, &vkAllocateMemory, &vkFreeMemory, &vkBindBufferMemory,
            &vkMapMemory, &vkUnmapMemory, &vkInvalidateMappedMemoryRanges, &vkCreateCommandPool,
            &vkDestroyCommandPool, &vkAllocateCommandBuffers, &vkFreeCommandBuffers,
            &vkBeginCommandBuffer, &vkEndCommandBuffer, &vkCmdBindPipeline, &vkCmdBindDescriptorSets,
            &vkCmdPushConstants, &vkCmdDispatch, &vkCmdPipelineBarrier, &vkCreateFence,
            &vkDestroyFence, &vkQueueSubmit, &vkWaitForFences, &vkResetFences)) {
        yona_vk_note_cpy("gpu: vkGetDeviceProcAddr returned null for a required entry point");
        return 0;
    }
    vkFlushMappedMemoryRanges = YONA_VK_DPA(vkFlushMappedMemoryRanges);

    yona_vk_compute_submit_lock();

    VkResult r = VK_SUCCESS;
    const int gpu_prefix = !yona_vk_filter_use_cpu_prefix();
    YonaVkReducePipe* mp = yona_vk_compute_filter_mark_pipe();
    YonaVkScatterPipe* sp = yona_vk_compute_filter_scatter_pipe();
    YonaVkReducePipe* ft = NULL;
    YonaVkReducePipe* pp = NULL;
    YonaVkScatterPipe* ix = NULL;
    r = yona_vk_compute_ensure_filter_mark_pipe();
    if (r != VK_SUCCESS) {
        char b[120];
        snprintf(b, sizeof b, "filter: mark pipeline ensure VkResult=%d", (int)r);
        yona_vk_note_cpy(b);
        yona_vk_compute_submit_unlock();
        return 0;
    }
    r = yona_vk_compute_ensure_filter_scatter_pipe();
    if (r != VK_SUCCESS) {
        char b[120];
        snprintf(b, sizeof b, "filter: scatter pipeline ensure VkResult=%d", (int)r);
        yona_vk_note_cpy(b);
        yona_vk_compute_submit_unlock();
        return 0;
    }
    if (gpu_prefix) {
        r = yona_vk_compute_ensure_filter_flags_to_i64_pipe();
        if (r != VK_SUCCESS) {
            char b[120];
            snprintf(b, sizeof b, "filter: flags_to_i64 pipeline ensure VkResult=%d", (int)r);
            yona_vk_note_cpy(b);
            yona_vk_compute_submit_unlock();
            return 0;
        }
        r = yona_vk_compute_ensure_filter_prefix_pipe();
        if (r != VK_SUCCESS) {
            char b[120];
            snprintf(b, sizeof b, "filter: prefix pipeline ensure VkResult=%d", (int)r);
            yona_vk_note_cpy(b);
            yona_vk_compute_submit_unlock();
            return 0;
        }
        r = yona_vk_compute_ensure_filter_inc_to_exc_pipe();
        if (r != VK_SUCCESS) {
            char b[120];
            snprintf(b, sizeof b, "filter: inc_to_exc pipeline ensure VkResult=%d", (int)r);
            yona_vk_note_cpy(b);
            yona_vk_compute_submit_unlock();
            return 0;
        }
        ft = yona_vk_compute_filter_flags_to_i64_pipe();
        pp = yona_vk_compute_filter_prefix_pipe();
        ix = yona_vk_compute_filter_inc_to_exc_pipe();
    }

    PFN_vkCmdCopyBuffer vkCmdCopyBuffer = YONA_VK_DPA(vkCmdCopyBuffer);

    uint32_t ulen = (uint32_t)len;
    VkDeviceSize nbytes_in = (VkDeviceSize)((size_t)len * sizeof(int64_t));
    VkDeviceSize nbytes_flags = (VkDeviceSize)((size_t)ulen * sizeof(int32_t));
    VkDeviceSize nbytes_prefix = nbytes_in;
    VkDeviceSize nbytes_out = nbytes_in;

    int use_staging = 0;
    VkDescriptorPool dpool = VK_NULL_HANDLE;
    VkDescriptorSet dsets[6] = {VK_NULL_HANDLE};
    VkBuffer buf_in = VK_NULL_HANDLE;
    VkBuffer buf_flags = VK_NULL_HANDLE;
    VkBuffer buf_prefix = VK_NULL_HANDLE;
    VkBuffer buf_out = VK_NULL_HANDLE;
    VkDeviceMemory mem_in = VK_NULL_HANDLE;
    VkDeviceMemory mem_flags = VK_NULL_HANDLE;
    VkDeviceMemory mem_prefix = VK_NULL_HANDLE;
    VkDeviceMemory mem_out = VK_NULL_HANDLE;
    VkBuffer buf_in_dev = VK_NULL_HANDLE;
    VkBuffer buf_in_stg = VK_NULL_HANDLE;
    VkBuffer buf_flags_dev = VK_NULL_HANDLE;
    VkBuffer buf_flags_stg = VK_NULL_HANDLE;
    VkBuffer buf_prefix_dev = VK_NULL_HANDLE;
    VkBuffer buf_prefix_stg = VK_NULL_HANDLE;
    VkBuffer buf_out_dev = VK_NULL_HANDLE;
    VkBuffer buf_out_stg = VK_NULL_HANDLE;
    VkDeviceMemory mem_in_dev = VK_NULL_HANDLE;
    VkDeviceMemory mem_in_stg = VK_NULL_HANDLE;
    VkDeviceMemory mem_flags_dev = VK_NULL_HANDLE;
    VkDeviceMemory mem_flags_stg = VK_NULL_HANDLE;
    VkDeviceMemory mem_prefix_dev = VK_NULL_HANDLE;
    VkDeviceMemory mem_prefix_stg = VK_NULL_HANDLE;
    VkDeviceMemory mem_out_dev = VK_NULL_HANDLE;
    VkDeviceMemory mem_out_stg = VK_NULL_HANDLE;
    VkCommandPool cpool = VK_NULL_HANDLE;
    VkCommandBuffer cmds[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkFence fence = VK_NULL_HANDLE;
    void* p_in = NULL;
    void* p_flags = NULL;
    void* p_prefix = NULL;
    void* p_out = NULL;
    VkDeviceMemory map_mem_in = VK_NULL_HANDLE;
    VkDeviceMemory map_mem_flags = VK_NULL_HANDLE;
    VkDeviceMemory map_mem_prefix = VK_NULL_HANDLE;
    VkDeviceMemory map_mem_out = VK_NULL_HANDLE;

    VkBuffer buf_scan0 = VK_NULL_HANDLE;
    VkBuffer buf_scan1 = VK_NULL_HANDLE;
    VkDeviceMemory mem_scan0 = VK_NULL_HANDLE;
    VkDeviceMemory mem_scan1 = VK_NULL_HANDLE;
    void* p_scan0 = NULL;
    void* p_scan1 = NULL;
    VkDeviceMemory map_mem_scan0 = VK_NULL_HANDLE;
    VkDeviceMemory map_mem_scan1 = VK_NULL_HANDLE;
    VkBuffer buf_scan0_dev = VK_NULL_HANDLE;
    VkBuffer buf_scan1_dev = VK_NULL_HANDLE;
    VkDeviceMemory mem_scan0_dev = VK_NULL_HANDLE;
    VkDeviceMemory mem_scan1_dev = VK_NULL_HANDLE;
    VkBuffer buf_count_stg = VK_NULL_HANDLE;
    VkDeviceMemory mem_count_stg = VK_NULL_HANDLE;
    void* p_count = NULL;
    VkDeviceMemory map_mem_count = VK_NULL_HANDLE;

    const uint32_t filter_desc_sets = gpu_prefix ? 6u : 2u;
    const uint32_t filter_desc_writes = gpu_prefix ? 15u : 6u;

    VkDescriptorPoolSize dps[1] = {0};
    dps[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    dps[0].descriptorCount = filter_desc_writes;

    VkDescriptorPoolCreateInfo dpci = {0};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = filter_desc_sets;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = dps;
    r = vkCreateDescriptorPool(yona_vk_dev, &dpci, NULL, &dpool);
    if (r != VK_SUCCESS) goto filt_fail;

    VkDescriptorSetLayout layouts_alloc[6];
    layouts_alloc[0] = mp->dsl;
    layouts_alloc[1] = sp->dsl;
    if (gpu_prefix) {
        layouts_alloc[2] = ft->dsl;
        layouts_alloc[3] = pp->dsl;
        layouts_alloc[4] = pp->dsl;
        layouts_alloc[5] = ix->dsl;
    }
    VkDescriptorSetAllocateInfo dsai = {0};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = dpool;
    dsai.descriptorSetCount = filter_desc_sets;
    dsai.pSetLayouts = layouts_alloc;
    r = vkAllocateDescriptorSets(yona_vk_dev, &dsai, dsets);
    if (r != VK_SUCCESS) goto filt_fail;

    if (!yona_vk_force_host_ssbo() && vkCmdCopyBuffer &&
        yona_vk_try_dev_stg_pair(vkCreateBuffer, vkDestroyBuffer, vkGetBufferMemoryRequirements,
                                 vkAllocateMemory, vkFreeMemory, vkBindBufferMemory, nbytes_in,
                                 &buf_in_dev, &mem_in_dev, &buf_in_stg, &mem_in_stg) &&
        yona_vk_try_dev_stg_pair(vkCreateBuffer, vkDestroyBuffer, vkGetBufferMemoryRequirements,
                                 vkAllocateMemory, vkFreeMemory, vkBindBufferMemory, nbytes_flags,
                                 &buf_flags_dev, &mem_flags_dev, &buf_flags_stg, &mem_flags_stg) &&
        yona_vk_try_dev_stg_pair(vkCreateBuffer, vkDestroyBuffer, vkGetBufferMemoryRequirements,
                                 vkAllocateMemory, vkFreeMemory, vkBindBufferMemory, nbytes_prefix,
                                 &buf_prefix_dev, &mem_prefix_dev, &buf_prefix_stg,
                                 &mem_prefix_stg) &&
        yona_vk_try_dev_stg_pair(vkCreateBuffer, vkDestroyBuffer, vkGetBufferMemoryRequirements,
                                 vkAllocateMemory, vkFreeMemory, vkBindBufferMemory, nbytes_out,
                                 &buf_out_dev, &mem_out_dev, &buf_out_stg, &mem_out_stg))
        use_staging = 1;
    else {
        if (buf_out_stg != VK_NULL_HANDLE) {
            vkDestroyBuffer(yona_vk_dev, buf_out_stg, NULL);
            buf_out_stg = VK_NULL_HANDLE;
        }
        if (mem_out_stg != VK_NULL_HANDLE) {
            vkFreeMemory(yona_vk_dev, mem_out_stg, NULL);
            mem_out_stg = VK_NULL_HANDLE;
        }
        if (buf_out_dev != VK_NULL_HANDLE) {
            vkDestroyBuffer(yona_vk_dev, buf_out_dev, NULL);
            buf_out_dev = VK_NULL_HANDLE;
        }
        if (mem_out_dev != VK_NULL_HANDLE) {
            vkFreeMemory(yona_vk_dev, mem_out_dev, NULL);
            mem_out_dev = VK_NULL_HANDLE;
        }
        if (buf_prefix_stg != VK_NULL_HANDLE) {
            vkDestroyBuffer(yona_vk_dev, buf_prefix_stg, NULL);
            buf_prefix_stg = VK_NULL_HANDLE;
        }
        if (mem_prefix_stg != VK_NULL_HANDLE) {
            vkFreeMemory(yona_vk_dev, mem_prefix_stg, NULL);
            mem_prefix_stg = VK_NULL_HANDLE;
        }
        if (buf_prefix_dev != VK_NULL_HANDLE) {
            vkDestroyBuffer(yona_vk_dev, buf_prefix_dev, NULL);
            buf_prefix_dev = VK_NULL_HANDLE;
        }
        if (mem_prefix_dev != VK_NULL_HANDLE) {
            vkFreeMemory(yona_vk_dev, mem_prefix_dev, NULL);
            mem_prefix_dev = VK_NULL_HANDLE;
        }
        if (buf_flags_stg != VK_NULL_HANDLE) {
            vkDestroyBuffer(yona_vk_dev, buf_flags_stg, NULL);
            buf_flags_stg = VK_NULL_HANDLE;
        }
        if (mem_flags_stg != VK_NULL_HANDLE) {
            vkFreeMemory(yona_vk_dev, mem_flags_stg, NULL);
            mem_flags_stg = VK_NULL_HANDLE;
        }
        if (buf_flags_dev != VK_NULL_HANDLE) {
            vkDestroyBuffer(yona_vk_dev, buf_flags_dev, NULL);
            buf_flags_dev = VK_NULL_HANDLE;
        }
        if (mem_flags_dev != VK_NULL_HANDLE) {
            vkFreeMemory(yona_vk_dev, mem_flags_dev, NULL);
            mem_flags_dev = VK_NULL_HANDLE;
        }
        if (buf_in_stg != VK_NULL_HANDLE) {
            vkDestroyBuffer(yona_vk_dev, buf_in_stg, NULL);
            buf_in_stg = VK_NULL_HANDLE;
        }
        if (mem_in_stg != VK_NULL_HANDLE) {
            vkFreeMemory(yona_vk_dev, mem_in_stg, NULL);
            mem_in_stg = VK_NULL_HANDLE;
        }
        if (buf_in_dev != VK_NULL_HANDLE) {
            vkDestroyBuffer(yona_vk_dev, buf_in_dev, NULL);
            buf_in_dev = VK_NULL_HANDLE;
        }
        if (mem_in_dev != VK_NULL_HANDLE) {
            vkFreeMemory(yona_vk_dev, mem_in_dev, NULL);
            mem_in_dev = VK_NULL_HANDLE;
        }
    }

    if (!use_staging) {
        VkBufferCreateInfo bci = {0};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = nbytes_in;
        bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        r = vkCreateBuffer(yona_vk_dev, &bci, NULL, &buf_in);
        if (r != VK_SUCCESS) goto filt_fail;

        bci.size = nbytes_flags;
        r = vkCreateBuffer(yona_vk_dev, &bci, NULL, &buf_flags);
        if (r != VK_SUCCESS) goto filt_fail;

        bci.size = nbytes_prefix;
        r = vkCreateBuffer(yona_vk_dev, &bci, NULL, &buf_prefix);
        if (r != VK_SUCCESS) goto filt_fail;

        bci.size = nbytes_out;
        r = vkCreateBuffer(yona_vk_dev, &bci, NULL, &buf_out);
        if (r != VK_SUCCESS) goto filt_fail;

        VkMemoryRequirements rq_in, rq_f, rq_p, rq_o;
        vkGetBufferMemoryRequirements(yona_vk_dev, buf_in, &rq_in);
        vkGetBufferMemoryRequirements(yona_vk_dev, buf_flags, &rq_f);
        vkGetBufferMemoryRequirements(yona_vk_dev, buf_prefix, &rq_p);
        vkGetBufferMemoryRequirements(yona_vk_dev, buf_out, &rq_o);
        uint32_t mt_in = 0, mt_f = 0, mt_p = 0, mt_o = 0;
        const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if (yona_vk_pick_memory_type(rq_in.memoryTypeBits, want, &mt_in) != 0 ||
            yona_vk_pick_memory_type(rq_f.memoryTypeBits, want, &mt_f) != 0 ||
            yona_vk_pick_memory_type(rq_p.memoryTypeBits, want, &mt_p) != 0 ||
            yona_vk_pick_memory_type(rq_o.memoryTypeBits, want, &mt_o) != 0) {
            yona_vk_note_cpy("filter: no HOST_VISIBLE|HOST_COHERENT memory for buffers");
            goto filt_fail;
        }

        VkMemoryAllocateInfo mai = {0};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = rq_in.size;
        mai.memoryTypeIndex = mt_in;
        r = vkAllocateMemory(yona_vk_dev, &mai, NULL, &mem_in);
        if (r != VK_SUCCESS) goto filt_fail;
        r = vkBindBufferMemory(yona_vk_dev, buf_in, mem_in, 0);
        if (r != VK_SUCCESS) goto filt_fail;

        mai.allocationSize = rq_f.size;
        mai.memoryTypeIndex = mt_f;
        r = vkAllocateMemory(yona_vk_dev, &mai, NULL, &mem_flags);
        if (r != VK_SUCCESS) goto filt_fail;
        r = vkBindBufferMemory(yona_vk_dev, buf_flags, mem_flags, 0);
        if (r != VK_SUCCESS) goto filt_fail;

        mai.allocationSize = rq_p.size;
        mai.memoryTypeIndex = mt_p;
        r = vkAllocateMemory(yona_vk_dev, &mai, NULL, &mem_prefix);
        if (r != VK_SUCCESS) goto filt_fail;
        r = vkBindBufferMemory(yona_vk_dev, buf_prefix, mem_prefix, 0);
        if (r != VK_SUCCESS) goto filt_fail;

        mai.allocationSize = rq_o.size;
        mai.memoryTypeIndex = mt_o;
        r = vkAllocateMemory(yona_vk_dev, &mai, NULL, &mem_out);
        if (r != VK_SUCCESS) goto filt_fail;
        r = vkBindBufferMemory(yona_vk_dev, buf_out, mem_out, 0);
        if (r != VK_SUCCESS) goto filt_fail;

        r = vkMapMemory(yona_vk_dev, mem_in, 0, nbytes_in, 0, &p_in);
        if (r != VK_SUCCESS) goto filt_fail;
        map_mem_in = mem_in;
        r = vkMapMemory(yona_vk_dev, mem_flags, 0, nbytes_flags, 0, &p_flags);
        if (r != VK_SUCCESS) goto filt_fail;
        map_mem_flags = mem_flags;
        r = vkMapMemory(yona_vk_dev, mem_prefix, 0, nbytes_prefix, 0, &p_prefix);
        if (r != VK_SUCCESS) goto filt_fail;
        map_mem_prefix = mem_prefix;
        r = vkMapMemory(yona_vk_dev, mem_out, 0, nbytes_out, 0, &p_out);
        if (r != VK_SUCCESS) goto filt_fail;
        map_mem_out = mem_out;
    } else {
        r = vkMapMemory(yona_vk_dev, mem_in_stg, 0, nbytes_in, 0, &p_in);
        if (r != VK_SUCCESS) goto filt_fail;
        map_mem_in = mem_in_stg;
        r = vkMapMemory(yona_vk_dev, mem_flags_stg, 0, nbytes_flags, 0, &p_flags);
        if (r != VK_SUCCESS) goto filt_fail;
        map_mem_flags = mem_flags_stg;
        r = vkMapMemory(yona_vk_dev, mem_prefix_stg, 0, nbytes_prefix, 0, &p_prefix);
        if (r != VK_SUCCESS) goto filt_fail;
        map_mem_prefix = mem_prefix_stg;
        r = vkMapMemory(yona_vk_dev, mem_out_stg, 0, nbytes_out, 0, &p_out);
        if (r != VK_SUCCESS) goto filt_fail;
        map_mem_out = mem_out_stg;
    }

    if (gpu_prefix) {
        if (!use_staging) {
            VkBufferCreateInfo bcs = {0};
            bcs.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bcs.size = nbytes_prefix;
            bcs.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            bcs.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            r = vkCreateBuffer(yona_vk_dev, &bcs, NULL, &buf_scan0);
            if (r != VK_SUCCESS) goto filt_fail;
            r = vkCreateBuffer(yona_vk_dev, &bcs, NULL, &buf_scan1);
            if (r != VK_SUCCESS) goto filt_fail;
            VkMemoryRequirements rq_sc0, rq_sc1;
            vkGetBufferMemoryRequirements(yona_vk_dev, buf_scan0, &rq_sc0);
            vkGetBufferMemoryRequirements(yona_vk_dev, buf_scan1, &rq_sc1);
            uint32_t mt_sc0 = 0, mt_sc1 = 0;
            const VkMemoryPropertyFlags want_sc =
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            if (yona_vk_pick_memory_type(rq_sc0.memoryTypeBits, want_sc, &mt_sc0) != 0 ||
                yona_vk_pick_memory_type(rq_sc1.memoryTypeBits, want_sc, &mt_sc1) != 0) {
                yona_vk_note_cpy("filter: no host memory for scan buffers");
                goto filt_fail;
            }
            VkMemoryAllocateInfo mas = {0};
            mas.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            mas.allocationSize = rq_sc0.size;
            mas.memoryTypeIndex = mt_sc0;
            r = vkAllocateMemory(yona_vk_dev, &mas, NULL, &mem_scan0);
            if (r != VK_SUCCESS) goto filt_fail;
            r = vkBindBufferMemory(yona_vk_dev, buf_scan0, mem_scan0, 0);
            if (r != VK_SUCCESS) goto filt_fail;
            mas.allocationSize = rq_sc1.size;
            mas.memoryTypeIndex = mt_sc1;
            r = vkAllocateMemory(yona_vk_dev, &mas, NULL, &mem_scan1);
            if (r != VK_SUCCESS) goto filt_fail;
            r = vkBindBufferMemory(yona_vk_dev, buf_scan1, mem_scan1, 0);
            if (r != VK_SUCCESS) goto filt_fail;
            r = vkMapMemory(yona_vk_dev, mem_scan0, 0, nbytes_prefix, 0, &p_scan0);
            if (r != VK_SUCCESS) goto filt_fail;
            map_mem_scan0 = mem_scan0;
            r = vkMapMemory(yona_vk_dev, mem_scan1, 0, nbytes_prefix, 0, &p_scan1);
            if (r != VK_SUCCESS) goto filt_fail;
            map_mem_scan1 = mem_scan1;
        } else {
            r = yona_vk_create_device_local_ssbo(
                vkCreateBuffer, vkDestroyBuffer, vkGetBufferMemoryRequirements, vkAllocateMemory,
                vkFreeMemory, vkBindBufferMemory, nbytes_prefix, &buf_scan0_dev, &mem_scan0_dev);
            if (r != VK_SUCCESS) goto filt_fail;
            r = yona_vk_create_device_local_ssbo(
                vkCreateBuffer, vkDestroyBuffer, vkGetBufferMemoryRequirements, vkAllocateMemory,
                vkFreeMemory, vkBindBufferMemory, nbytes_prefix, &buf_scan1_dev, &mem_scan1_dev);
            if (r != VK_SUCCESS) goto filt_fail;
            VkBufferCreateInfo bcc = {0};
            bcc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bcc.size = 64;
            bcc.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bcc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            r = vkCreateBuffer(yona_vk_dev, &bcc, NULL, &buf_count_stg);
            if (r != VK_SUCCESS) goto filt_fail;
            VkMemoryRequirements rq_c;
            vkGetBufferMemoryRequirements(yona_vk_dev, buf_count_stg, &rq_c);
            uint32_t mt_c = 0;
            if (yona_vk_pick_memory_type(rq_c.memoryTypeBits,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                         &mt_c) != 0) {
                yona_vk_note_cpy("filter: no host memory for count staging");
                goto filt_fail;
            }
            VkMemoryAllocateInfo mac = {0};
            mac.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            mac.allocationSize = rq_c.size;
            mac.memoryTypeIndex = mt_c;
            r = vkAllocateMemory(yona_vk_dev, &mac, NULL, &mem_count_stg);
            if (r != VK_SUCCESS) goto filt_fail;
            r = vkBindBufferMemory(yona_vk_dev, buf_count_stg, mem_count_stg, 0);
            if (r != VK_SUCCESS) goto filt_fail;
            r = vkMapMemory(yona_vk_dev, mem_count_stg, 0, rq_c.size, 0, &p_count);
            if (r != VK_SUCCESS) goto filt_fail;
            map_mem_count = mem_count_stg;
        }
    }

    uint32_t prefix_passes = 0u;
    for (uint32_t sx = 1u; sx < ulen; sx <<= 1u) prefix_passes++;
    const int scan1_has_inclusive = (prefix_passes & 1u) != 0u;
    VkBuffer inc_buf_host = scan1_has_inclusive ? buf_scan1 : buf_scan0;
    VkBuffer inc_buf_dev = scan1_has_inclusive ? buf_scan1_dev : buf_scan0_dev;

    memcpy(p_in, arr + 1, (size_t)nbytes_in);
    memset(p_flags, 0, (size_t)nbytes_flags);
    memset(p_prefix, 0, (size_t)nbytes_prefix);
    memset(p_out, 0, (size_t)nbytes_out);
    if (gpu_prefix && !use_staging) {
        memset(p_scan0, 0, (size_t)nbytes_prefix);
        memset(p_scan1, 0, (size_t)nbytes_prefix);
    }
    if (gpu_prefix && use_staging && p_count) memset(p_count, 0, 64);

    VkDescriptorBufferInfo dbi_m0 = {use_staging ? buf_in_dev : buf_in, 0, nbytes_in};
    VkDescriptorBufferInfo dbi_m1 = {use_staging ? buf_flags_dev : buf_flags, 0, nbytes_flags};
    VkWriteDescriptorSet wm[2] = {0};
    wm[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wm[0].dstSet = dsets[0];
    wm[0].dstBinding = 0;
    wm[0].descriptorCount = 1;
    wm[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    wm[0].pBufferInfo = &dbi_m0;
    wm[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wm[1].dstSet = dsets[0];
    wm[1].dstBinding = 1;
    wm[1].descriptorCount = 1;
    wm[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    wm[1].pBufferInfo = &dbi_m1;
    vkUpdateDescriptorSets(yona_vk_dev, 2, wm, 0, NULL);

    VkDescriptorBufferInfo dbi_s0 = {use_staging ? buf_in_dev : buf_in, 0, nbytes_in};
    VkDescriptorBufferInfo dbi_s1 = {use_staging ? buf_flags_dev : buf_flags, 0, nbytes_flags};
    VkDescriptorBufferInfo dbi_s2 = {use_staging ? buf_prefix_dev : buf_prefix, 0, nbytes_prefix};
    VkDescriptorBufferInfo dbi_s3 = {use_staging ? buf_out_dev : buf_out, 0, nbytes_out};
    VkWriteDescriptorSet ws[4] = {0};
    uint32_t si;
    for (si = 0; si < 4u; si++) {
        ws[si].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[si].dstSet = dsets[1];
        ws[si].dstBinding = si;
        ws[si].descriptorCount = 1;
        ws[si].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    ws[0].pBufferInfo = &dbi_s0;
    ws[1].pBufferInfo = &dbi_s1;
    ws[2].pBufferInfo = &dbi_s2;
    ws[3].pBufferInfo = &dbi_s3;
    vkUpdateDescriptorSets(yona_vk_dev, 4, ws, 0, NULL);

    if (gpu_prefix) {
        VkDescriptorBufferInfo dbi_f0 = {use_staging ? buf_flags_dev : buf_flags, 0,
                                         nbytes_flags};
        VkDescriptorBufferInfo dbi_f1 = {use_staging ? buf_scan0_dev : buf_scan0, 0, nbytes_prefix};
        VkWriteDescriptorSet wf[2] = {0};
        wf[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wf[0].dstSet = dsets[2];
        wf[0].dstBinding = 0;
        wf[0].descriptorCount = 1;
        wf[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        wf[0].pBufferInfo = &dbi_f0;
        wf[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wf[1].dstSet = dsets[2];
        wf[1].dstBinding = 1;
        wf[1].descriptorCount = 1;
        wf[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        wf[1].pBufferInfo = &dbi_f1;
        VkDescriptorBufferInfo dbi_pe0 = {use_staging ? buf_scan0_dev : buf_scan0, 0,
                                          nbytes_prefix};
        VkDescriptorBufferInfo dbi_pe1 = {use_staging ? buf_scan1_dev : buf_scan1, 0,
                                          nbytes_prefix};
        VkWriteDescriptorSet wpe[2] = {0};
        wpe[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wpe[0].dstSet = dsets[3];
        wpe[0].dstBinding = 0;
        wpe[0].descriptorCount = 1;
        wpe[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        wpe[0].pBufferInfo = &dbi_pe0;
        wpe[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wpe[1].dstSet = dsets[3];
        wpe[1].dstBinding = 1;
        wpe[1].descriptorCount = 1;
        wpe[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        wpe[1].pBufferInfo = &dbi_pe1;
        VkDescriptorBufferInfo dbi_po0 = {use_staging ? buf_scan1_dev : buf_scan1, 0,
                                          nbytes_prefix};
        VkDescriptorBufferInfo dbi_po1 = {use_staging ? buf_scan0_dev : buf_scan0, 0,
                                          nbytes_prefix};
        VkWriteDescriptorSet wpo[2] = {0};
        wpo[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wpo[0].dstSet = dsets[4];
        wpo[0].dstBinding = 0;
        wpo[0].descriptorCount = 1;
        wpo[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        wpo[0].pBufferInfo = &dbi_po0;
        wpo[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wpo[1].dstSet = dsets[4];
        wpo[1].dstBinding = 1;
        wpo[1].descriptorCount = 1;
        wpo[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        wpo[1].pBufferInfo = &dbi_po1;
        VkDescriptorBufferInfo dbi_x0 = {use_staging ? inc_buf_dev : inc_buf_host, 0,
                                          nbytes_prefix};
        VkDescriptorBufferInfo dbi_x1 = {use_staging ? buf_flags_dev : buf_flags, 0,
                                         nbytes_flags};
        VkDescriptorBufferInfo dbi_x2 = {use_staging ? buf_prefix_dev : buf_prefix, 0,
                                         nbytes_prefix};
        VkWriteDescriptorSet wx[3] = {0};
        for (uint32_t xi = 0; xi < 3u; xi++) {
            wx[xi].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wx[xi].dstSet = dsets[5];
            wx[xi].dstBinding = xi;
            wx[xi].descriptorCount = 1;
            wx[xi].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        }
        wx[0].pBufferInfo = &dbi_x0;
        wx[1].pBufferInfo = &dbi_x1;
        wx[2].pBufferInfo = &dbi_x2;
        VkWriteDescriptorSet wgpu[9];
        memcpy(wgpu, wf, sizeof wf);
        memcpy(wgpu + 2, wpe, sizeof wpe);
        memcpy(wgpu + 4, wpo, sizeof wpo);
        memcpy(wgpu + 6, wx, sizeof wx);
        vkUpdateDescriptorSets(yona_vk_dev, 9, wgpu, 0, NULL);
    }

    VkCommandPoolCreateInfo cpci0 = {0};
    cpci0.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci0.queueFamilyIndex = yona_vk_queue_family;
    cpci0.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                  VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    r = vkCreateCommandPool(yona_vk_dev, &cpci0, NULL, &cpool);
    if (r != VK_SUCCESS) goto filt_fail;

    VkCommandBufferAllocateInfo cbai = {0};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = cpool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 2;
    r = vkAllocateCommandBuffers(yona_vk_dev, &cbai, cmds);
    if (r != VK_SUCCESS) goto filt_fail;

    VkFenceCreateInfo fci = {0};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    r = vkCreateFence(yona_vk_dev, &fci, NULL, &fence);
    if (r != VK_SUCCESS) goto filt_fail;

    VkCommandBufferBeginInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    r = vkBeginCommandBuffer(cmds[0], &bi);
    if (r != VK_SUCCESS) goto filt_fail;
    if (use_staging) {
        VkBufferCopy c_in = {0};
        c_in.size = nbytes_in;
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmds[0], buf_in_stg, VK_ACCESS_HOST_WRITE_BIT,
                               VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT);
        vkCmdCopyBuffer(cmds[0], buf_in_stg, buf_in_dev, 1, &c_in);
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmds[0], buf_in_dev, VK_ACCESS_TRANSFER_WRITE_BIT,
                               VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmds[0], buf_flags_dev, 0u,
                               VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }
    vkCmdBindPipeline(cmds[0], VK_PIPELINE_BIND_POINT_COMPUTE, mp->pipe);
    vkCmdBindDescriptorSets(cmds[0], VK_PIPELINE_BIND_POINT_COMPUTE, mp->pl, 0, 1, &dsets[0], 0,
                            NULL);
    char pc_mark[16];
    memcpy(pc_mark, &threshold, sizeof(int64_t));
    memcpy(pc_mark + sizeof(int64_t), &ulen, sizeof(uint32_t));
    vkCmdPushConstants(cmds[0], mp->pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 12, pc_mark);
    vkCmdDispatch(cmds[0], (ulen + 63u) / 64u, 1, 1);
    if (gpu_prefix) {
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmds[0],
                               use_staging ? buf_flags_dev : buf_flags, VK_ACCESS_SHADER_WRITE_BIT,
                               VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        vkCmdBindPipeline(cmds[0], VK_PIPELINE_BIND_POINT_COMPUTE, ft->pipe);
        vkCmdBindDescriptorSets(cmds[0], VK_PIPELINE_BIND_POINT_COMPUTE, ft->pl, 0, 1, &dsets[2], 0,
                                NULL);
        vkCmdPushConstants(cmds[0], ft->pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &ulen);
        vkCmdDispatch(cmds[0], (ulen + 63u) / 64u, 1, 1);
        VkBuffer scan0b = use_staging ? buf_scan0_dev : buf_scan0;
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmds[0], scan0b, VK_ACCESS_SHADER_WRITE_BIT,
                               VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        vkCmdBindPipeline(cmds[0], VK_PIPELINE_BIND_POINT_COMPUTE, pp->pipe);
        uint32_t stride = 1u;
        int even_pass = 1;
        while (stride < ulen) {
            VkDescriptorSet pset = even_pass ? dsets[3] : dsets[4];
            vkCmdBindDescriptorSets(cmds[0], VK_PIPELINE_BIND_POINT_COMPUTE, pp->pl, 0, 1, &pset, 0,
                                    NULL);
            char pc_pr[8];
            memcpy(pc_pr, &stride, sizeof(uint32_t));
            memcpy(pc_pr + sizeof(uint32_t), &ulen, sizeof(uint32_t));
            vkCmdPushConstants(cmds[0], pp->pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 8, pc_pr);
            vkCmdDispatch(cmds[0], (ulen + 63u) / 64u, 1, 1);
            VkMemoryBarrier mb_mid = {0};
            mb_mid.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            mb_mid.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            mb_mid.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmds[0], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb_mid, 0, NULL, 0,
                                 NULL);
            stride <<= 1u;
            even_pass = !even_pass;
        }
        VkBuffer incb = use_staging ? inc_buf_dev : inc_buf_host;
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmds[0], incb, VK_ACCESS_SHADER_WRITE_BIT,
                               use_staging ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_HOST_READ_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               use_staging ? VK_PIPELINE_STAGE_TRANSFER_BIT
                                           : VK_PIPELINE_STAGE_HOST_BIT);
        if (use_staging) {
            VkBufferCopy cc = {0};
            cc.srcOffset = (VkDeviceSize)((size_t)(ulen - 1u) * sizeof(int64_t));
            cc.size = sizeof(int64_t);
            vkCmdCopyBuffer(cmds[0], incb, buf_count_stg, 1, &cc);
            yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmds[0], buf_count_stg,
                                   VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT);
        }
    } else if (use_staging) {
        VkBufferCopy c_fl = {0};
        c_fl.size = nbytes_flags;
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmds[0], buf_flags_dev,
                               VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmds[0], buf_flags_stg, VK_ACCESS_TRANSFER_READ_BIT,
                               VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT);
        vkCmdCopyBuffer(cmds[0], buf_flags_dev, buf_flags_stg, 1, &c_fl);
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmds[0], buf_flags_stg,
                               VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT);
    } else {
        VkMemoryBarrier mb0 = {0};
        mb0.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb0.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mb0.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmds[0], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb0, 0, NULL, 0, NULL);
    }
    r = vkEndCommandBuffer(cmds[0]);
    if (r != VK_SUCCESS) goto filt_fail;

    VkSubmitInfo si0 = {0};
    si0.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si0.commandBufferCount = 1;
    si0.pCommandBuffers = &cmds[0];
    r = vkQueueSubmit(yona_vk_queue, 1, &si0, fence);
    if (r != VK_SUCCESS) goto filt_fail;
    r = vkWaitForFences(yona_vk_dev, 1, &fence, VK_TRUE, UINT64_MAX);
    if (r != VK_SUCCESS) goto filt_fail;

    int64_t count = 0;
    if (gpu_prefix) {
        if (use_staging) {
            if (vkInvalidateMappedMemoryRanges) {
                VkMappedMemoryRange invc = {0};
                invc.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
                invc.memory = map_mem_count;
                invc.offset = 0;
                invc.size = sizeof(int64_t);
                vkInvalidateMappedMemoryRanges(yona_vk_dev, 1, &invc);
            }
            count = *(int64_t*)p_count;
        } else {
            void* p_inc = scan1_has_inclusive ? p_scan1 : p_scan0;
            if (vkInvalidateMappedMemoryRanges) {
                VkMappedMemoryRange invi = {0};
                invi.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
                invi.memory = scan1_has_inclusive ? map_mem_scan1 : map_mem_scan0;
                invi.offset = (VkDeviceSize)((size_t)(ulen - 1u) * sizeof(int64_t));
                invi.size = sizeof(int64_t);
                vkInvalidateMappedMemoryRanges(yona_vk_dev, 1, &invi);
            }
            count = ((int64_t*)p_inc)[(size_t)ulen - 1u];
        }
    } else {
        if (vkInvalidateMappedMemoryRanges) {
            VkMappedMemoryRange inv = {0};
            inv.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            inv.memory = map_mem_flags;
            inv.offset = 0;
            inv.size = nbytes_flags;
            vkInvalidateMappedMemoryRanges(yona_vk_dev, 1, &inv);
        }

        int32_t* flags = (int32_t*)p_flags;
        int64_t* pref = (int64_t*)p_prefix;
        int64_t run = 0;
        for (uint32_t i = 0; i < ulen; i++) {
            pref[i] = run;
            run += (flags[i] != 0);
        }
        count = run;

        if (vkFlushMappedMemoryRanges) {
            VkMappedMemoryRange fl[2] = {0};
            fl[0].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            fl[0].memory = map_mem_prefix;
            fl[0].offset = 0;
            fl[0].size = nbytes_prefix;
            fl[1].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            fl[1].memory = map_mem_flags;
            fl[1].offset = 0;
            fl[1].size = nbytes_flags;
            (void)vkFlushMappedMemoryRanges(yona_vk_dev, 2, fl);
        }
    }

    r = vkResetFences(yona_vk_dev, 1, &fence);
    if (r != VK_SUCCESS) goto filt_fail;

    r = vkBeginCommandBuffer(cmds[1], &bi);
    if (r != VK_SUCCESS) goto filt_fail;
    if (gpu_prefix) {
        VkBuffer incb2 = use_staging ? inc_buf_dev : inc_buf_host;
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmds[1], incb2, 0u,
                               VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        vkCmdBindPipeline(cmds[1], VK_PIPELINE_BIND_POINT_COMPUTE, ix->pipe);
        vkCmdBindDescriptorSets(cmds[1], VK_PIPELINE_BIND_POINT_COMPUTE, ix->pl, 0, 1, &dsets[5], 0,
                                NULL);
        vkCmdPushConstants(cmds[1], ix->pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &ulen);
        vkCmdDispatch(cmds[1], (ulen + 63u) / 64u, 1, 1);
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmds[1],
                               use_staging ? buf_prefix_dev : buf_prefix, VK_ACCESS_SHADER_WRITE_BIT,
                               VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    } else if (use_staging) {
        VkBufferCopy c_pr = {0};
        c_pr.size = nbytes_prefix;
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmds[1], buf_prefix_stg,
                               VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                               VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        vkCmdCopyBuffer(cmds[1], buf_prefix_stg, buf_prefix_dev, 1, &c_pr);
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmds[1], buf_prefix_dev,
                               VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    } else {
        VkMemoryBarrier mb1 = {0};
        mb1.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb1.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        mb1.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmds[1], VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &mb1, 0, NULL, 0, NULL);
    }
    vkCmdBindPipeline(cmds[1], VK_PIPELINE_BIND_POINT_COMPUTE, sp->pipe);
    vkCmdBindDescriptorSets(cmds[1], VK_PIPELINE_BIND_POINT_COMPUTE, sp->pl, 0, 1, &dsets[1], 0,
                            NULL);
    vkCmdPushConstants(cmds[1], sp->pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &ulen);
    vkCmdDispatch(cmds[1], (ulen + 63u) / 64u, 1, 1);
    if (use_staging) {
        VkBufferCopy c_ou = {0};
        c_ou.size = nbytes_out;
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmds[1], buf_out_dev, VK_ACCESS_SHADER_WRITE_BIT,
                               VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT);
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmds[1], buf_out_stg, VK_ACCESS_TRANSFER_READ_BIT,
                               VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT);
        vkCmdCopyBuffer(cmds[1], buf_out_dev, buf_out_stg, 1, &c_ou);
        yona_vk_barrier_buffer(vkCmdPipelineBarrier, cmds[1], buf_out_stg,
                               VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT);
    } else {
        VkMemoryBarrier mb2 = {0};
        mb2.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb2.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mb2.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmds[1], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                             0, 1, &mb2, 0, NULL, 0, NULL);
    }
    r = vkEndCommandBuffer(cmds[1]);
    if (r != VK_SUCCESS) goto filt_fail;

    VkSubmitInfo si1 = {0};
    si1.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si1.commandBufferCount = 1;
    si1.pCommandBuffers = &cmds[1];
    r = vkQueueSubmit(yona_vk_queue, 1, &si1, fence);
    if (r != VK_SUCCESS) goto filt_fail;
    r = vkWaitForFences(yona_vk_dev, 1, &fence, VK_TRUE, UINT64_MAX);
    if (r != VK_SUCCESS) goto filt_fail;

    if (vkInvalidateMappedMemoryRanges) {
        VkMappedMemoryRange inv = {0};
        inv.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        inv.memory = map_mem_out;
        inv.offset = 0;
        inv.size = nbytes_out;
        vkInvalidateMappedMemoryRanges(yona_vk_dev, 1, &inv);
    }

    int64_t* result = yona_rt_int_array_alloc(count);
    if (!result) {
        yona_vk_note_cpy("filter: yona_rt_int_array_alloc failed");
        goto filt_fail;
    }
    int64_t* packed = (int64_t*)p_out;
    for (int64_t i = 0; i < count; i++) result[1 + i] = packed[i];

    if (map_mem_in != VK_NULL_HANDLE) vkUnmapMemory(yona_vk_dev, map_mem_in);
    if (map_mem_flags != VK_NULL_HANDLE) vkUnmapMemory(yona_vk_dev, map_mem_flags);
    if (map_mem_prefix != VK_NULL_HANDLE) vkUnmapMemory(yona_vk_dev, map_mem_prefix);
    if (map_mem_out != VK_NULL_HANDLE) vkUnmapMemory(yona_vk_dev, map_mem_out);
    if (map_mem_scan0 != VK_NULL_HANDLE) vkUnmapMemory(yona_vk_dev, map_mem_scan0);
    if (map_mem_scan1 != VK_NULL_HANDLE) vkUnmapMemory(yona_vk_dev, map_mem_scan1);
    if (map_mem_count != VK_NULL_HANDLE) vkUnmapMemory(yona_vk_dev, map_mem_count);
    p_in = p_flags = p_prefix = p_out = NULL;
    p_scan0 = p_scan1 = p_count = NULL;
    map_mem_in = map_mem_flags = map_mem_prefix = map_mem_out = VK_NULL_HANDLE;
    map_mem_scan0 = map_mem_scan1 = map_mem_count = VK_NULL_HANDLE;

    vkDestroyFence(yona_vk_dev, fence, NULL);
    fence = VK_NULL_HANDLE;
    vkFreeCommandBuffers(yona_vk_dev, cpool, 2, cmds);
    cmds[0] = cmds[1] = VK_NULL_HANDLE;
    vkDestroyCommandPool(yona_vk_dev, cpool, NULL);
    cpool = VK_NULL_HANDLE;
    if (use_staging) {
        vkDestroyBuffer(yona_vk_dev, buf_in_stg, NULL);
        vkFreeMemory(yona_vk_dev, mem_in_stg, NULL);
        vkDestroyBuffer(yona_vk_dev, buf_in_dev, NULL);
        vkFreeMemory(yona_vk_dev, mem_in_dev, NULL);
        vkDestroyBuffer(yona_vk_dev, buf_flags_stg, NULL);
        vkFreeMemory(yona_vk_dev, mem_flags_stg, NULL);
        vkDestroyBuffer(yona_vk_dev, buf_flags_dev, NULL);
        vkFreeMemory(yona_vk_dev, mem_flags_dev, NULL);
        vkDestroyBuffer(yona_vk_dev, buf_prefix_stg, NULL);
        vkFreeMemory(yona_vk_dev, mem_prefix_stg, NULL);
        vkDestroyBuffer(yona_vk_dev, buf_prefix_dev, NULL);
        vkFreeMemory(yona_vk_dev, mem_prefix_dev, NULL);
        vkDestroyBuffer(yona_vk_dev, buf_out_stg, NULL);
        vkFreeMemory(yona_vk_dev, mem_out_stg, NULL);
        vkDestroyBuffer(yona_vk_dev, buf_out_dev, NULL);
        vkFreeMemory(yona_vk_dev, mem_out_dev, NULL);
        buf_in_stg = buf_in_dev = VK_NULL_HANDLE;
        mem_in_stg = mem_in_dev = VK_NULL_HANDLE;
        buf_flags_stg = buf_flags_dev = VK_NULL_HANDLE;
        mem_flags_stg = mem_flags_dev = VK_NULL_HANDLE;
        buf_prefix_stg = buf_prefix_dev = VK_NULL_HANDLE;
        mem_prefix_stg = mem_prefix_dev = VK_NULL_HANDLE;
        buf_out_stg = buf_out_dev = VK_NULL_HANDLE;
        mem_out_stg = mem_out_dev = VK_NULL_HANDLE;
        if (buf_scan0_dev != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_scan0_dev, NULL);
        if (mem_scan0_dev != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_scan0_dev, NULL);
        if (buf_scan1_dev != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_scan1_dev, NULL);
        if (mem_scan1_dev != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_scan1_dev, NULL);
        if (buf_count_stg != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_count_stg, NULL);
        if (mem_count_stg != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_count_stg, NULL);
        buf_scan0_dev = buf_scan1_dev = VK_NULL_HANDLE;
        mem_scan0_dev = mem_scan1_dev = VK_NULL_HANDLE;
        buf_count_stg = VK_NULL_HANDLE;
        mem_count_stg = VK_NULL_HANDLE;
    } else {
        vkDestroyBuffer(yona_vk_dev, buf_in, NULL);
        vkDestroyBuffer(yona_vk_dev, buf_flags, NULL);
        vkDestroyBuffer(yona_vk_dev, buf_prefix, NULL);
        vkDestroyBuffer(yona_vk_dev, buf_out, NULL);
        if (buf_scan0 != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_scan0, NULL);
        if (buf_scan1 != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_scan1, NULL);
        if (mem_scan0 != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_scan0, NULL);
        if (mem_scan1 != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_scan1, NULL);
        buf_scan0 = buf_scan1 = VK_NULL_HANDLE;
        mem_scan0 = mem_scan1 = VK_NULL_HANDLE;
        buf_in = buf_flags = buf_prefix = buf_out = VK_NULL_HANDLE;
        vkFreeMemory(yona_vk_dev, mem_in, NULL);
        vkFreeMemory(yona_vk_dev, mem_flags, NULL);
        vkFreeMemory(yona_vk_dev, mem_prefix, NULL);
        vkFreeMemory(yona_vk_dev, mem_out, NULL);
        mem_in = mem_flags = mem_prefix = mem_out = VK_NULL_HANDLE;
    }
    vkDestroyDescriptorPool(yona_vk_dev, dpool, NULL);
    dpool = VK_NULL_HANDLE;

    *out = result;
    yona_vk_compute_submit_unlock();
    return 1;

filt_fail:
    if (!yona_vk_last_note[0]) {
        char b[120];
        snprintf(b, sizeof b, "filter: Vulkan failure VkResult=%d", (int)r);
        yona_vk_note_cpy(b);
    }
    if (map_mem_in != VK_NULL_HANDLE) vkUnmapMemory(yona_vk_dev, map_mem_in);
    if (map_mem_flags != VK_NULL_HANDLE) vkUnmapMemory(yona_vk_dev, map_mem_flags);
    if (map_mem_prefix != VK_NULL_HANDLE) vkUnmapMemory(yona_vk_dev, map_mem_prefix);
    if (map_mem_out != VK_NULL_HANDLE) vkUnmapMemory(yona_vk_dev, map_mem_out);
    if (map_mem_scan0 != VK_NULL_HANDLE) vkUnmapMemory(yona_vk_dev, map_mem_scan0);
    if (map_mem_scan1 != VK_NULL_HANDLE) vkUnmapMemory(yona_vk_dev, map_mem_scan1);
    if (map_mem_count != VK_NULL_HANDLE) vkUnmapMemory(yona_vk_dev, map_mem_count);
    if (fence != VK_NULL_HANDLE) vkDestroyFence(yona_vk_dev, fence, NULL);
    if (cmds[0] != VK_NULL_HANDLE && cpool != VK_NULL_HANDLE)
        vkFreeCommandBuffers(yona_vk_dev, cpool, 2, cmds);
    if (cpool != VK_NULL_HANDLE) vkDestroyCommandPool(yona_vk_dev, cpool, NULL);
    if (use_staging) {
        if (buf_in_stg != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_in_stg, NULL);
        if (mem_in_stg != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_in_stg, NULL);
        if (buf_in_dev != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_in_dev, NULL);
        if (mem_in_dev != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_in_dev, NULL);
        if (buf_flags_stg != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_flags_stg, NULL);
        if (mem_flags_stg != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_flags_stg, NULL);
        if (buf_flags_dev != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_flags_dev, NULL);
        if (mem_flags_dev != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_flags_dev, NULL);
        if (buf_prefix_stg != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_prefix_stg, NULL);
        if (mem_prefix_stg != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_prefix_stg, NULL);
        if (buf_prefix_dev != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_prefix_dev, NULL);
        if (mem_prefix_dev != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_prefix_dev, NULL);
        if (buf_out_stg != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_out_stg, NULL);
        if (mem_out_stg != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_out_stg, NULL);
        if (buf_out_dev != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_out_dev, NULL);
        if (mem_out_dev != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_out_dev, NULL);
        if (buf_scan0_dev != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_scan0_dev, NULL);
        if (mem_scan0_dev != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_scan0_dev, NULL);
        if (buf_scan1_dev != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_scan1_dev, NULL);
        if (mem_scan1_dev != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_scan1_dev, NULL);
        if (buf_count_stg != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_count_stg, NULL);
        if (mem_count_stg != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_count_stg, NULL);
    } else {
        if (buf_in != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_in, NULL);
        if (buf_flags != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_flags, NULL);
        if (buf_prefix != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_prefix, NULL);
        if (buf_out != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_out, NULL);
        if (buf_scan0 != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_scan0, NULL);
        if (buf_scan1 != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_scan1, NULL);
        if (mem_in != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_in, NULL);
        if (mem_flags != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_flags, NULL);
        if (mem_prefix != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_prefix, NULL);
        if (mem_out != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_out, NULL);
        if (mem_scan0 != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_scan0, NULL);
        if (mem_scan1 != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_scan1, NULL);
    }
    if (dpool != VK_NULL_HANDLE) vkDestroyDescriptorPool(yona_vk_dev, dpool, NULL);
    yona_vk_compute_submit_unlock();
    return 0;
}

#endif /* YONA_GPU_VULKAN_ENABLED */
