/* Vulkan compute: mapAdd, mapMul, reduceSum — included from gpu_vulkan_device.c */

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

#else /* YONA_GPU_VULKAN_ENABLED */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

typedef struct YonaVkSimplePipe YonaVkSimplePipe;
typedef struct YonaVkReducePipe YonaVkReducePipe;

extern int64_t* yona_rt_int_array_alloc(int64_t count);

#define YONA_VK_DPA(name) ((PFN_##name)(void*)yona_pfn_vkGetDeviceProcAddr(yona_vk_dev, #name))

VkResult yona_vk_compute_ensure_mapadd_pipe(void);
VkResult yona_vk_compute_ensure_mapmul_pipe(void);
VkResult yona_vk_compute_ensure_reduce_pipe(void);
YonaVkSimplePipe* yona_vk_compute_mapadd_pipe(void);
YonaVkSimplePipe* yona_vk_compute_mapmul_pipe(void);
YonaVkReducePipe* yona_vk_compute_reduce_pipe(void);
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

    VkDescriptorPool dpool = VK_NULL_HANDLE;
    VkDescriptorSet dset = VK_NULL_HANDLE;
    VkBuffer buf_in = VK_NULL_HANDLE;
    VkBuffer buf_sums = VK_NULL_HANDLE;
    VkDeviceMemory mem_in = VK_NULL_HANDLE;
    VkDeviceMemory mem_sums = VK_NULL_HANDLE;
    VkCommandPool cpool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    void* mapped_in = NULL;
    void* mapped_sums = NULL;

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
    r = vkMapMemory(yona_vk_dev, mem_sums, 0, nbytes_sums, 0, &mapped_sums);
    if (r != VK_SUCCESS) goto reduce_fail;
    memcpy(mapped_in, arr + 1, (size_t)nbytes_in);
    memset(mapped_sums, 0, (size_t)nbytes_sums);

    VkDescriptorBufferInfo dbi[2] = {0};
    dbi[0].buffer = buf_in;
    dbi[0].offset = 0;
    dbi[0].range = nbytes_in;
    dbi[1].buffer = buf_sums;
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

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rp->pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rp->pl, 0, 1, &dset, 0, NULL);
    vkCmdPushConstants(cmd, rp->pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &ulen);
    vkCmdDispatch(cmd, groups, 1, 1);

    VkMemoryBarrier mb = {0};
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                         1, &mb, 0, NULL, 0, NULL);

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

    int64_t total = 0;
    int64_t* lanes = (int64_t*)mapped_sums;
    for (uint32_t i = 0; i < groups; i++) total += lanes[i];
    *out_sum = total;

    vkUnmapMemory(yona_vk_dev, mem_in);
    vkUnmapMemory(yona_vk_dev, mem_sums);
    mapped_in = NULL;
    mapped_sums = NULL;

    vkDestroyFence(yona_vk_dev, fence, NULL);
    fence = VK_NULL_HANDLE;
    vkFreeCommandBuffers(yona_vk_dev, cpool, 1, &cmd);
    cmd = VK_NULL_HANDLE;
    vkDestroyCommandPool(yona_vk_dev, cpool, NULL);
    cpool = VK_NULL_HANDLE;
    vkDestroyBuffer(yona_vk_dev, buf_in, NULL);
    vkDestroyBuffer(yona_vk_dev, buf_sums, NULL);
    buf_in = VK_NULL_HANDLE;
    buf_sums = VK_NULL_HANDLE;
    vkFreeMemory(yona_vk_dev, mem_in, NULL);
    vkFreeMemory(yona_vk_dev, mem_sums, NULL);
    mem_in = VK_NULL_HANDLE;
    mem_sums = VK_NULL_HANDLE;
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
    if (mapped_in) vkUnmapMemory(yona_vk_dev, mem_in);
    if (mapped_sums) vkUnmapMemory(yona_vk_dev, mem_sums);
    if (fence != VK_NULL_HANDLE) vkDestroyFence(yona_vk_dev, fence, NULL);
    if (cmd != VK_NULL_HANDLE && cpool != VK_NULL_HANDLE)
        vkFreeCommandBuffers(yona_vk_dev, cpool, 1, &cmd);
    if (cpool != VK_NULL_HANDLE) vkDestroyCommandPool(yona_vk_dev, cpool, NULL);
    if (buf_in != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_in, NULL);
    if (buf_sums != VK_NULL_HANDLE) vkDestroyBuffer(yona_vk_dev, buf_sums, NULL);
    if (mem_in != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_in, NULL);
    if (mem_sums != VK_NULL_HANDLE) vkFreeMemory(yona_vk_dev, mem_sums, NULL);
    if (dpool != VK_NULL_HANDLE) vkDestroyDescriptorPool(yona_vk_dev, dpool, NULL);
    yona_vk_compute_submit_unlock();
    return 0;
}

#endif /* YONA_GPU_VULKAN_ENABLED */
