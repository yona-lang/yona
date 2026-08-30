/* Cached compute pipelines (map/mul/reduce/filter) + submit serialization —
 * included from gpu_vulkan_device.c */

#include "Runtime/Gpu/VulkanInternal.h"

#if YONA_GPU_VULKAN_ENABLED

#include "Runtime/Generated/FilterFlagsToInt32Spv.inc"
#include "Runtime/Generated/FilterFlagsToInt64Spv.inc"
#include "Runtime/Generated/FilterInclusiveToExclusiveInt32Spv.inc"
#include "Runtime/Generated/FilterInclusiveToExclusiveSpv.inc"
#include "Runtime/Generated/FilterMarkInt32Spv.inc"
#include "Runtime/Generated/FilterMarkInt64Spv.inc"
#include "Runtime/Generated/FilterMarkLessThanInt32Spv.inc"
#include "Runtime/Generated/FilterMarkLessThanInt64Spv.inc"
#include "Runtime/Generated/FilterPrefixInclusiveStepInt32Spv.inc"
#include "Runtime/Generated/FilterPrefixInclusiveStepSpv.inc"
#include "Runtime/Generated/FilterScatterInt32Spv.inc"
#include "Runtime/Generated/FilterScatterInt64Spv.inc"
#include "Runtime/Generated/MapAddInt32Spv.inc"
#include "Runtime/Generated/MapAddInt64Spv.inc"
#include "Runtime/Generated/MapMultiplyInt32Spv.inc"
#include "Runtime/Generated/MapMultiplyInt64Spv.inc"
#include "Runtime/Generated/MapSquareInt32Spv.inc"
#include "Runtime/Generated/MapSquareInt64Spv.inc"
#include "Runtime/Generated/ReduceBlockInt32Spv.inc"
#include "Runtime/Generated/ReduceBlockInt64Spv.inc"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include "yona/Runtime/Platform/Windows.h"
#else
#include <pthread.h>
#endif

#if defined(_WIN32)
static SRWLOCK YonaVulkanSubmitLock = SRWLOCK_INIT;

void yonaVulkanComputeSubmitLock(void) {
  AcquireSRWLockExclusive(&YonaVulkanSubmitLock);
}

void yonaVulkanComputeSubmitUnlock(void) {
  ReleaseSRWLockExclusive(&YonaVulkanSubmitLock);
}

#else

static pthread_mutex_t YonaVulkanSubmitMutex = PTHREAD_MUTEX_INITIALIZER;

void yonaVulkanComputeSubmitLock(void) {
  (void)pthread_mutex_lock(&YonaVulkanSubmitMutex);
}

void yonaVulkanComputeSubmitUnlock(void) {
  (void)pthread_mutex_unlock(&YonaVulkanSubmitMutex);
}

#endif

