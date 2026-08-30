#ifndef YONA_SRC_RUNTIME_GPU_VULKANINTERNAL_H
#define YONA_SRC_RUNTIME_GPU_VULKANINTERNAL_H

#include "yona/Runtime/Gpu/BuildConfig.h"

#if YONA_GPU_VULKAN_ENABLED

#define YONA_VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

typedef struct YonaVulkanSimplePipeline {
  VkShaderModule ShaderModule;
  VkDescriptorSetLayout DescriptorSetLayout;
  VkPipelineLayout PipelineLayout;
  VkPipeline Pipeline;
  int Ready;
} YonaVulkanSimplePipeline;

typedef struct YonaVulkanReducePipeline {
  VkShaderModule ShaderModule;
  VkDescriptorSetLayout DescriptorSetLayout;
  VkPipelineLayout PipelineLayout;
  VkPipeline Pipeline;
  int Ready;
} YonaVulkanReducePipeline;

typedef struct YonaVulkanScatterPipeline {
  VkShaderModule ShaderModule;
  VkDescriptorSetLayout DescriptorSetLayout;
  VkPipelineLayout PipelineLayout;
  VkPipeline Pipeline;
  int Ready;
} YonaVulkanScatterPipeline;

extern PFN_vkGetDeviceProcAddr YonaVulkanGetDeviceProcAddress;
extern PFN_vkGetPhysicalDeviceMemoryProperties
    YonaVulkanGetPhysicalDeviceMemoryProperties;
extern VkPhysicalDevice YonaVulkanPhysicalDevice;
extern VkDevice YonaVulkanDevice;
extern VkQueue YonaVulkanQueue;
extern uint32_t YonaVulkanQueueFamily;
extern int YonaVulkanSynchronization2Enabled;
extern char YonaVulkanLastNote[256];

void yonaVulkanNoteClear(void);
void yonaVulkanNoteCopy(const char *Message);
int yonaVulkanOperationBegin(void);
void yonaVulkanOperationEnd(void);
int yonaVulkanPickMemoryType(uint32_t TypeBits,
                             VkMemoryPropertyFlags DesiredFlags,
                             uint32_t *OutputIndex);

void yonaVulkanComputeDestroyCachedPipelines(void);
void yonaVulkanComputeSubmitLock(void);
void yonaVulkanComputeSubmitUnlock(void);

VkResult yonaVulkanComputeEnsureMapAddPipe(void);
VkResult yonaVulkanComputeEnsureMapAddI32Pipe(void);
VkResult yonaVulkanComputeEnsureMapMulPipe(void);
VkResult yonaVulkanComputeEnsureMapMulI32Pipe(void);
VkResult yonaVulkanComputeEnsureMapSquarePipe(void);
VkResult yonaVulkanComputeEnsureMapSquareI32Pipe(void);
VkResult yonaVulkanComputeEnsureReducePipe(void);
VkResult yonaVulkanComputeEnsureReduceI32Pipe(void);
VkResult yonaVulkanComputeEnsureFilterMarkPipe(void);
VkResult yonaVulkanComputeEnsureFilterMarkLtPipe(void);
VkResult yonaVulkanComputeEnsureFilterScatterPipe(void);
VkResult yonaVulkanComputeEnsureFilterFlagsToInt64Pipe(void);
VkResult yonaVulkanComputeEnsureFilterPrefixPipe(void);
VkResult yonaVulkanComputeEnsureFilterIncToExcPipe(void);
VkResult yonaVulkanComputeEnsureFilterMarkI32Pipe(void);
VkResult yonaVulkanComputeEnsureFilterMarkLtI32Pipe(void);
VkResult yonaVulkanComputeEnsureFilterScatterI32Pipe(void);
VkResult yonaVulkanComputeEnsureFilterFlagsToI32Pipe(void);
VkResult yonaVulkanComputeEnsureFilterPrefixI32Pipe(void);
VkResult yonaVulkanComputeEnsureFilterIncToExcI32Pipe(void);

YonaVulkanSimplePipeline *yonaVulkanComputeMapAddPipe(void);
YonaVulkanSimplePipeline *yonaVulkanComputeMapAddI32Pipe(void);
YonaVulkanSimplePipeline *yonaVulkanComputeMapMulPipe(void);
YonaVulkanSimplePipeline *yonaVulkanComputeMapMulI32Pipe(void);
YonaVulkanSimplePipeline *yonaVulkanComputeMapSquarePipe(void);
YonaVulkanSimplePipeline *yonaVulkanComputeMapSquareI32Pipe(void);
YonaVulkanReducePipeline *yonaVulkanComputeReducePipe(void);
YonaVulkanReducePipeline *yonaVulkanComputeReduceI32Pipe(void);
YonaVulkanReducePipeline *yonaVulkanComputeFilterMarkPipe(void);
YonaVulkanReducePipeline *yonaVulkanComputeFilterMarkLtPipe(void);
YonaVulkanScatterPipeline *yonaVulkanComputeFilterScatterPipe(void);
YonaVulkanReducePipeline *yonaVulkanComputeFilterFlagsToInt64Pipe(void);
YonaVulkanReducePipeline *yonaVulkanComputeFilterPrefixPipe(void);
YonaVulkanScatterPipeline *yonaVulkanComputeFilterIncToExcPipe(void);
YonaVulkanReducePipeline *yonaVulkanComputeFilterMarkI32Pipe(void);
YonaVulkanReducePipeline *yonaVulkanComputeFilterMarkLtI32Pipe(void);
YonaVulkanScatterPipeline *yonaVulkanComputeFilterScatterI32Pipe(void);
YonaVulkanReducePipeline *yonaVulkanComputeFilterFlagsToI32Pipe(void);
YonaVulkanReducePipeline *yonaVulkanComputeFilterPrefixI32Pipe(void);
YonaVulkanScatterPipeline *yonaVulkanComputeFilterIncToExcI32Pipe(void);

#endif

#endif /* YONA_SRC_RUNTIME_GPU_VULKANINTERNAL_H */
