/* Cached compute pipelines (map/mul/reduce/filter) + submit serialization — included from gpu_vulkan_device.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "gpu/map_add_int64_spv.inc"
#include "gpu/map_mul_int64_spv.inc"
#include "gpu/map_square_int64_spv.inc"
#include "gpu/reduce_block_int64_spv.inc"
#include "gpu/map_add_int32_spv.inc"
#include "gpu/map_mul_int32_spv.inc"
#include "gpu/map_square_int32_spv.inc"
#include "gpu/reduce_block_int32_spv.inc"
#include "gpu/filter_mark_int64_spv.inc"
#include "gpu/filter_mark_lt_int64_spv.inc"
#include "gpu/filter_scatter_int64_spv.inc"
#include "gpu/filter_flags_to_i64_spv.inc"
#include "gpu/filter_prefix_inclusive_step_spv.inc"
#include "gpu/filter_inclusive_to_exclusive_spv.inc"
#include "gpu/filter_mark_int32_spv.inc"
#include "gpu/filter_mark_lt_int32_spv.inc"
#include "gpu/filter_scatter_int32_spv.inc"
#include "gpu/filter_flags_to_int32_spv.inc"
#include "gpu/filter_prefix_inclusive_step_int32_spv.inc"
#include "gpu/filter_inclusive_to_exclusive_int32_spv.inc"

#if !defined(_WIN32)
#include <pthread.h>
#endif

#if defined(_WIN32)
static CRITICAL_SECTION yona_vk_submit_cs;
static volatile LONG yona_vk_submit_cs_ready;

static void yona_vk_submit_cs_ensure(void) {
    if (InterlockedCompareExchange(&yona_vk_submit_cs_ready, 1, 0) == 0)
        InitializeCriticalSection(&yona_vk_submit_cs);
}

void yona_vk_compute_submit_lock(void) {
    yona_vk_submit_cs_ensure();
    EnterCriticalSection(&yona_vk_submit_cs);
}

void yona_vk_compute_submit_unlock(void) { LeaveCriticalSection(&yona_vk_submit_cs); }

#else

static pthread_mutex_t yona_vk_submit_mutex = PTHREAD_MUTEX_INITIALIZER;

void yona_vk_compute_submit_lock(void) { (void)pthread_mutex_lock(&yona_vk_submit_mutex); }

void yona_vk_compute_submit_unlock(void) { (void)pthread_mutex_unlock(&yona_vk_submit_mutex); }

#endif

#define YONA_VK_DPA(name) ((PFN_##name)(void*)yona_pfn_vkGetDeviceProcAddr(yona_vk_dev, #name))

typedef struct YonaVkSimplePipe {
    VkShaderModule sm;
    VkDescriptorSetLayout dsl;
    VkPipelineLayout pl;
    VkPipeline pipe;
    int ready;
} YonaVkSimplePipe;

typedef struct YonaVkReducePipe {
    VkShaderModule sm;
    VkDescriptorSetLayout dsl;
    VkPipelineLayout pl;
    VkPipeline pipe;
    int ready;
} YonaVkReducePipe;

typedef struct YonaVkScatterPipe {
    VkShaderModule sm;
    VkDescriptorSetLayout dsl;
    VkPipelineLayout pl;
    VkPipeline pipe;
    int ready;
} YonaVkScatterPipe;

static YonaVkSimplePipe g_yona_pipe_mapadd;
static YonaVkSimplePipe g_yona_pipe_mapmul;
static YonaVkSimplePipe g_yona_pipe_mapsquare;
static YonaVkSimplePipe g_yona_pipe_mapadd_i32;
static YonaVkSimplePipe g_yona_pipe_mapmul_i32;
static YonaVkSimplePipe g_yona_pipe_mapsquare_i32;
static YonaVkReducePipe g_yona_pipe_reduce;
static YonaVkReducePipe g_yona_pipe_reduce_i32;
static YonaVkReducePipe g_yona_pipe_filter_mark;
static YonaVkReducePipe g_yona_pipe_filter_mark_lt;
static YonaVkScatterPipe g_yona_pipe_filter_scatter;
static YonaVkReducePipe g_yona_pipe_filter_flags_to_i64;
static YonaVkReducePipe g_yona_pipe_filter_prefix;
static YonaVkScatterPipe g_yona_pipe_filter_inc_to_exc;
static YonaVkReducePipe g_yona_pipe_filter_mark_i32;
static YonaVkReducePipe g_yona_pipe_filter_mark_lt_i32;
static YonaVkScatterPipe g_yona_pipe_filter_scatter_i32;
static YonaVkReducePipe g_yona_pipe_filter_flags_to_i32;
static YonaVkReducePipe g_yona_pipe_filter_prefix_i32;
static YonaVkScatterPipe g_yona_pipe_filter_inc_to_exc_i32;

static void yona_vk_destroy_simple_pipe(YonaVkSimplePipe* p) {
    PFN_vkDestroyPipeline vkDestroyPipeline = YONA_VK_DPA(vkDestroyPipeline);
    PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout = YONA_VK_DPA(vkDestroyPipelineLayout);
    PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout =
        YONA_VK_DPA(vkDestroyDescriptorSetLayout);
    PFN_vkDestroyShaderModule vkDestroyShaderModule = YONA_VK_DPA(vkDestroyShaderModule);
    if (!vkDestroyPipeline || !vkDestroyPipelineLayout || !vkDestroyDescriptorSetLayout ||
        !vkDestroyShaderModule)
        goto clear;
    if (p->pipe != VK_NULL_HANDLE) vkDestroyPipeline(yona_vk_dev, p->pipe, NULL);
    if (p->pl != VK_NULL_HANDLE) vkDestroyPipelineLayout(yona_vk_dev, p->pl, NULL);
    if (p->dsl != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(yona_vk_dev, p->dsl, NULL);
    if (p->sm != VK_NULL_HANDLE) vkDestroyShaderModule(yona_vk_dev, p->sm, NULL);
clear:
    p->pipe = VK_NULL_HANDLE;
    p->pl = VK_NULL_HANDLE;
    p->dsl = VK_NULL_HANDLE;
    p->sm = VK_NULL_HANDLE;
    p->ready = 0;
}

void yona_vk_compute_destroy_cached_pipelines(void) {
    if (yona_vk_dev == VK_NULL_HANDLE) {
        memset(&g_yona_pipe_mapadd, 0, sizeof g_yona_pipe_mapadd);
        memset(&g_yona_pipe_mapmul, 0, sizeof g_yona_pipe_mapmul);
        memset(&g_yona_pipe_mapsquare, 0, sizeof g_yona_pipe_mapsquare);
        memset(&g_yona_pipe_mapadd_i32, 0, sizeof g_yona_pipe_mapadd_i32);
        memset(&g_yona_pipe_mapmul_i32, 0, sizeof g_yona_pipe_mapmul_i32);
        memset(&g_yona_pipe_mapsquare_i32, 0, sizeof g_yona_pipe_mapsquare_i32);
        memset(&g_yona_pipe_reduce, 0, sizeof g_yona_pipe_reduce);
        memset(&g_yona_pipe_reduce_i32, 0, sizeof g_yona_pipe_reduce_i32);
        memset(&g_yona_pipe_filter_mark, 0, sizeof g_yona_pipe_filter_mark);
        memset(&g_yona_pipe_filter_mark_lt, 0, sizeof g_yona_pipe_filter_mark_lt);
        memset(&g_yona_pipe_filter_scatter, 0, sizeof g_yona_pipe_filter_scatter);
        memset(&g_yona_pipe_filter_flags_to_i64, 0, sizeof g_yona_pipe_filter_flags_to_i64);
        memset(&g_yona_pipe_filter_prefix, 0, sizeof g_yona_pipe_filter_prefix);
        memset(&g_yona_pipe_filter_inc_to_exc, 0, sizeof g_yona_pipe_filter_inc_to_exc);
        memset(&g_yona_pipe_filter_mark_i32, 0, sizeof g_yona_pipe_filter_mark_i32);
        memset(&g_yona_pipe_filter_mark_lt_i32, 0, sizeof g_yona_pipe_filter_mark_lt_i32);
        memset(&g_yona_pipe_filter_scatter_i32, 0, sizeof g_yona_pipe_filter_scatter_i32);
        memset(&g_yona_pipe_filter_flags_to_i32, 0, sizeof g_yona_pipe_filter_flags_to_i32);
        memset(&g_yona_pipe_filter_prefix_i32, 0, sizeof g_yona_pipe_filter_prefix_i32);
        memset(&g_yona_pipe_filter_inc_to_exc_i32, 0, sizeof g_yona_pipe_filter_inc_to_exc_i32);
        return;
    }
    yona_vk_destroy_simple_pipe(&g_yona_pipe_mapadd);
    yona_vk_destroy_simple_pipe(&g_yona_pipe_mapmul);
    yona_vk_destroy_simple_pipe(&g_yona_pipe_mapsquare);
    yona_vk_destroy_simple_pipe(&g_yona_pipe_mapadd_i32);
    yona_vk_destroy_simple_pipe(&g_yona_pipe_mapmul_i32);
    yona_vk_destroy_simple_pipe(&g_yona_pipe_mapsquare_i32);
    yona_vk_destroy_simple_pipe((YonaVkSimplePipe*)&g_yona_pipe_reduce);
    yona_vk_destroy_simple_pipe((YonaVkSimplePipe*)&g_yona_pipe_reduce_i32);
    yona_vk_destroy_simple_pipe((YonaVkSimplePipe*)&g_yona_pipe_filter_mark);
    yona_vk_destroy_simple_pipe((YonaVkSimplePipe*)&g_yona_pipe_filter_mark_lt);
    yona_vk_destroy_simple_pipe((YonaVkSimplePipe*)&g_yona_pipe_filter_scatter);
    yona_vk_destroy_simple_pipe((YonaVkSimplePipe*)&g_yona_pipe_filter_flags_to_i64);
    yona_vk_destroy_simple_pipe((YonaVkSimplePipe*)&g_yona_pipe_filter_prefix);
    yona_vk_destroy_simple_pipe((YonaVkSimplePipe*)&g_yona_pipe_filter_inc_to_exc);
    yona_vk_destroy_simple_pipe((YonaVkSimplePipe*)&g_yona_pipe_filter_mark_i32);
    yona_vk_destroy_simple_pipe((YonaVkSimplePipe*)&g_yona_pipe_filter_mark_lt_i32);
    yona_vk_destroy_simple_pipe((YonaVkSimplePipe*)&g_yona_pipe_filter_scatter_i32);
    yona_vk_destroy_simple_pipe((YonaVkSimplePipe*)&g_yona_pipe_filter_flags_to_i32);
    yona_vk_destroy_simple_pipe((YonaVkSimplePipe*)&g_yona_pipe_filter_prefix_i32);
    yona_vk_destroy_simple_pipe((YonaVkSimplePipe*)&g_yona_pipe_filter_inc_to_exc_i32);
}

static VkResult yona_vk_build_simple_compute_pipe(const uint32_t* spirv, uint32_t spirv_words,
                                                  uint32_t push_bytes, YonaVkSimplePipe* out) {
    memset(out, 0, sizeof(*out));
    PFN_vkCreateShaderModule vkCreateShaderModule = YONA_VK_DPA(vkCreateShaderModule);
    PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout =
        YONA_VK_DPA(vkCreateDescriptorSetLayout);
    PFN_vkCreatePipelineLayout vkCreatePipelineLayout = YONA_VK_DPA(vkCreatePipelineLayout);
    PFN_vkCreateComputePipelines vkCreateComputePipelines = YONA_VK_DPA(vkCreateComputePipelines);
    if (!vkCreateShaderModule || !vkCreateDescriptorSetLayout || !vkCreatePipelineLayout ||
        !vkCreateComputePipelines)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkShaderModuleCreateInfo smci = {0};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = (size_t)spirv_words * sizeof(uint32_t);
    smci.pCode = spirv;
    VkResult r = vkCreateShaderModule(yona_vk_dev, &smci, NULL, &out->sm);
    if (r != VK_SUCCESS) return r;

    VkDescriptorSetLayoutBinding bind = {0};
    bind.binding = 0;
    bind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bind.descriptorCount = 1;
    bind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dslci = {0};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 1;
    dslci.pBindings = &bind;
    r = vkCreateDescriptorSetLayout(yona_vk_dev, &dslci, NULL, &out->dsl);
    if (r != VK_SUCCESS) goto fail_sm;

    VkPushConstantRange pcr = {0};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset = 0;
    pcr.size = push_bytes;

    VkPipelineLayoutCreateInfo plci = {0};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &out->dsl;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    r = vkCreatePipelineLayout(yona_vk_dev, &plci, NULL, &out->pl);
    if (r != VK_SUCCESS) goto fail_dsl;

    VkPipelineShaderStageCreateInfo stage = {0};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = out->sm;
    stage.pName = "main";

    VkComputePipelineCreateInfo cpci = {0};
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage = stage;
    cpci.layout = out->pl;
    r = vkCreateComputePipelines(yona_vk_dev, VK_NULL_HANDLE, 1, &cpci, NULL, &out->pipe);
    if (r != VK_SUCCESS) goto fail_pl;
    return VK_SUCCESS;

fail_pl: {
    PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout = YONA_VK_DPA(vkDestroyPipelineLayout);
    if (vkDestroyPipelineLayout) vkDestroyPipelineLayout(yona_vk_dev, out->pl, NULL);
    out->pl = VK_NULL_HANDLE;
}
fail_dsl: {
    PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout =
        YONA_VK_DPA(vkDestroyDescriptorSetLayout);
    if (vkDestroyDescriptorSetLayout) vkDestroyDescriptorSetLayout(yona_vk_dev, out->dsl, NULL);
    out->dsl = VK_NULL_HANDLE;
}
fail_sm: {
    PFN_vkDestroyShaderModule vkDestroyShaderModule = YONA_VK_DPA(vkDestroyShaderModule);
    if (vkDestroyShaderModule) vkDestroyShaderModule(yona_vk_dev, out->sm, NULL);
    out->sm = VK_NULL_HANDLE;
}
    return r;
}

static VkResult yona_vk_build_two_ssbo_compute_pipe(const uint32_t* spirv, uint32_t spirv_words,
                                                    uint32_t push_bytes, YonaVkReducePipe* out) {
    memset(out, 0, sizeof(*out));
    PFN_vkCreateShaderModule vkCreateShaderModule = YONA_VK_DPA(vkCreateShaderModule);
    PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout =
        YONA_VK_DPA(vkCreateDescriptorSetLayout);
    PFN_vkCreatePipelineLayout vkCreatePipelineLayout = YONA_VK_DPA(vkCreatePipelineLayout);
    PFN_vkCreateComputePipelines vkCreateComputePipelines = YONA_VK_DPA(vkCreateComputePipelines);
    if (!vkCreateShaderModule || !vkCreateDescriptorSetLayout || !vkCreatePipelineLayout ||
        !vkCreateComputePipelines)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkShaderModuleCreateInfo smci = {0};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = (size_t)spirv_words * sizeof(uint32_t);
    smci.pCode = spirv;
    VkResult r = vkCreateShaderModule(yona_vk_dev, &smci, NULL, &out->sm);
    if (r != VK_SUCCESS) return r;

    VkDescriptorSetLayoutBinding binds[2] = {0};
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
    r = vkCreateDescriptorSetLayout(yona_vk_dev, &dslci, NULL, &out->dsl);
    if (r != VK_SUCCESS) goto fail_sm;

    VkPushConstantRange pcr = {0};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset = 0;
    pcr.size = push_bytes;

    VkPipelineLayoutCreateInfo plci = {0};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &out->dsl;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    r = vkCreatePipelineLayout(yona_vk_dev, &plci, NULL, &out->pl);
    if (r != VK_SUCCESS) goto fail_dsl;

    VkPipelineShaderStageCreateInfo stage = {0};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = out->sm;
    stage.pName = "main";

    VkComputePipelineCreateInfo cpci = {0};
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage = stage;
    cpci.layout = out->pl;
    r = vkCreateComputePipelines(yona_vk_dev, VK_NULL_HANDLE, 1, &cpci, NULL, &out->pipe);
    if (r != VK_SUCCESS) goto fail_pl;
    return VK_SUCCESS;

fail_pl: {
    PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout = YONA_VK_DPA(vkDestroyPipelineLayout);
    if (vkDestroyPipelineLayout) vkDestroyPipelineLayout(yona_vk_dev, out->pl, NULL);
    out->pl = VK_NULL_HANDLE;
}
fail_dsl: {
    PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout =
        YONA_VK_DPA(vkDestroyDescriptorSetLayout);
    if (vkDestroyDescriptorSetLayout) vkDestroyDescriptorSetLayout(yona_vk_dev, out->dsl, NULL);
    out->dsl = VK_NULL_HANDLE;
}
fail_sm: {
    PFN_vkDestroyShaderModule vkDestroyShaderModule = YONA_VK_DPA(vkDestroyShaderModule);
    if (vkDestroyShaderModule) vkDestroyShaderModule(yona_vk_dev, out->sm, NULL);
    out->sm = VK_NULL_HANDLE;
}
    return r;
}

static VkResult yona_vk_build_four_ssbo_compute_pipe(const uint32_t* spirv, uint32_t spirv_words,
                                                     uint32_t push_bytes, YonaVkScatterPipe* out) {
    memset(out, 0, sizeof(*out));
    PFN_vkCreateShaderModule vkCreateShaderModule = YONA_VK_DPA(vkCreateShaderModule);
    PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout =
        YONA_VK_DPA(vkCreateDescriptorSetLayout);
    PFN_vkCreatePipelineLayout vkCreatePipelineLayout = YONA_VK_DPA(vkCreatePipelineLayout);
    PFN_vkCreateComputePipelines vkCreateComputePipelines = YONA_VK_DPA(vkCreateComputePipelines);
    if (!vkCreateShaderModule || !vkCreateDescriptorSetLayout || !vkCreatePipelineLayout ||
        !vkCreateComputePipelines)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkShaderModuleCreateInfo smci = {0};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = (size_t)spirv_words * sizeof(uint32_t);
    smci.pCode = spirv;
    VkResult r = vkCreateShaderModule(yona_vk_dev, &smci, NULL, &out->sm);
    if (r != VK_SUCCESS) return r;

    VkDescriptorSetLayoutBinding binds[4] = {0};
    for (uint32_t b = 0; b < 4u; b++) {
        binds[b].binding = b;
        binds[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binds[b].descriptorCount = 1;
        binds[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo dslci = {0};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 4;
    dslci.pBindings = binds;
    r = vkCreateDescriptorSetLayout(yona_vk_dev, &dslci, NULL, &out->dsl);
    if (r != VK_SUCCESS) goto fail_sm;

    VkPushConstantRange pcr = {0};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset = 0;
    pcr.size = push_bytes;

    VkPipelineLayoutCreateInfo plci = {0};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &out->dsl;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    r = vkCreatePipelineLayout(yona_vk_dev, &plci, NULL, &out->pl);
    if (r != VK_SUCCESS) goto fail_dsl;

    VkPipelineShaderStageCreateInfo stage = {0};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = out->sm;
    stage.pName = "main";

    VkComputePipelineCreateInfo cpci = {0};
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage = stage;
    cpci.layout = out->pl;
    r = vkCreateComputePipelines(yona_vk_dev, VK_NULL_HANDLE, 1, &cpci, NULL, &out->pipe);
    if (r != VK_SUCCESS) goto fail_pl;
    return VK_SUCCESS;

fail_pl: {
    PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout = YONA_VK_DPA(vkDestroyPipelineLayout);
    if (vkDestroyPipelineLayout) vkDestroyPipelineLayout(yona_vk_dev, out->pl, NULL);
    out->pl = VK_NULL_HANDLE;
}
fail_dsl: {
    PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout =
        YONA_VK_DPA(vkDestroyDescriptorSetLayout);
    if (vkDestroyDescriptorSetLayout) vkDestroyDescriptorSetLayout(yona_vk_dev, out->dsl, NULL);
    out->dsl = VK_NULL_HANDLE;
}
fail_sm: {
    PFN_vkDestroyShaderModule vkDestroyShaderModule = YONA_VK_DPA(vkDestroyShaderModule);
    if (vkDestroyShaderModule) vkDestroyShaderModule(yona_vk_dev, out->sm, NULL);
    out->sm = VK_NULL_HANDLE;
}
    return r;
}

static VkResult yona_vk_build_three_ssbo_compute_pipe(const uint32_t* spirv, uint32_t spirv_words,
                                                      uint32_t push_bytes, YonaVkScatterPipe* out) {
    memset(out, 0, sizeof(*out));
    PFN_vkCreateShaderModule vkCreateShaderModule = YONA_VK_DPA(vkCreateShaderModule);
    PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout =
        YONA_VK_DPA(vkCreateDescriptorSetLayout);
    PFN_vkCreatePipelineLayout vkCreatePipelineLayout = YONA_VK_DPA(vkCreatePipelineLayout);
    PFN_vkCreateComputePipelines vkCreateComputePipelines = YONA_VK_DPA(vkCreateComputePipelines);
    if (!vkCreateShaderModule || !vkCreateDescriptorSetLayout || !vkCreatePipelineLayout ||
        !vkCreateComputePipelines)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkShaderModuleCreateInfo smci = {0};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = (size_t)spirv_words * sizeof(uint32_t);
    smci.pCode = spirv;
    VkResult r = vkCreateShaderModule(yona_vk_dev, &smci, NULL, &out->sm);
    if (r != VK_SUCCESS) return r;

    VkDescriptorSetLayoutBinding binds[3] = {0};
    for (uint32_t b = 0; b < 3u; b++) {
        binds[b].binding = b;
        binds[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binds[b].descriptorCount = 1;
        binds[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo dslci = {0};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 3;
    dslci.pBindings = binds;
    r = vkCreateDescriptorSetLayout(yona_vk_dev, &dslci, NULL, &out->dsl);
    if (r != VK_SUCCESS) goto fail_sm;

    VkPushConstantRange pcr = {0};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset = 0;
    pcr.size = push_bytes;

    VkPipelineLayoutCreateInfo plci = {0};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &out->dsl;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    r = vkCreatePipelineLayout(yona_vk_dev, &plci, NULL, &out->pl);
    if (r != VK_SUCCESS) goto fail_dsl;

    VkPipelineShaderStageCreateInfo stage = {0};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = out->sm;
    stage.pName = "main";

    VkComputePipelineCreateInfo cpci = {0};
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage = stage;
    cpci.layout = out->pl;
    r = vkCreateComputePipelines(yona_vk_dev, VK_NULL_HANDLE, 1, &cpci, NULL, &out->pipe);
    if (r != VK_SUCCESS) goto fail_pl;
    return VK_SUCCESS;

fail_pl: {
    PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout = YONA_VK_DPA(vkDestroyPipelineLayout);
    if (vkDestroyPipelineLayout) vkDestroyPipelineLayout(yona_vk_dev, out->pl, NULL);
    out->pl = VK_NULL_HANDLE;
}
fail_dsl: {
    PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout =
        YONA_VK_DPA(vkDestroyDescriptorSetLayout);
    if (vkDestroyDescriptorSetLayout) vkDestroyDescriptorSetLayout(yona_vk_dev, out->dsl, NULL);
    out->dsl = VK_NULL_HANDLE;
}
fail_sm: {
    PFN_vkDestroyShaderModule vkDestroyShaderModule = YONA_VK_DPA(vkDestroyShaderModule);
    if (vkDestroyShaderModule) vkDestroyShaderModule(yona_vk_dev, out->sm, NULL);
    out->sm = VK_NULL_HANDLE;
}
    return r;
}

VkResult yona_vk_compute_ensure_mapadd_pipe(void) {
    if (g_yona_pipe_mapadd.ready) return VK_SUCCESS;
    VkResult r = yona_vk_build_simple_compute_pipe(
        kYonaGpuMapAddInt64Spv, kYonaGpuMapAddInt64SpvWordCount, 12u, &g_yona_pipe_mapadd);
    if (r == VK_SUCCESS) g_yona_pipe_mapadd.ready = 1;
    return r;
}

VkResult yona_vk_compute_ensure_mapadd_i32_pipe(void) {
    if (g_yona_pipe_mapadd_i32.ready) return VK_SUCCESS;
    VkResult r = yona_vk_build_simple_compute_pipe(
        kYonaGpuMapAddInt32Spv, kYonaGpuMapAddInt32SpvWordCount, 8u, &g_yona_pipe_mapadd_i32);
    if (r == VK_SUCCESS) g_yona_pipe_mapadd_i32.ready = 1;
    return r;
}

VkResult yona_vk_compute_ensure_mapmul_pipe(void) {
    if (g_yona_pipe_mapmul.ready) return VK_SUCCESS;
    VkResult r = yona_vk_build_simple_compute_pipe(
        kYonaGpuMapMulInt64Spv, kYonaGpuMapMulInt64SpvWordCount, 12u, &g_yona_pipe_mapmul);
    if (r == VK_SUCCESS) g_yona_pipe_mapmul.ready = 1;
    return r;
}

VkResult yona_vk_compute_ensure_mapmul_i32_pipe(void) {
    if (g_yona_pipe_mapmul_i32.ready) return VK_SUCCESS;
    VkResult r = yona_vk_build_simple_compute_pipe(
        kYonaGpuMapMulInt32Spv, kYonaGpuMapMulInt32SpvWordCount, 8u, &g_yona_pipe_mapmul_i32);
    if (r == VK_SUCCESS) g_yona_pipe_mapmul_i32.ready = 1;
    return r;
}

VkResult yona_vk_compute_ensure_mapsquare_pipe(void) {
    if (g_yona_pipe_mapsquare.ready) return VK_SUCCESS;
    VkResult r = yona_vk_build_simple_compute_pipe(
        kYonaGpuMapSquareInt64Spv, kYonaGpuMapSquareInt64SpvWordCount, 12u, &g_yona_pipe_mapsquare);
    if (r == VK_SUCCESS) g_yona_pipe_mapsquare.ready = 1;
    return r;
}

VkResult yona_vk_compute_ensure_mapsquare_i32_pipe(void) {
    if (g_yona_pipe_mapsquare_i32.ready) return VK_SUCCESS;
    VkResult r = yona_vk_build_simple_compute_pipe(
        kYonaGpuMapSquareInt32Spv, kYonaGpuMapSquareInt32SpvWordCount, 8u, &g_yona_pipe_mapsquare_i32);
    if (r == VK_SUCCESS) g_yona_pipe_mapsquare_i32.ready = 1;
    return r;
}

VkResult yona_vk_compute_ensure_reduce_pipe(void) {
    if (g_yona_pipe_reduce.ready) return VK_SUCCESS;
    VkResult r = yona_vk_build_two_ssbo_compute_pipe(
        kYonaGpuReduceBlockInt64Spv, kYonaGpuReduceBlockInt64SpvWordCount, 4u, &g_yona_pipe_reduce);
    if (r == VK_SUCCESS) g_yona_pipe_reduce.ready = 1;
    return r;
}

VkResult yona_vk_compute_ensure_reduce_i32_pipe(void) {
    if (g_yona_pipe_reduce_i32.ready) return VK_SUCCESS;
    VkResult r = yona_vk_build_two_ssbo_compute_pipe(
        kYonaGpuReduceBlockInt32Spv, kYonaGpuReduceBlockInt32SpvWordCount, 4u, &g_yona_pipe_reduce_i32);
    if (r == VK_SUCCESS) g_yona_pipe_reduce_i32.ready = 1;
    return r;
}

VkResult yona_vk_compute_ensure_filter_mark_pipe(void) {
    if (g_yona_pipe_filter_mark.ready) return VK_SUCCESS;
    VkResult r = yona_vk_build_two_ssbo_compute_pipe(
        kYonaGpuFilterMarkInt64Spv, kYonaGpuFilterMarkInt64SpvWordCount, 12u, &g_yona_pipe_filter_mark);
    if (r == VK_SUCCESS) g_yona_pipe_filter_mark.ready = 1;
    return r;
}

VkResult yona_vk_compute_ensure_filter_mark_lt_pipe(void) {
    if (g_yona_pipe_filter_mark_lt.ready) return VK_SUCCESS;
    VkResult r = yona_vk_build_two_ssbo_compute_pipe(
        kYonaGpuFilterMarkLtInt64Spv, kYonaGpuFilterMarkLtInt64SpvWordCount, 12u,
        &g_yona_pipe_filter_mark_lt);
    if (r == VK_SUCCESS) g_yona_pipe_filter_mark_lt.ready = 1;
    return r;
}

VkResult yona_vk_compute_ensure_filter_scatter_pipe(void) {
    if (g_yona_pipe_filter_scatter.ready) return VK_SUCCESS;
    VkResult r = yona_vk_build_four_ssbo_compute_pipe(kYonaGpuFilterScatterInt64Spv,
                                                      kYonaGpuFilterScatterInt64SpvWordCount, 4u,
                                                      &g_yona_pipe_filter_scatter);
    if (r == VK_SUCCESS) g_yona_pipe_filter_scatter.ready = 1;
    return r;
}

VkResult yona_vk_compute_ensure_filter_flags_to_i64_pipe(void) {
    if (g_yona_pipe_filter_flags_to_i64.ready) return VK_SUCCESS;
    VkResult r = yona_vk_build_two_ssbo_compute_pipe(
        kYonaGpuFilterFlagsToI64Spv, kYonaGpuFilterFlagsToI64SpvWordCount, 4u,
        &g_yona_pipe_filter_flags_to_i64);
    if (r == VK_SUCCESS) g_yona_pipe_filter_flags_to_i64.ready = 1;
    return r;
}

VkResult yona_vk_compute_ensure_filter_prefix_pipe(void) {
    if (g_yona_pipe_filter_prefix.ready) return VK_SUCCESS;
    VkResult r = yona_vk_build_two_ssbo_compute_pipe(
        kYonaGpuFilterPrefixInclusiveStepSpv, kYonaGpuFilterPrefixInclusiveStepSpvWordCount, 8u,
        &g_yona_pipe_filter_prefix);
    if (r == VK_SUCCESS) g_yona_pipe_filter_prefix.ready = 1;
    return r;
}

VkResult yona_vk_compute_ensure_filter_inc_to_exc_pipe(void) {
    if (g_yona_pipe_filter_inc_to_exc.ready) return VK_SUCCESS;
    VkResult r = yona_vk_build_three_ssbo_compute_pipe(
        kYonaGpuFilterInclusiveToExclusiveSpv, kYonaGpuFilterInclusiveToExclusiveSpvWordCount, 4u,
        &g_yona_pipe_filter_inc_to_exc);
    if (r == VK_SUCCESS) g_yona_pipe_filter_inc_to_exc.ready = 1;
    return r;
}

VkResult yona_vk_compute_ensure_filter_mark_i32_pipe(void) {
    if (g_yona_pipe_filter_mark_i32.ready) return VK_SUCCESS;
    VkResult r = yona_vk_build_two_ssbo_compute_pipe(
        kYonaGpuFilterMarkInt32Spv, kYonaGpuFilterMarkInt32SpvWordCount, 8u,
        &g_yona_pipe_filter_mark_i32);
    if (r == VK_SUCCESS) g_yona_pipe_filter_mark_i32.ready = 1;
    return r;
}

VkResult yona_vk_compute_ensure_filter_mark_lt_i32_pipe(void) {
    if (g_yona_pipe_filter_mark_lt_i32.ready) return VK_SUCCESS;
    VkResult r = yona_vk_build_two_ssbo_compute_pipe(
        kYonaGpuFilterMarkLtInt32Spv, kYonaGpuFilterMarkLtInt32SpvWordCount, 8u,
        &g_yona_pipe_filter_mark_lt_i32);
    if (r == VK_SUCCESS) g_yona_pipe_filter_mark_lt_i32.ready = 1;
    return r;
}

VkResult yona_vk_compute_ensure_filter_scatter_i32_pipe(void) {
    if (g_yona_pipe_filter_scatter_i32.ready) return VK_SUCCESS;
    VkResult r = yona_vk_build_four_ssbo_compute_pipe(kYonaGpuFilterScatterInt32Spv,
                                                      kYonaGpuFilterScatterInt32SpvWordCount, 4u,
                                                      &g_yona_pipe_filter_scatter_i32);
    if (r == VK_SUCCESS) g_yona_pipe_filter_scatter_i32.ready = 1;
    return r;
}

VkResult yona_vk_compute_ensure_filter_flags_to_i32_pipe(void) {
    if (g_yona_pipe_filter_flags_to_i32.ready) return VK_SUCCESS;
    VkResult r = yona_vk_build_two_ssbo_compute_pipe(
        kYonaGpuFilterFlagsToInt32Spv, kYonaGpuFilterFlagsToInt32SpvWordCount, 4u,
        &g_yona_pipe_filter_flags_to_i32);
    if (r == VK_SUCCESS) g_yona_pipe_filter_flags_to_i32.ready = 1;
    return r;
}

VkResult yona_vk_compute_ensure_filter_prefix_i32_pipe(void) {
    if (g_yona_pipe_filter_prefix_i32.ready) return VK_SUCCESS;
    VkResult r = yona_vk_build_two_ssbo_compute_pipe(
        kYonaGpuFilterPrefixInclusiveStepInt32Spv, kYonaGpuFilterPrefixInclusiveStepInt32SpvWordCount,
        8u, &g_yona_pipe_filter_prefix_i32);
    if (r == VK_SUCCESS) g_yona_pipe_filter_prefix_i32.ready = 1;
    return r;
}

VkResult yona_vk_compute_ensure_filter_inc_to_exc_i32_pipe(void) {
    if (g_yona_pipe_filter_inc_to_exc_i32.ready) return VK_SUCCESS;
    VkResult r = yona_vk_build_three_ssbo_compute_pipe(
        kYonaGpuFilterInclusiveToExclusiveInt32Spv,
        kYonaGpuFilterInclusiveToExclusiveInt32SpvWordCount, 4u, &g_yona_pipe_filter_inc_to_exc_i32);
    if (r == VK_SUCCESS) g_yona_pipe_filter_inc_to_exc_i32.ready = 1;
    return r;
}

YonaVkSimplePipe* yona_vk_compute_mapadd_pipe(void) { return &g_yona_pipe_mapadd; }

YonaVkSimplePipe* yona_vk_compute_mapadd_i32_pipe(void) { return &g_yona_pipe_mapadd_i32; }

YonaVkSimplePipe* yona_vk_compute_mapmul_pipe(void) { return &g_yona_pipe_mapmul; }

YonaVkSimplePipe* yona_vk_compute_mapmul_i32_pipe(void) { return &g_yona_pipe_mapmul_i32; }

YonaVkSimplePipe* yona_vk_compute_mapsquare_pipe(void) { return &g_yona_pipe_mapsquare; }

YonaVkSimplePipe* yona_vk_compute_mapsquare_i32_pipe(void) { return &g_yona_pipe_mapsquare_i32; }

YonaVkReducePipe* yona_vk_compute_reduce_pipe(void) { return &g_yona_pipe_reduce; }

YonaVkReducePipe* yona_vk_compute_reduce_i32_pipe(void) { return &g_yona_pipe_reduce_i32; }

YonaVkReducePipe* yona_vk_compute_filter_mark_pipe(void) { return &g_yona_pipe_filter_mark; }

YonaVkReducePipe* yona_vk_compute_filter_mark_lt_pipe(void) { return &g_yona_pipe_filter_mark_lt; }

YonaVkScatterPipe* yona_vk_compute_filter_scatter_pipe(void) { return &g_yona_pipe_filter_scatter; }

YonaVkReducePipe* yona_vk_compute_filter_flags_to_i64_pipe(void) {
    return &g_yona_pipe_filter_flags_to_i64;
}

YonaVkReducePipe* yona_vk_compute_filter_prefix_pipe(void) { return &g_yona_pipe_filter_prefix; }

YonaVkScatterPipe* yona_vk_compute_filter_inc_to_exc_pipe(void) {
    return &g_yona_pipe_filter_inc_to_exc;
}

YonaVkReducePipe* yona_vk_compute_filter_mark_i32_pipe(void) { return &g_yona_pipe_filter_mark_i32; }

YonaVkReducePipe* yona_vk_compute_filter_mark_lt_i32_pipe(void) {
    return &g_yona_pipe_filter_mark_lt_i32;
}

YonaVkScatterPipe* yona_vk_compute_filter_scatter_i32_pipe(void) {
    return &g_yona_pipe_filter_scatter_i32;
}

YonaVkReducePipe* yona_vk_compute_filter_flags_to_i32_pipe(void) {
    return &g_yona_pipe_filter_flags_to_i32;
}

YonaVkReducePipe* yona_vk_compute_filter_prefix_i32_pipe(void) {
    return &g_yona_pipe_filter_prefix_i32;
}

YonaVkScatterPipe* yona_vk_compute_filter_inc_to_exc_i32_pipe(void) {
    return &g_yona_pipe_filter_inc_to_exc_i32;
}