#define YONA_VK_DPA(name)                                                      \
  ((PFN_##name)(void *)YonaVulkanGetDeviceProcAddress(YonaVulkanDevice, #name))

static YonaVulkanSimplePipeline MapAddPipeline;
static YonaVulkanSimplePipeline MapMulPipeline;
static YonaVulkanSimplePipeline MapSquarePipeline;
static YonaVulkanSimplePipeline MapAddPipelineI32;
static YonaVulkanSimplePipeline MapMulPipelineI32;
static YonaVulkanSimplePipeline MapSquarePipelineI32;
static YonaVulkanReducePipeline ReducePipeline;
static YonaVulkanReducePipeline ReducePipelineI32;
static YonaVulkanReducePipeline FilterMarkPipeline;
static YonaVulkanReducePipeline FilterMarkPipelineLt;
static YonaVulkanScatterPipeline FilterScatterPipeline;
static YonaVulkanReducePipeline FilterFlagsToInt64Pipeline;
static YonaVulkanReducePipeline FilterPrefixPipeline;
static YonaVulkanScatterPipeline FilterIncToExcPipeline;
static YonaVulkanReducePipeline FilterMarkPipelineI32;
static YonaVulkanReducePipeline FilterMarkPipelineLtI32;
static YonaVulkanScatterPipeline FilterScatterPipelineI32;
static YonaVulkanReducePipeline FilterFlagsToI32Pipeline;
static YonaVulkanReducePipeline FilterPrefixPipelineI32;
static YonaVulkanScatterPipeline FilterIncToExcPipelineI32;

static void yonaVulkanDestroySimplePipe(YonaVulkanSimplePipeline *P) {
  PFN_vkDestroyPipeline VkDestroyPipeline = YONA_VK_DPA(vkDestroyPipeline);
  PFN_vkDestroyPipelineLayout VkDestroyPipelineLayout =
      YONA_VK_DPA(vkDestroyPipelineLayout);
  PFN_vkDestroyDescriptorSetLayout VkDestroyDescriptorSetLayout =
      YONA_VK_DPA(vkDestroyDescriptorSetLayout);
  PFN_vkDestroyShaderModule VkDestroyShaderModule =
      YONA_VK_DPA(vkDestroyShaderModule);
  if (!VkDestroyPipeline || !VkDestroyPipelineLayout ||
      !VkDestroyDescriptorSetLayout || !VkDestroyShaderModule)
    goto clear;
  if (P->Pipeline != VK_NULL_HANDLE)
    VkDestroyPipeline(YonaVulkanDevice, P->Pipeline, NULL);
  if (P->PipelineLayout != VK_NULL_HANDLE)
    VkDestroyPipelineLayout(YonaVulkanDevice, P->PipelineLayout, NULL);
  if (P->DescriptorSetLayout != VK_NULL_HANDLE)
    VkDestroyDescriptorSetLayout(YonaVulkanDevice, P->DescriptorSetLayout,
                                 NULL);
  if (P->ShaderModule != VK_NULL_HANDLE)
    VkDestroyShaderModule(YonaVulkanDevice, P->ShaderModule, NULL);
clear:
  P->Pipeline = VK_NULL_HANDLE;
  P->PipelineLayout = VK_NULL_HANDLE;
  P->DescriptorSetLayout = VK_NULL_HANDLE;
  P->ShaderModule = VK_NULL_HANDLE;
  P->Ready = 0;
}

void yonaVulkanComputeDestroyCachedPipelines(void) {
  if (YonaVulkanDevice == VK_NULL_HANDLE) {
    memset(&MapAddPipeline, 0, sizeof MapAddPipeline);
    memset(&MapMulPipeline, 0, sizeof MapMulPipeline);
    memset(&MapSquarePipeline, 0, sizeof MapSquarePipeline);
    memset(&MapAddPipelineI32, 0, sizeof MapAddPipelineI32);
    memset(&MapMulPipelineI32, 0, sizeof MapMulPipelineI32);
    memset(&MapSquarePipelineI32, 0, sizeof MapSquarePipelineI32);
    memset(&ReducePipeline, 0, sizeof ReducePipeline);
    memset(&ReducePipelineI32, 0, sizeof ReducePipelineI32);
    memset(&FilterMarkPipeline, 0, sizeof FilterMarkPipeline);
    memset(&FilterMarkPipelineLt, 0, sizeof FilterMarkPipelineLt);
    memset(&FilterScatterPipeline, 0, sizeof FilterScatterPipeline);
    memset(&FilterFlagsToInt64Pipeline, 0, sizeof FilterFlagsToInt64Pipeline);
    memset(&FilterPrefixPipeline, 0, sizeof FilterPrefixPipeline);
    memset(&FilterIncToExcPipeline, 0, sizeof FilterIncToExcPipeline);
    memset(&FilterMarkPipelineI32, 0, sizeof FilterMarkPipelineI32);
    memset(&FilterMarkPipelineLtI32, 0, sizeof FilterMarkPipelineLtI32);
    memset(&FilterScatterPipelineI32, 0, sizeof FilterScatterPipelineI32);
    memset(&FilterFlagsToI32Pipeline, 0, sizeof FilterFlagsToI32Pipeline);
    memset(&FilterPrefixPipelineI32, 0, sizeof FilterPrefixPipelineI32);
    memset(&FilterIncToExcPipelineI32, 0, sizeof FilterIncToExcPipelineI32);
    return;
  }
  yonaVulkanDestroySimplePipe(&MapAddPipeline);
  yonaVulkanDestroySimplePipe(&MapMulPipeline);
  yonaVulkanDestroySimplePipe(&MapSquarePipeline);
  yonaVulkanDestroySimplePipe(&MapAddPipelineI32);
  yonaVulkanDestroySimplePipe(&MapMulPipelineI32);
  yonaVulkanDestroySimplePipe(&MapSquarePipelineI32);
  yonaVulkanDestroySimplePipe((YonaVulkanSimplePipeline *)&ReducePipeline);
  yonaVulkanDestroySimplePipe((YonaVulkanSimplePipeline *)&ReducePipelineI32);
  yonaVulkanDestroySimplePipe((YonaVulkanSimplePipeline *)&FilterMarkPipeline);
  yonaVulkanDestroySimplePipe(
      (YonaVulkanSimplePipeline *)&FilterMarkPipelineLt);
  yonaVulkanDestroySimplePipe(
      (YonaVulkanSimplePipeline *)&FilterScatterPipeline);
  yonaVulkanDestroySimplePipe(
      (YonaVulkanSimplePipeline *)&FilterFlagsToInt64Pipeline);
  yonaVulkanDestroySimplePipe(
      (YonaVulkanSimplePipeline *)&FilterPrefixPipeline);
  yonaVulkanDestroySimplePipe(
      (YonaVulkanSimplePipeline *)&FilterIncToExcPipeline);
  yonaVulkanDestroySimplePipe(
      (YonaVulkanSimplePipeline *)&FilterMarkPipelineI32);
  yonaVulkanDestroySimplePipe(
      (YonaVulkanSimplePipeline *)&FilterMarkPipelineLtI32);
  yonaVulkanDestroySimplePipe(
      (YonaVulkanSimplePipeline *)&FilterScatterPipelineI32);
  yonaVulkanDestroySimplePipe(
      (YonaVulkanSimplePipeline *)&FilterFlagsToI32Pipeline);
  yonaVulkanDestroySimplePipe(
      (YonaVulkanSimplePipeline *)&FilterPrefixPipelineI32);
  yonaVulkanDestroySimplePipe(
      (YonaVulkanSimplePipeline *)&FilterIncToExcPipelineI32);
}

static VkResult
yonaVulkanBuildSimpleComputePipe(const uint32_t *Spirv, uint32_t SpirvWords,
                                 uint32_t PushBytes,
                                 YonaVulkanSimplePipeline *Out) {
  memset(Out, 0, sizeof(*Out));
  PFN_vkCreateShaderModule VkCreateShaderModule =
      YONA_VK_DPA(vkCreateShaderModule);
  PFN_vkCreateDescriptorSetLayout VkCreateDescriptorSetLayout =
      YONA_VK_DPA(vkCreateDescriptorSetLayout);
  PFN_vkCreatePipelineLayout VkCreatePipelineLayout =
      YONA_VK_DPA(vkCreatePipelineLayout);
  PFN_vkCreateComputePipelines VkCreateComputePipelines =
      YONA_VK_DPA(vkCreateComputePipelines);
  if (!VkCreateShaderModule || !VkCreateDescriptorSetLayout ||
      !VkCreatePipelineLayout || !VkCreateComputePipelines)
    return VK_ERROR_INITIALIZATION_FAILED;

  VkShaderModuleCreateInfo Smci = {0};
  Smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  Smci.codeSize = (size_t)SpirvWords * sizeof(uint32_t);
  Smci.pCode = Spirv;
  VkResult R =
      VkCreateShaderModule(YonaVulkanDevice, &Smci, NULL, &Out->ShaderModule);
  if (R != VK_SUCCESS)
    return R;

  VkDescriptorSetLayoutBinding Bind = {0};
  Bind.binding = 0;
  Bind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Bind.descriptorCount = 1;
  Bind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

  VkDescriptorSetLayoutCreateInfo Dslci = {0};
  Dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  Dslci.bindingCount = 1;
  Dslci.pBindings = &Bind;
  R = VkCreateDescriptorSetLayout(YonaVulkanDevice, &Dslci, NULL,
                                  &Out->DescriptorSetLayout);
  if (R != VK_SUCCESS)
    goto fail_sm;

  VkPushConstantRange Pcr = {0};
  Pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  Pcr.offset = 0;
  Pcr.size = PushBytes;

  VkPipelineLayoutCreateInfo Plci = {0};
  Plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  Plci.setLayoutCount = 1;
  Plci.pSetLayouts = &Out->DescriptorSetLayout;
  Plci.pushConstantRangeCount = 1;
  Plci.pPushConstantRanges = &Pcr;
  R = VkCreatePipelineLayout(YonaVulkanDevice, &Plci, NULL,
                             &Out->PipelineLayout);
  if (R != VK_SUCCESS)
    goto fail_dsl;

  VkPipelineShaderStageCreateInfo Stage = {0};
  Stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  Stage.module = Out->ShaderModule;
  Stage.pName = "main";

  VkComputePipelineCreateInfo Cpci = {0};
  Cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  Cpci.stage = Stage;
  Cpci.layout = Out->PipelineLayout;
  R = VkCreateComputePipelines(YonaVulkanDevice, VK_NULL_HANDLE, 1, &Cpci, NULL,
                               &Out->Pipeline);
  if (R != VK_SUCCESS)
    goto fail_pl;
  return VK_SUCCESS;

fail_pl: {
  PFN_vkDestroyPipelineLayout VkDestroyPipelineLayout =
      YONA_VK_DPA(vkDestroyPipelineLayout);
  if (VkDestroyPipelineLayout)
    VkDestroyPipelineLayout(YonaVulkanDevice, Out->PipelineLayout, NULL);
  Out->PipelineLayout = VK_NULL_HANDLE;
}
fail_dsl: {
  PFN_vkDestroyDescriptorSetLayout VkDestroyDescriptorSetLayout =
      YONA_VK_DPA(vkDestroyDescriptorSetLayout);
  if (VkDestroyDescriptorSetLayout)
    VkDestroyDescriptorSetLayout(YonaVulkanDevice, Out->DescriptorSetLayout,
                                 NULL);
  Out->DescriptorSetLayout = VK_NULL_HANDLE;
}
fail_sm: {
  PFN_vkDestroyShaderModule VkDestroyShaderModule =
      YONA_VK_DPA(vkDestroyShaderModule);
  if (VkDestroyShaderModule)
    VkDestroyShaderModule(YonaVulkanDevice, Out->ShaderModule, NULL);
  Out->ShaderModule = VK_NULL_HANDLE;
}
  return R;
}

static VkResult
yonaVulkanBuildTwoSsboComputePipe(const uint32_t *Spirv, uint32_t SpirvWords,
                                  uint32_t PushBytes,
                                  YonaVulkanReducePipeline *Out) {
  memset(Out, 0, sizeof(*Out));
  PFN_vkCreateShaderModule VkCreateShaderModule =
      YONA_VK_DPA(vkCreateShaderModule);
  PFN_vkCreateDescriptorSetLayout VkCreateDescriptorSetLayout =
      YONA_VK_DPA(vkCreateDescriptorSetLayout);
  PFN_vkCreatePipelineLayout VkCreatePipelineLayout =
      YONA_VK_DPA(vkCreatePipelineLayout);
  PFN_vkCreateComputePipelines VkCreateComputePipelines =
      YONA_VK_DPA(vkCreateComputePipelines);
  if (!VkCreateShaderModule || !VkCreateDescriptorSetLayout ||
      !VkCreatePipelineLayout || !VkCreateComputePipelines)
    return VK_ERROR_INITIALIZATION_FAILED;

  VkShaderModuleCreateInfo Smci = {0};
  Smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  Smci.codeSize = (size_t)SpirvWords * sizeof(uint32_t);
  Smci.pCode = Spirv;
  VkResult R =
      VkCreateShaderModule(YonaVulkanDevice, &Smci, NULL, &Out->ShaderModule);
  if (R != VK_SUCCESS)
    return R;

  VkDescriptorSetLayoutBinding Binds[2] = {0};
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
  R = VkCreateDescriptorSetLayout(YonaVulkanDevice, &Dslci, NULL,
                                  &Out->DescriptorSetLayout);
  if (R != VK_SUCCESS)
    goto fail_sm;

  VkPushConstantRange Pcr = {0};
  Pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  Pcr.offset = 0;
  Pcr.size = PushBytes;

  VkPipelineLayoutCreateInfo Plci = {0};
  Plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  Plci.setLayoutCount = 1;
  Plci.pSetLayouts = &Out->DescriptorSetLayout;
  Plci.pushConstantRangeCount = 1;
  Plci.pPushConstantRanges = &Pcr;
  R = VkCreatePipelineLayout(YonaVulkanDevice, &Plci, NULL,
                             &Out->PipelineLayout);
  if (R != VK_SUCCESS)
    goto fail_dsl;

  VkPipelineShaderStageCreateInfo Stage = {0};
  Stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  Stage.module = Out->ShaderModule;
  Stage.pName = "main";

  VkComputePipelineCreateInfo Cpci = {0};
  Cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  Cpci.stage = Stage;
  Cpci.layout = Out->PipelineLayout;
  R = VkCreateComputePipelines(YonaVulkanDevice, VK_NULL_HANDLE, 1, &Cpci, NULL,
                               &Out->Pipeline);
  if (R != VK_SUCCESS)
    goto fail_pl;
  return VK_SUCCESS;

fail_pl: {
  PFN_vkDestroyPipelineLayout VkDestroyPipelineLayout =
      YONA_VK_DPA(vkDestroyPipelineLayout);
  if (VkDestroyPipelineLayout)
    VkDestroyPipelineLayout(YonaVulkanDevice, Out->PipelineLayout, NULL);
  Out->PipelineLayout = VK_NULL_HANDLE;
}
fail_dsl: {
  PFN_vkDestroyDescriptorSetLayout VkDestroyDescriptorSetLayout =
      YONA_VK_DPA(vkDestroyDescriptorSetLayout);
  if (VkDestroyDescriptorSetLayout)
    VkDestroyDescriptorSetLayout(YonaVulkanDevice, Out->DescriptorSetLayout,
                                 NULL);
  Out->DescriptorSetLayout = VK_NULL_HANDLE;
}
fail_sm: {
  PFN_vkDestroyShaderModule VkDestroyShaderModule =
      YONA_VK_DPA(vkDestroyShaderModule);
  if (VkDestroyShaderModule)
    VkDestroyShaderModule(YonaVulkanDevice, Out->ShaderModule, NULL);
  Out->ShaderModule = VK_NULL_HANDLE;
}
  return R;
}

static VkResult
yonaVulkanBuildFourSsboComputePipe(const uint32_t *Spirv, uint32_t SpirvWords,
                                   uint32_t PushBytes,
                                   YonaVulkanScatterPipeline *Out) {
  memset(Out, 0, sizeof(*Out));
  PFN_vkCreateShaderModule VkCreateShaderModule =
      YONA_VK_DPA(vkCreateShaderModule);
  PFN_vkCreateDescriptorSetLayout VkCreateDescriptorSetLayout =
      YONA_VK_DPA(vkCreateDescriptorSetLayout);
  PFN_vkCreatePipelineLayout VkCreatePipelineLayout =
      YONA_VK_DPA(vkCreatePipelineLayout);
  PFN_vkCreateComputePipelines VkCreateComputePipelines =
      YONA_VK_DPA(vkCreateComputePipelines);
  if (!VkCreateShaderModule || !VkCreateDescriptorSetLayout ||
      !VkCreatePipelineLayout || !VkCreateComputePipelines)
    return VK_ERROR_INITIALIZATION_FAILED;

  VkShaderModuleCreateInfo Smci = {0};
  Smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  Smci.codeSize = (size_t)SpirvWords * sizeof(uint32_t);
  Smci.pCode = Spirv;
  VkResult R =
      VkCreateShaderModule(YonaVulkanDevice, &Smci, NULL, &Out->ShaderModule);
  if (R != VK_SUCCESS)
    return R;

  VkDescriptorSetLayoutBinding Binds[4] = {0};
  for (uint32_t B = 0; B < 4u; B++) {
    Binds[B].binding = B;
    Binds[B].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Binds[B].descriptorCount = 1;
    Binds[B].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  }

  VkDescriptorSetLayoutCreateInfo Dslci = {0};
  Dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  Dslci.bindingCount = 4;
  Dslci.pBindings = Binds;
  R = VkCreateDescriptorSetLayout(YonaVulkanDevice, &Dslci, NULL,
                                  &Out->DescriptorSetLayout);
  if (R != VK_SUCCESS)
    goto fail_sm;

  VkPushConstantRange Pcr = {0};
  Pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  Pcr.offset = 0;
  Pcr.size = PushBytes;

  VkPipelineLayoutCreateInfo Plci = {0};
  Plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  Plci.setLayoutCount = 1;
  Plci.pSetLayouts = &Out->DescriptorSetLayout;
  Plci.pushConstantRangeCount = 1;
  Plci.pPushConstantRanges = &Pcr;
  R = VkCreatePipelineLayout(YonaVulkanDevice, &Plci, NULL,
                             &Out->PipelineLayout);
  if (R != VK_SUCCESS)
    goto fail_dsl;

  VkPipelineShaderStageCreateInfo Stage = {0};
  Stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  Stage.module = Out->ShaderModule;
  Stage.pName = "main";

  VkComputePipelineCreateInfo Cpci = {0};
  Cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  Cpci.stage = Stage;
  Cpci.layout = Out->PipelineLayout;
  R = VkCreateComputePipelines(YonaVulkanDevice, VK_NULL_HANDLE, 1, &Cpci, NULL,
                               &Out->Pipeline);
  if (R != VK_SUCCESS)
    goto fail_pl;
  return VK_SUCCESS;

fail_pl: {
  PFN_vkDestroyPipelineLayout VkDestroyPipelineLayout =
      YONA_VK_DPA(vkDestroyPipelineLayout);
  if (VkDestroyPipelineLayout)
    VkDestroyPipelineLayout(YonaVulkanDevice, Out->PipelineLayout, NULL);
  Out->PipelineLayout = VK_NULL_HANDLE;
}
fail_dsl: {
  PFN_vkDestroyDescriptorSetLayout VkDestroyDescriptorSetLayout =
      YONA_VK_DPA(vkDestroyDescriptorSetLayout);
  if (VkDestroyDescriptorSetLayout)
    VkDestroyDescriptorSetLayout(YonaVulkanDevice, Out->DescriptorSetLayout,
                                 NULL);
  Out->DescriptorSetLayout = VK_NULL_HANDLE;
}
fail_sm: {
  PFN_vkDestroyShaderModule VkDestroyShaderModule =
      YONA_VK_DPA(vkDestroyShaderModule);
  if (VkDestroyShaderModule)
    VkDestroyShaderModule(YonaVulkanDevice, Out->ShaderModule, NULL);
  Out->ShaderModule = VK_NULL_HANDLE;
}
  return R;
}

static VkResult
yonaVulkanBuildThreeSsboComputePipe(const uint32_t *Spirv, uint32_t SpirvWords,
                                    uint32_t PushBytes,
                                    YonaVulkanScatterPipeline *Out) {
  memset(Out, 0, sizeof(*Out));
  PFN_vkCreateShaderModule VkCreateShaderModule =
      YONA_VK_DPA(vkCreateShaderModule);
  PFN_vkCreateDescriptorSetLayout VkCreateDescriptorSetLayout =
      YONA_VK_DPA(vkCreateDescriptorSetLayout);
  PFN_vkCreatePipelineLayout VkCreatePipelineLayout =
      YONA_VK_DPA(vkCreatePipelineLayout);
  PFN_vkCreateComputePipelines VkCreateComputePipelines =
      YONA_VK_DPA(vkCreateComputePipelines);
  if (!VkCreateShaderModule || !VkCreateDescriptorSetLayout ||
      !VkCreatePipelineLayout || !VkCreateComputePipelines)
    return VK_ERROR_INITIALIZATION_FAILED;

  VkShaderModuleCreateInfo Smci = {0};
  Smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  Smci.codeSize = (size_t)SpirvWords * sizeof(uint32_t);
  Smci.pCode = Spirv;
  VkResult R =
      VkCreateShaderModule(YonaVulkanDevice, &Smci, NULL, &Out->ShaderModule);
  if (R != VK_SUCCESS)
    return R;

  VkDescriptorSetLayoutBinding Binds[3] = {0};
  for (uint32_t B = 0; B < 3u; B++) {
    Binds[B].binding = B;
    Binds[B].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Binds[B].descriptorCount = 1;
    Binds[B].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  }

  VkDescriptorSetLayoutCreateInfo Dslci = {0};
  Dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  Dslci.bindingCount = 3;
  Dslci.pBindings = Binds;
  R = VkCreateDescriptorSetLayout(YonaVulkanDevice, &Dslci, NULL,
                                  &Out->DescriptorSetLayout);
  if (R != VK_SUCCESS)
    goto fail_sm;

  VkPushConstantRange Pcr = {0};
  Pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  Pcr.offset = 0;
  Pcr.size = PushBytes;

  VkPipelineLayoutCreateInfo Plci = {0};
  Plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  Plci.setLayoutCount = 1;
  Plci.pSetLayouts = &Out->DescriptorSetLayout;
  Plci.pushConstantRangeCount = 1;
  Plci.pPushConstantRanges = &Pcr;
  R = VkCreatePipelineLayout(YonaVulkanDevice, &Plci, NULL,
                             &Out->PipelineLayout);
  if (R != VK_SUCCESS)
    goto fail_dsl;

  VkPipelineShaderStageCreateInfo Stage = {0};
  Stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  Stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  Stage.module = Out->ShaderModule;
  Stage.pName = "main";

  VkComputePipelineCreateInfo Cpci = {0};
  Cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  Cpci.stage = Stage;
  Cpci.layout = Out->PipelineLayout;
  R = VkCreateComputePipelines(YonaVulkanDevice, VK_NULL_HANDLE, 1, &Cpci, NULL,
                               &Out->Pipeline);
  if (R != VK_SUCCESS)
    goto fail_pl;
  return VK_SUCCESS;

fail_pl: {
  PFN_vkDestroyPipelineLayout VkDestroyPipelineLayout =
      YONA_VK_DPA(vkDestroyPipelineLayout);
  if (VkDestroyPipelineLayout)
    VkDestroyPipelineLayout(YonaVulkanDevice, Out->PipelineLayout, NULL);
  Out->PipelineLayout = VK_NULL_HANDLE;
}
fail_dsl: {
  PFN_vkDestroyDescriptorSetLayout VkDestroyDescriptorSetLayout =
      YONA_VK_DPA(vkDestroyDescriptorSetLayout);
  if (VkDestroyDescriptorSetLayout)
    VkDestroyDescriptorSetLayout(YonaVulkanDevice, Out->DescriptorSetLayout,
                                 NULL);
  Out->DescriptorSetLayout = VK_NULL_HANDLE;
}
fail_sm: {
  PFN_vkDestroyShaderModule VkDestroyShaderModule =
      YONA_VK_DPA(vkDestroyShaderModule);
  if (VkDestroyShaderModule)
    VkDestroyShaderModule(YonaVulkanDevice, Out->ShaderModule, NULL);
  Out->ShaderModule = VK_NULL_HANDLE;
}
  return R;
}

VkResult yonaVulkanComputeEnsureMapAddPipe(void) {
  if (MapAddPipeline.Ready)
    return VK_SUCCESS;
  VkResult R = yonaVulkanBuildSimpleComputePipe(YonaGpuMapAddInt64Spv,
                                                YonaGpuMapAddInt64SpvWordCount,
                                                12u, &MapAddPipeline);
  if (R == VK_SUCCESS)
    MapAddPipeline.Ready = 1;
  return R;
}

VkResult yonaVulkanComputeEnsureMapAddI32Pipe(void) {
  if (MapAddPipelineI32.Ready)
    return VK_SUCCESS;
  VkResult R = yonaVulkanBuildSimpleComputePipe(YonaGpuMapAddInt32Spv,
                                                YonaGpuMapAddInt32SpvWordCount,
                                                8u, &MapAddPipelineI32);
  if (R == VK_SUCCESS)
    MapAddPipelineI32.Ready = 1;
  return R;
}

VkResult yonaVulkanComputeEnsureMapMulPipe(void) {
  if (MapMulPipeline.Ready)
    return VK_SUCCESS;
  VkResult R = yonaVulkanBuildSimpleComputePipe(
      YonaGpuMapMultiplyInt64Spv, YonaGpuMapMultiplyInt64SpvWordCount, 12u,
      &MapMulPipeline);
  if (R == VK_SUCCESS)
    MapMulPipeline.Ready = 1;
  return R;
}

VkResult yonaVulkanComputeEnsureMapMulI32Pipe(void) {
  if (MapMulPipelineI32.Ready)
    return VK_SUCCESS;
  VkResult R = yonaVulkanBuildSimpleComputePipe(
      YonaGpuMapMultiplyInt32Spv, YonaGpuMapMultiplyInt32SpvWordCount, 8u,
      &MapMulPipelineI32);
  if (R == VK_SUCCESS)
    MapMulPipelineI32.Ready = 1;
  return R;
}

VkResult yonaVulkanComputeEnsureMapSquarePipe(void) {
  if (MapSquarePipeline.Ready)
    return VK_SUCCESS;
  VkResult R = yonaVulkanBuildSimpleComputePipe(
      YonaGpuMapSquareInt64Spv, YonaGpuMapSquareInt64SpvWordCount, 12u,
      &MapSquarePipeline);
  if (R == VK_SUCCESS)
    MapSquarePipeline.Ready = 1;
  return R;
}

VkResult yonaVulkanComputeEnsureMapSquareI32Pipe(void) {
  if (MapSquarePipelineI32.Ready)
    return VK_SUCCESS;
  VkResult R = yonaVulkanBuildSimpleComputePipe(
      YonaGpuMapSquareInt32Spv, YonaGpuMapSquareInt32SpvWordCount, 8u,
      &MapSquarePipelineI32);
  if (R == VK_SUCCESS)
    MapSquarePipelineI32.Ready = 1;
  return R;
}

VkResult yonaVulkanComputeEnsureReducePipe(void) {
  if (ReducePipeline.Ready)
    return VK_SUCCESS;
  VkResult R = yonaVulkanBuildTwoSsboComputePipe(
      YonaGpuReduceBlockInt64Spv, YonaGpuReduceBlockInt64SpvWordCount, 4u,
      &ReducePipeline);
  if (R == VK_SUCCESS)
    ReducePipeline.Ready = 1;
  return R;
}

VkResult yonaVulkanComputeEnsureReduceI32Pipe(void) {
  if (ReducePipelineI32.Ready)
    return VK_SUCCESS;
  VkResult R = yonaVulkanBuildTwoSsboComputePipe(
      YonaGpuReduceBlockInt32Spv, YonaGpuReduceBlockInt32SpvWordCount, 4u,
      &ReducePipelineI32);
  if (R == VK_SUCCESS)
    ReducePipelineI32.Ready = 1;
  return R;
}

VkResult yonaVulkanComputeEnsureFilterMarkPipe(void) {
  if (FilterMarkPipeline.Ready)
    return VK_SUCCESS;
  VkResult R = yonaVulkanBuildTwoSsboComputePipe(
      YonaGpuFilterMarkInt64Spv, YonaGpuFilterMarkInt64SpvWordCount, 12u,
      &FilterMarkPipeline);
  if (R == VK_SUCCESS)
    FilterMarkPipeline.Ready = 1;
  return R;
}

VkResult yonaVulkanComputeEnsureFilterMarkLtPipe(void) {
  if (FilterMarkPipelineLt.Ready)
    return VK_SUCCESS;
  VkResult R = yonaVulkanBuildTwoSsboComputePipe(
      YonaGpuFilterMarkLessThanInt64Spv,
      YonaGpuFilterMarkLessThanInt64SpvWordCount, 12u, &FilterMarkPipelineLt);
  if (R == VK_SUCCESS)
    FilterMarkPipelineLt.Ready = 1;
  return R;
}

VkResult yonaVulkanComputeEnsureFilterScatterPipe(void) {
  if (FilterScatterPipeline.Ready)
    return VK_SUCCESS;
  VkResult R = yonaVulkanBuildFourSsboComputePipe(
      YonaGpuFilterScatterInt64Spv, YonaGpuFilterScatterInt64SpvWordCount, 4u,
      &FilterScatterPipeline);
  if (R == VK_SUCCESS)
    FilterScatterPipeline.Ready = 1;
  return R;
}

VkResult yonaVulkanComputeEnsureFilterFlagsToInt64Pipe(void) {
  if (FilterFlagsToInt64Pipeline.Ready)
    return VK_SUCCESS;
  VkResult R = yonaVulkanBuildTwoSsboComputePipe(
      YonaGpuFilterFlagsToInt64Spv, YonaGpuFilterFlagsToInt64SpvWordCount, 4u,
      &FilterFlagsToInt64Pipeline);
  if (R == VK_SUCCESS)
    FilterFlagsToInt64Pipeline.Ready = 1;
  return R;
}

VkResult yonaVulkanComputeEnsureFilterPrefixPipe(void) {
  if (FilterPrefixPipeline.Ready)
    return VK_SUCCESS;
  VkResult R = yonaVulkanBuildTwoSsboComputePipe(
      YonaGpuFilterPrefixInclusiveStepSpv,
      YonaGpuFilterPrefixInclusiveStepSpvWordCount, 8u, &FilterPrefixPipeline);
  if (R == VK_SUCCESS)
    FilterPrefixPipeline.Ready = 1;
  return R;
}

VkResult yonaVulkanComputeEnsureFilterIncToExcPipe(void) {
  if (FilterIncToExcPipeline.Ready)
    return VK_SUCCESS;
  VkResult R = yonaVulkanBuildThreeSsboComputePipe(
      YonaGpuFilterInclusiveToExclusiveSpv,
      YonaGpuFilterInclusiveToExclusiveSpvWordCount, 4u,
      &FilterIncToExcPipeline);
  if (R == VK_SUCCESS)
    FilterIncToExcPipeline.Ready = 1;
  return R;
}

VkResult yonaVulkanComputeEnsureFilterMarkI32Pipe(void) {
  if (FilterMarkPipelineI32.Ready)
    return VK_SUCCESS;
  VkResult R = yonaVulkanBuildTwoSsboComputePipe(
      YonaGpuFilterMarkInt32Spv, YonaGpuFilterMarkInt32SpvWordCount, 8u,
      &FilterMarkPipelineI32);
  if (R == VK_SUCCESS)
    FilterMarkPipelineI32.Ready = 1;
  return R;
}

VkResult yonaVulkanComputeEnsureFilterMarkLtI32Pipe(void) {
  if (FilterMarkPipelineLtI32.Ready)
    return VK_SUCCESS;
  VkResult R = yonaVulkanBuildTwoSsboComputePipe(
      YonaGpuFilterMarkLessThanInt32Spv,
      YonaGpuFilterMarkLessThanInt32SpvWordCount, 8u, &FilterMarkPipelineLtI32);
  if (R == VK_SUCCESS)
    FilterMarkPipelineLtI32.Ready = 1;
  return R;
}

VkResult yonaVulkanComputeEnsureFilterScatterI32Pipe(void) {
  if (FilterScatterPipelineI32.Ready)
    return VK_SUCCESS;
  VkResult R = yonaVulkanBuildFourSsboComputePipe(
      YonaGpuFilterScatterInt32Spv, YonaGpuFilterScatterInt32SpvWordCount, 4u,
      &FilterScatterPipelineI32);
  if (R == VK_SUCCESS)
    FilterScatterPipelineI32.Ready = 1;
  return R;
}

VkResult yonaVulkanComputeEnsureFilterFlagsToI32Pipe(void) {
  if (FilterFlagsToI32Pipeline.Ready)
    return VK_SUCCESS;
  VkResult R = yonaVulkanBuildTwoSsboComputePipe(
      YonaGpuFilterFlagsToInt32Spv, YonaGpuFilterFlagsToInt32SpvWordCount, 4u,
      &FilterFlagsToI32Pipeline);
  if (R == VK_SUCCESS)
    FilterFlagsToI32Pipeline.Ready = 1;
  return R;
}

VkResult yonaVulkanComputeEnsureFilterPrefixI32Pipe(void) {
  if (FilterPrefixPipelineI32.Ready)
    return VK_SUCCESS;
  VkResult R = yonaVulkanBuildTwoSsboComputePipe(
      YonaGpuFilterPrefixInclusiveStepInt32Spv,
      YonaGpuFilterPrefixInclusiveStepInt32SpvWordCount, 8u,
      &FilterPrefixPipelineI32);
  if (R == VK_SUCCESS)
    FilterPrefixPipelineI32.Ready = 1;
  return R;
}

VkResult yonaVulkanComputeEnsureFilterIncToExcI32Pipe(void) {
  if (FilterIncToExcPipelineI32.Ready)
    return VK_SUCCESS;
  VkResult R = yonaVulkanBuildThreeSsboComputePipe(
      YonaGpuFilterInclusiveToExclusiveInt32Spv,
      YonaGpuFilterInclusiveToExclusiveInt32SpvWordCount, 4u,
      &FilterIncToExcPipelineI32);
  if (R == VK_SUCCESS)
    FilterIncToExcPipelineI32.Ready = 1;
  return R;
}

YonaVulkanSimplePipeline *yonaVulkanComputeMapAddPipe(void) {
  return &MapAddPipeline;
}

YonaVulkanSimplePipeline *yonaVulkanComputeMapAddI32Pipe(void) {
  return &MapAddPipelineI32;
}

YonaVulkanSimplePipeline *yonaVulkanComputeMapMulPipe(void) {
  return &MapMulPipeline;
}

YonaVulkanSimplePipeline *yonaVulkanComputeMapMulI32Pipe(void) {
  return &MapMulPipelineI32;
}

YonaVulkanSimplePipeline *yonaVulkanComputeMapSquarePipe(void) {
  return &MapSquarePipeline;
}

YonaVulkanSimplePipeline *yonaVulkanComputeMapSquareI32Pipe(void) {
  return &MapSquarePipelineI32;
}

YonaVulkanReducePipeline *yonaVulkanComputeReducePipe(void) {
  return &ReducePipeline;
}

YonaVulkanReducePipeline *yonaVulkanComputeReduceI32Pipe(void) {
  return &ReducePipelineI32;
}

YonaVulkanReducePipeline *yonaVulkanComputeFilterMarkPipe(void) {
  return &FilterMarkPipeline;
}

YonaVulkanReducePipeline *yonaVulkanComputeFilterMarkLtPipe(void) {
  return &FilterMarkPipelineLt;
}

YonaVulkanScatterPipeline *yonaVulkanComputeFilterScatterPipe(void) {
  return &FilterScatterPipeline;
}

YonaVulkanReducePipeline *yonaVulkanComputeFilterFlagsToInt64Pipe(void) {
  return &FilterFlagsToInt64Pipeline;
}

YonaVulkanReducePipeline *yonaVulkanComputeFilterPrefixPipe(void) {
  return &FilterPrefixPipeline;
}

YonaVulkanScatterPipeline *yonaVulkanComputeFilterIncToExcPipe(void) {
  return &FilterIncToExcPipeline;
}

YonaVulkanReducePipeline *yonaVulkanComputeFilterMarkI32Pipe(void) {
  return &FilterMarkPipelineI32;
}

YonaVulkanReducePipeline *yonaVulkanComputeFilterMarkLtI32Pipe(void) {
  return &FilterMarkPipelineLtI32;
}

YonaVulkanScatterPipeline *yonaVulkanComputeFilterScatterI32Pipe(void) {
  return &FilterScatterPipelineI32;
}

YonaVulkanReducePipeline *yonaVulkanComputeFilterFlagsToI32Pipe(void) {
  return &FilterFlagsToI32Pipeline;
}

YonaVulkanReducePipeline *yonaVulkanComputeFilterPrefixI32Pipe(void) {
  return &FilterPrefixPipelineI32;
}

YonaVulkanScatterPipeline *yonaVulkanComputeFilterIncToExcI32Pipe(void) {
  return &FilterIncToExcPipelineI32;
}

#endif /* YONA_GPU_VULKAN_ENABLED */
