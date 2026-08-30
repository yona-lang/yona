/* Vulkan compute operations: map, reduce, filter, and fused graphs. */

#include "yona/Runtime/Core/Api.h"
#include "yona/Runtime/Gpu/BuildConfig.h"
#include "yona/Runtime/Gpu/VulkanDevice.h"

#if !YONA_GPU_VULKAN_ENABLED

#include <stdint.h>

int YonaRuntimeGpuVulkanTryMapAddInt64(int64_t Delta, int64_t *Arr,
                                       int64_t **Out) {
  (void)Delta;
  (void)Arr;
  (void)Out;
  return 0;
}

int YonaRuntimeGpuVulkanTryMapMulInt64(int64_t Factor, int64_t *Arr,
                                       int64_t **Out) {
  (void)Factor;
  (void)Arr;
  (void)Out;
  return 0;
}

int YonaRuntimeGpuVulkanTryMapSquareInt64(int64_t *Arr, int64_t **Out) {
  (void)Arr;
  (void)Out;
  return 0;
}

int YonaRuntimeGpuVulkanTryReduceSumInt64(int64_t *Arr, int64_t *OutSum) {
  (void)Arr;
  (void)OutSum;
  return 0;
}

int YonaRuntimeGpuVulkanTryFilterGreaterThanInt64(int64_t Threshold,
                                                  int64_t *Arr, int64_t **Out) {
  (void)Threshold;
  (void)Arr;
  (void)Out;
  return 0;
}

int YonaRuntimeGpuVulkanTryFilterLessThanInt64(int64_t Threshold, int64_t *Arr,
                                               int64_t **Out) {
  (void)Threshold;
  (void)Arr;
  (void)Out;
  return 0;
}

int YonaRuntimeGpuVulkanTryMapReduceGraphInt64(int64_t *Stages, int64_t *Arr,
                                               int64_t *OutSum) {
  (void)Stages;
  (void)Arr;
  (void)OutSum;
  return 0;
}

#else /* YONA_GPU_VULKAN_ENABLED */

#include "Runtime/Gpu/VulkanInternal.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define YONA_VULKAN_DEVICE_PROCEDURE(Name)                                     \
  ((PFN_##Name)(void *)YonaVulkanGetDeviceProcAddress(YonaVulkanDevice, #Name))

static int yonaVulkanEnvCompute(void) {
  const char *C = getenv("YONA_GPU_VULKAN_COMPUTE");
  return C && strcmp(C, "1") == 0;
}

static int yonaVulkanEnvMapAdd(void) {
  if (yonaVulkanEnvCompute())
    return 1;
  const char *M = getenv("YONA_GPU_VULKAN_MAPADD");
  return M && strcmp(M, "1") == 0;
}

static int yonaVulkanEnvMapMul(void) {
  if (yonaVulkanEnvCompute())
    return 1;
  const char *M = getenv("YONA_GPU_VULKAN_MAPMUL");
  return M && strcmp(M, "1") == 0;
}

static int yonaVulkanEnvReduce(void) {
  if (yonaVulkanEnvCompute())
    return 1;
  const char *M = getenv("YONA_GPU_VULKAN_REDUCE");
  return M && strcmp(M, "1") == 0;
}

static int yonaVulkanEnvFilter(void) {
  if (yonaVulkanEnvCompute())
    return 1;
  const char *M = getenv("YONA_GPU_VULKAN_FILTER");
  return M && strcmp(M, "1") == 0;
}

static int64_t readMinimumLength(const char *GlobalVariable,
                                 const char *OperationVariable) {
  const char *Value = getenv(GlobalVariable);
  if (Value && Value[0]) {
    int64_t Length = (int64_t)strtoll(Value, NULL, 10);
    return Length < 1 ? 1 : Length;
  }
  Value = getenv(OperationVariable);
  if (Value && Value[0]) {
    int64_t Length = (int64_t)strtoll(Value, NULL, 10);
    return Length < 1 ? 1 : Length;
  }
  return 4096;
}

static int yonaVulkanCommonPrecheck(int64_t *Arr, const char *OpTag) {
  if (!Arr)
    return 0;
  yonaVulkanNoteClear();

  {
    int Ti = YonaRuntimeGpuVulkanDeviceTryInitialize();
    if (Ti != 0) {
      char B[280];
      const char *Devn = YonaRuntimeGpuVulkanDeviceLastNote();
      if (Devn && Devn[0])
        snprintf(B, sizeof B, "%s: initialization returned %d; %s", OpTag, Ti,
                 Devn);
      else
        snprintf(B, sizeof B, "%s: initialization returned %d", OpTag, Ti);
      yonaVulkanNoteCopy(B);
      return 0;
    }
  }
  if (YonaVulkanDevice == VK_NULL_HANDLE || YonaVulkanQueue == VK_NULL_HANDLE) {
    char B[80];
    snprintf(B, sizeof B, "%s: no VkDevice/VkQueue after initialization",
             OpTag);
    yonaVulkanNoteCopy(B);
    return 0;
  }
  return 1;
}

static int yonaVulkanI32MapAddFits(const int64_t *Arr, int64_t Delta) {
  if (Delta < (int64_t)INT32_MIN || Delta > (int64_t)INT32_MAX)
    return 0;
  int64_t Len = Arr[0];
  for (int64_t I = 0; I < Len; I++) {
    int64_t V = Arr[1 + I];
    if (V < (int64_t)INT32_MIN || V > (int64_t)INT32_MAX)
      return 0;
    int64_t S = V + Delta;
    if (S < (int64_t)INT32_MIN || S > (int64_t)INT32_MAX)
      return 0;
  }
  return 1;
}

static int yonaVulkanI32MapMulFits(const int64_t *Arr, int64_t Factor) {
  if (Factor < (int64_t)INT32_MIN || Factor > (int64_t)INT32_MAX)
    return 0;
  int64_t Len = Arr[0];
  for (int64_t I = 0; I < Len; I++) {
    int64_t V = Arr[1 + I];
    if (V < (int64_t)INT32_MIN || V > (int64_t)INT32_MAX)
      return 0;
    int64_t P = V * Factor;
    if (P < (int64_t)INT32_MIN || P > (int64_t)INT32_MAX)
      return 0;
  }
  return 1;
}

static int yonaVulkanI32MapSquareFits(const int64_t *Arr) {
  int64_t Len = Arr[0];
  for (int64_t I = 0; I < Len; I++) {
    int64_t V = Arr[1 + I];
    if (V < (int64_t)INT32_MIN || V > (int64_t)INT32_MAX)
      return 0;
    int64_t P = V * V;
    if (P < (int64_t)INT32_MIN || P > (int64_t)INT32_MAX)
      return 0;
  }
  return 1;
}

static int yonaVulkanPreferI32(void) {
  if (!YonaRuntimeGpuVulkanDeviceHasShaderInt64())
    return 1;
  const char *F = getenv("YONA_GPU_VULKAN_FORCE_I32");
  return (F && F[0] && strcmp(F, "0") != 0) ? 1 : 0;
}

static int yonaVulkanI32FilterFits(const int64_t *Arr, int64_t Threshold) {
  if (Threshold < (int64_t)INT32_MIN || Threshold > (int64_t)INT32_MAX)
    return 0;
  int64_t Len = Arr[0];
  for (int64_t I = 0; I < Len; I++) {
    int64_t V = Arr[1 + I];
    if (V < (int64_t)INT32_MIN || V > (int64_t)INT32_MAX)
      return 0;
  }
  return 1;
}

static int yonaVulkanI32ReduceFits(const int64_t *Arr) {
  int64_t Len = Arr[0];
  int64_t MaxAbs = 0;
  for (int64_t I = 0; I < Len; I++) {
    int64_t V = Arr[1 + I];
    if (V < (int64_t)INT32_MIN || V > (int64_t)INT32_MAX)
      return 0;
    int64_t A = V < 0 ? -V : V;
    if (A > MaxAbs)
      MaxAbs = A;
  }
  /* Shared-memory tree of 64 ints must stay in int32. */
  if (MaxAbs > (int64_t)INT32_MAX / 64)
    return 0;
  return 1;
}

static int yonaVulkanLoadDispatchFunctions(
    PFN_vkCreateDescriptorPool *ODpool, PFN_vkDestroyDescriptorPool *ODpoold,
    PFN_vkAllocateDescriptorSets *OAllocds, PFN_vkUpdateDescriptorSets *OUpds,
    PFN_vkCreateBuffer *OBuf, PFN_vkDestroyBuffer *OBufd,
    PFN_vkGetBufferMemoryRequirements *OGbmr, PFN_vkAllocateMemory *OAllocm,
    PFN_vkFreeMemory *OFreem, PFN_vkBindBufferMemory *OBind,
    PFN_vkMapMemory *OMap, PFN_vkUnmapMemory *OUnmap,
    PFN_vkInvalidateMappedMemoryRanges *OInv, PFN_vkCreateCommandPool *OCcp,
    PFN_vkDestroyCommandPool *ODcp, PFN_vkAllocateCommandBuffers *OAcb,
    PFN_vkFreeCommandBuffers *OFcb, PFN_vkBeginCommandBuffer *OBcb,
    PFN_vkEndCommandBuffer *OEcb, PFN_vkCmdBindPipeline *OCbp,
    PFN_vkCmdBindDescriptorSets *OCbds, PFN_vkCmdPushConstants *OCpc,
    PFN_vkCmdDispatch *OCd, PFN_vkCmdPipelineBarrier *OCpb,
    PFN_vkCreateFence *OCf, PFN_vkDestroyFence *ODf, PFN_vkQueueSubmit *OQs,
    PFN_vkWaitForFences *OWff, PFN_vkResetFences *ORf) {
  *ODpool = YONA_VULKAN_DEVICE_PROCEDURE(vkCreateDescriptorPool);
  *ODpoold = YONA_VULKAN_DEVICE_PROCEDURE(vkDestroyDescriptorPool);
  *OAllocds = YONA_VULKAN_DEVICE_PROCEDURE(vkAllocateDescriptorSets);
  *OUpds = YONA_VULKAN_DEVICE_PROCEDURE(vkUpdateDescriptorSets);
  *OBuf = YONA_VULKAN_DEVICE_PROCEDURE(vkCreateBuffer);
  *OBufd = YONA_VULKAN_DEVICE_PROCEDURE(vkDestroyBuffer);
  *OGbmr = YONA_VULKAN_DEVICE_PROCEDURE(vkGetBufferMemoryRequirements);
  *OAllocm = YONA_VULKAN_DEVICE_PROCEDURE(vkAllocateMemory);
  *OFreem = YONA_VULKAN_DEVICE_PROCEDURE(vkFreeMemory);
  *OBind = YONA_VULKAN_DEVICE_PROCEDURE(vkBindBufferMemory);
  *OMap = YONA_VULKAN_DEVICE_PROCEDURE(vkMapMemory);
  *OUnmap = YONA_VULKAN_DEVICE_PROCEDURE(vkUnmapMemory);
  *OInv = YONA_VULKAN_DEVICE_PROCEDURE(vkInvalidateMappedMemoryRanges);
  *OCcp = YONA_VULKAN_DEVICE_PROCEDURE(vkCreateCommandPool);
  *ODcp = YONA_VULKAN_DEVICE_PROCEDURE(vkDestroyCommandPool);
  *OAcb = YONA_VULKAN_DEVICE_PROCEDURE(vkAllocateCommandBuffers);
  *OFcb = YONA_VULKAN_DEVICE_PROCEDURE(vkFreeCommandBuffers);
  *OBcb = YONA_VULKAN_DEVICE_PROCEDURE(vkBeginCommandBuffer);
  *OEcb = YONA_VULKAN_DEVICE_PROCEDURE(vkEndCommandBuffer);
  *OCbp = YONA_VULKAN_DEVICE_PROCEDURE(vkCmdBindPipeline);
  *OCbds = YONA_VULKAN_DEVICE_PROCEDURE(vkCmdBindDescriptorSets);
  *OCpc = YONA_VULKAN_DEVICE_PROCEDURE(vkCmdPushConstants);
  *OCd = YONA_VULKAN_DEVICE_PROCEDURE(vkCmdDispatch);
  *OCpb = YONA_VULKAN_DEVICE_PROCEDURE(vkCmdPipelineBarrier);
  *OCf = YONA_VULKAN_DEVICE_PROCEDURE(vkCreateFence);
  *ODf = YONA_VULKAN_DEVICE_PROCEDURE(vkDestroyFence);
  *OQs = YONA_VULKAN_DEVICE_PROCEDURE(vkQueueSubmit);
  *OWff = YONA_VULKAN_DEVICE_PROCEDURE(vkWaitForFences);
  *ORf = YONA_VULKAN_DEVICE_PROCEDURE(vkResetFences);
  return *ODpool && *ODpoold && *OAllocds && *OUpds && *OBuf && *OBufd &&
         *OGbmr && *OAllocm && *OFreem && *OBind && *OMap && *OUnmap && *OCcp &&
         *ODcp && *OAcb && *OFcb && *OBcb && *OEcb && *OCbp && *OCbds &&
         *OCpc && *OCd && *OCpb && *OCf && *ODf && *OQs && *OWff && *ORf;
}

/** Prefer A memory type that is device-local and not host-visible (discrete
 * VRAM). */
static int yonaVulkanMemoryTypeDeviceLocalOnly(uint32_t TypeBits,
                                               uint32_t *OutIndex) {
  VkPhysicalDeviceMemoryProperties Mp;
  YonaVulkanGetPhysicalDeviceMemoryProperties(YonaVulkanPhysicalDevice, &Mp);
  for (uint32_t I = 0; I < Mp.memoryTypeCount; I++) {
    if (!(TypeBits & (1u << I)))
      continue;
    VkMemoryPropertyFlags F = Mp.memoryTypes[I].propertyFlags;
    if ((F & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
        !(F & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
      *OutIndex = I;
      return 1;
    }
  }
  return 0;
}

static int yonaVulkanForceHostSsbo(void) {
  const char *S = getenv("YONA_GPU_VULKAN_HOST_SSBO");
  return S && strcmp(S, "1") == 0;
}

/** Returns 1 and fills device and staging buffers when A discrete device-local
 * heap exists. */
static int yonaVulkanTryDevStgPair(
    PFN_vkCreateBuffer VkCreateBuffer, PFN_vkDestroyBuffer VkDestroyBuffer,
    PFN_vkGetBufferMemoryRequirements VkGetBufferMemoryRequirements,
    PFN_vkAllocateMemory VkAllocateMemory, PFN_vkFreeMemory VkFreeMemory,
    PFN_vkBindBufferMemory VkBindBufferMemory, VkDeviceSize Nbytes,
    VkBuffer *DevBuf, VkDeviceMemory *DevMem, VkBuffer *StgBuf,
    VkDeviceMemory *StgMem) {
  *DevBuf = *StgBuf = VK_NULL_HANDLE;
  *DevMem = *StgMem = VK_NULL_HANDLE;
  VkBufferCreateInfo Bd = {0};
  Bd.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  Bd.size = Nbytes;
  Bd.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
             VK_BUFFER_USAGE_TRANSFER_DST_BIT |
             VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  Bd.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VkResult R = VkCreateBuffer(YonaVulkanDevice, &Bd, NULL, DevBuf);
  if (R != VK_SUCCESS)
    return 0;
  VkMemoryRequirements RqDev;
  VkGetBufferMemoryRequirements(YonaVulkanDevice, *DevBuf, &RqDev);
  uint32_t MtDev = 0;
  if (!yonaVulkanMemoryTypeDeviceLocalOnly(RqDev.memoryTypeBits, &MtDev))
    goto FailDev;
  VkMemoryAllocateInfo MaiD = {0};
  MaiD.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  MaiD.allocationSize = RqDev.size;
  MaiD.memoryTypeIndex = MtDev;
  R = VkAllocateMemory(YonaVulkanDevice, &MaiD, NULL, DevMem);
  if (R != VK_SUCCESS)
    goto FailDev;
  R = VkBindBufferMemory(YonaVulkanDevice, *DevBuf, *DevMem, 0);
  if (R != VK_SUCCESS)
    goto FailDevMem;

  VkBufferCreateInfo Bs = {0};
  Bs.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  Bs.size = Nbytes;
  Bs.usage =
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  Bs.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  R = VkCreateBuffer(YonaVulkanDevice, &Bs, NULL, StgBuf);
  if (R != VK_SUCCESS)
    goto FailDevMem;
  VkMemoryRequirements RqSt;
  VkGetBufferMemoryRequirements(YonaVulkanDevice, *StgBuf, &RqSt);
  uint32_t MtSt = 0;
  if (yonaVulkanPickMemoryType(RqSt.memoryTypeBits,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               &MtSt) != 0)
    goto FailStgBuf;
  VkMemoryAllocateInfo MaiS = {0};
  MaiS.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  MaiS.allocationSize = RqSt.size;
  MaiS.memoryTypeIndex = MtSt;
  R = VkAllocateMemory(YonaVulkanDevice, &MaiS, NULL, StgMem);
  if (R != VK_SUCCESS)
    goto FailStgBuf;
  R = VkBindBufferMemory(YonaVulkanDevice, *StgBuf, *StgMem, 0);
  if (R != VK_SUCCESS)
    goto FailStgMem;
  return 1;

FailStgMem:
  VkFreeMemory(YonaVulkanDevice, *StgMem, NULL);
  *StgMem = VK_NULL_HANDLE;
FailStgBuf:
  VkDestroyBuffer(YonaVulkanDevice, *StgBuf, NULL);
  *StgBuf = VK_NULL_HANDLE;
FailDevMem:
  VkFreeMemory(YonaVulkanDevice, *DevMem, NULL);
  *DevMem = VK_NULL_HANDLE;
FailDev:
  VkDestroyBuffer(YonaVulkanDevice, *DevBuf, NULL);
  *DevBuf = VK_NULL_HANDLE;
  return 0;
}

static VkResult yonaVulkanCreateDeviceLocalSsbo(
    PFN_vkCreateBuffer VkCreateBuffer, PFN_vkDestroyBuffer VkDestroyBuffer,
    PFN_vkGetBufferMemoryRequirements VkGetBufferMemoryRequirements,
    PFN_vkAllocateMemory VkAllocateMemory, PFN_vkFreeMemory VkFreeMemory,
    PFN_vkBindBufferMemory VkBindBufferMemory, VkDeviceSize Nbytes,
    VkBuffer *OutBuf, VkDeviceMemory *OutMem) {
  *OutBuf = VK_NULL_HANDLE;
  *OutMem = VK_NULL_HANDLE;
  VkBufferCreateInfo Bd = {0};
  Bd.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  Bd.size = Nbytes;
  Bd.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
             VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
             VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  Bd.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VkResult R = VkCreateBuffer(YonaVulkanDevice, &Bd, NULL, OutBuf);
  if (R != VK_SUCCESS)
    return R;
  VkMemoryRequirements Rq;
  VkGetBufferMemoryRequirements(YonaVulkanDevice, *OutBuf, &Rq);
  uint32_t Mt = 0;
  if (!yonaVulkanMemoryTypeDeviceLocalOnly(Rq.memoryTypeBits, &Mt)) {
    VkDestroyBuffer(YonaVulkanDevice, *OutBuf, NULL);
    *OutBuf = VK_NULL_HANDLE;
    return VK_ERROR_FEATURE_NOT_PRESENT;
  }
  VkMemoryAllocateInfo Mai = {0};
  Mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  Mai.allocationSize = Rq.size;
  Mai.memoryTypeIndex = Mt;
  R = VkAllocateMemory(YonaVulkanDevice, &Mai, NULL, OutMem);
  if (R != VK_SUCCESS)
    goto BadBuf;
  R = VkBindBufferMemory(YonaVulkanDevice, *OutBuf, *OutMem, 0);
  if (R != VK_SUCCESS)
    goto BadMem;
  return VK_SUCCESS;
BadMem:
  VkFreeMemory(YonaVulkanDevice, *OutMem, NULL);
  *OutMem = VK_NULL_HANDLE;
BadBuf:
  VkDestroyBuffer(YonaVulkanDevice, *OutBuf, NULL);
  *OutBuf = VK_NULL_HANDLE;
  return R;
}

static void
yonaVulkanBarrierBuffer(PFN_vkCmdPipelineBarrier VkCmdPipelineBarrier,
                        VkCommandBuffer Cmd, VkBuffer Buf, VkAccessFlags SrcAcc,
                        VkAccessFlags DstAcc, VkPipelineStageFlags SrcSt,
                        VkPipelineStageFlags DstSt) {
  VkBufferMemoryBarrier B = {0};
  B.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  B.srcAccessMask = SrcAcc;
  B.dstAccessMask = DstAcc;
  B.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  B.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  B.buffer = Buf;
  B.offset = 0;
  B.size = VK_WHOLE_SIZE;
  VkCmdPipelineBarrier(Cmd, SrcSt, DstSt, 0, 0, NULL, 1, &B, 0, NULL);
}

static int yonaVulkanRunSimpleMap(const char *FailTag, VkResult (*Ensure)(void),
                                  YonaVulkanSimplePipeline *Pipe,
                                  int64_t Scalar, int64_t *Arr, int64_t **Out,
                                  int UseI32) {
  *Out = NULL;
  VkResult R = VK_SUCCESS;
  int UseStaging = 0;
  int32_t *Packed = NULL;

  PFN_vkCreateDescriptorPool VkCreateDescriptorPool;
  PFN_vkDestroyDescriptorPool VkDestroyDescriptorPool;
  PFN_vkAllocateDescriptorSets VkAllocateDescriptorSets;
  PFN_vkUpdateDescriptorSets VkUpdateDescriptorSets;
  PFN_vkCreateBuffer VkCreateBuffer;
  PFN_vkDestroyBuffer VkDestroyBuffer;
  PFN_vkGetBufferMemoryRequirements VkGetBufferMemoryRequirements;
  PFN_vkAllocateMemory VkAllocateMemory;
  PFN_vkFreeMemory VkFreeMemory;
  PFN_vkBindBufferMemory VkBindBufferMemory;
  PFN_vkMapMemory VkMapMemory;
  PFN_vkUnmapMemory VkUnmapMemory;
  PFN_vkInvalidateMappedMemoryRanges VkInvalidateMappedMemoryRanges;
  PFN_vkCreateCommandPool VkCreateCommandPool;
  PFN_vkDestroyCommandPool VkDestroyCommandPool;
  PFN_vkAllocateCommandBuffers VkAllocateCommandBuffers;
  PFN_vkFreeCommandBuffers VkFreeCommandBuffers;
  PFN_vkBeginCommandBuffer VkBeginCommandBuffer;
  PFN_vkEndCommandBuffer VkEndCommandBuffer;
  PFN_vkCmdBindPipeline VkCmdBindPipeline;
  PFN_vkCmdBindDescriptorSets VkCmdBindDescriptorSets;
  PFN_vkCmdPushConstants VkCmdPushConstants;
  PFN_vkCmdDispatch VkCmdDispatch;
  PFN_vkCmdPipelineBarrier VkCmdPipelineBarrier;
  PFN_vkCreateFence VkCreateFence;
  PFN_vkDestroyFence VkDestroyFence;
  PFN_vkQueueSubmit VkQueueSubmit;
  PFN_vkWaitForFences VkWaitForFences;
  PFN_vkResetFences VkResetFences;
  PFN_vkCmdCopyBuffer VkCmdCopyBuffer =
      YONA_VULKAN_DEVICE_PROCEDURE(vkCmdCopyBuffer);

  if (!yonaVulkanLoadDispatchFunctions(
          &VkCreateDescriptorPool, &VkDestroyDescriptorPool,
          &VkAllocateDescriptorSets, &VkUpdateDescriptorSets, &VkCreateBuffer,
          &VkDestroyBuffer, &VkGetBufferMemoryRequirements, &VkAllocateMemory,
          &VkFreeMemory, &VkBindBufferMemory, &VkMapMemory, &VkUnmapMemory,
          &VkInvalidateMappedMemoryRanges, &VkCreateCommandPool,
          &VkDestroyCommandPool, &VkAllocateCommandBuffers,
          &VkFreeCommandBuffers, &VkBeginCommandBuffer, &VkEndCommandBuffer,
          &VkCmdBindPipeline, &VkCmdBindDescriptorSets, &VkCmdPushConstants,
          &VkCmdDispatch, &VkCmdPipelineBarrier, &VkCreateFence,
          &VkDestroyFence, &VkQueueSubmit, &VkWaitForFences, &VkResetFences)) {
    yonaVulkanNoteCopy(
        "gpu: vkGetDeviceProcAddr returned null for A required entry point");
    return 0;
  }

  R = Ensure();
  if (R != VK_SUCCESS) {
    char B[120];
    snprintf(B, sizeof B, "%s: pipeline ensure VkResult=%d", FailTag, (int)R);
    yonaVulkanNoteCopy(B);
    return 0;
  }

  int64_t Len = Arr[0];
  if (UseI32) {
    Packed = (int32_t *)malloc((size_t)Len * sizeof(int32_t));
    if (!Packed) {
      yonaVulkanNoteCopy("gpu: malloc Failed packing i32 column");
      return 0;
    }
    for (int64_t I = 0; I < Len; I++)
      Packed[I] = (int32_t)Arr[1 + I];
  }
  VkDeviceSize Nbytes =
      (VkDeviceSize)((size_t)Len *
                     (UseI32 ? sizeof(int32_t) : sizeof(int64_t)));
  const void *HostSrc = UseI32 ? (const void *)Packed : (const void *)(Arr + 1);

  VkDescriptorPool Dpool = VK_NULL_HANDLE;
  VkDescriptorSet Dset = VK_NULL_HANDLE;
  VkBuffer Buf = VK_NULL_HANDLE;
  VkDeviceMemory Mem = VK_NULL_HANDLE;
  VkBuffer BufDev = VK_NULL_HANDLE;
  VkBuffer BufStg = VK_NULL_HANDLE;
  VkDeviceMemory MemDev = VK_NULL_HANDLE;
  VkDeviceMemory MemStg = VK_NULL_HANDLE;
  VkCommandPool Cpool = VK_NULL_HANDLE;
  VkCommandBuffer Cmd = VK_NULL_HANDLE;
  VkFence Fence = VK_NULL_HANDLE;
  void *Mapped = NULL;

  VkDescriptorPoolSize Dps = {0};
  Dps.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Dps.descriptorCount = 1;

  VkDescriptorPoolCreateInfo Dpci = {0};
  Dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  Dpci.maxSets = 1;
  Dpci.poolSizeCount = 1;
  Dpci.pPoolSizes = &Dps;
  R = VkCreateDescriptorPool(YonaVulkanDevice, &Dpci, NULL, &Dpool);
  if (R != VK_SUCCESS)
    goto Failure;

  VkDescriptorSetAllocateInfo Dsai = {0};
  Dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  Dsai.descriptorPool = Dpool;
  Dsai.descriptorSetCount = 1;
  Dsai.pSetLayouts = &Pipe->DescriptorSetLayout;
  R = VkAllocateDescriptorSets(YonaVulkanDevice, &Dsai, &Dset);
  if (R != VK_SUCCESS)
    goto Failure;

  if (!yonaVulkanForceHostSsbo() && VkCmdCopyBuffer) {
    VkBufferCreateInfo Bd = {0};
    Bd.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    Bd.size = Nbytes;
    Bd.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
               VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    Bd.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    R = VkCreateBuffer(YonaVulkanDevice, &Bd, NULL, &BufDev);
    if (R == VK_SUCCESS) {
      VkMemoryRequirements RqDev;
      VkGetBufferMemoryRequirements(YonaVulkanDevice, BufDev, &RqDev);
      uint32_t MtDev = 0;
      if (yonaVulkanMemoryTypeDeviceLocalOnly(RqDev.memoryTypeBits, &MtDev)) {
        VkMemoryAllocateInfo MaiD = {0};
        MaiD.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        MaiD.allocationSize = RqDev.size;
        MaiD.memoryTypeIndex = MtDev;
        R = VkAllocateMemory(YonaVulkanDevice, &MaiD, NULL, &MemDev);
        if (R == VK_SUCCESS) {
          R = VkBindBufferMemory(YonaVulkanDevice, BufDev, MemDev, 0);
          if (R != VK_SUCCESS) {
            VkFreeMemory(YonaVulkanDevice, MemDev, NULL);
            MemDev = VK_NULL_HANDLE;
          }
        }
        if (R == VK_SUCCESS) {
          VkBufferCreateInfo Bs = {0};
          Bs.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
          Bs.size = Nbytes;
          Bs.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT;
          Bs.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
          R = VkCreateBuffer(YonaVulkanDevice, &Bs, NULL, &BufStg);
          if (R == VK_SUCCESS) {
            VkMemoryRequirements RqSt;
            VkGetBufferMemoryRequirements(YonaVulkanDevice, BufStg, &RqSt);
            uint32_t MtSt = 0;
            if (yonaVulkanPickMemoryType(
                    RqSt.memoryTypeBits,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    &MtSt) == 0) {
              VkMemoryAllocateInfo MaiS = {0};
              MaiS.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
              MaiS.allocationSize = RqSt.size;
              MaiS.memoryTypeIndex = MtSt;
              R = VkAllocateMemory(YonaVulkanDevice, &MaiS, NULL, &MemStg);
              if (R == VK_SUCCESS) {
                R = VkBindBufferMemory(YonaVulkanDevice, BufStg, MemStg, 0);
                if (R != VK_SUCCESS) {
                  VkFreeMemory(YonaVulkanDevice, MemStg, NULL);
                  MemStg = VK_NULL_HANDLE;
                }
              }
              if (R == VK_SUCCESS)
                UseStaging = 1;
            }
          }
        }
      }
    }
    if (!UseStaging) {
      if (BufStg != VK_NULL_HANDLE) {
        VkDestroyBuffer(YonaVulkanDevice, BufStg, NULL);
        BufStg = VK_NULL_HANDLE;
      }
      if (MemStg != VK_NULL_HANDLE) {
        VkFreeMemory(YonaVulkanDevice, MemStg, NULL);
        MemStg = VK_NULL_HANDLE;
      }
      if (MemDev != VK_NULL_HANDLE) {
        VkFreeMemory(YonaVulkanDevice, MemDev, NULL);
        MemDev = VK_NULL_HANDLE;
      }
      if (BufDev != VK_NULL_HANDLE) {
        VkDestroyBuffer(YonaVulkanDevice, BufDev, NULL);
        BufDev = VK_NULL_HANDLE;
      }
    }
  }

  if (!UseStaging) {
    VkBufferCreateInfo Bci = {0};
    Bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    Bci.size = Nbytes;
    Bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    Bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    R = VkCreateBuffer(YonaVulkanDevice, &Bci, NULL, &Buf);
    if (R != VK_SUCCESS)
      goto Failure;

    VkMemoryRequirements Req;
    VkGetBufferMemoryRequirements(YonaVulkanDevice, Buf, &Req);
    uint32_t Mt = 0;
    if (yonaVulkanPickMemoryType(Req.memoryTypeBits,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 &Mt) != 0) {
      yonaVulkanNoteCopy(
          "gpu: no memory type with HOST_VISIBLE|HOST_COHERENT for SSBO");
      goto Failure;
    }

    VkMemoryAllocateInfo Mai = {0};
    Mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    Mai.allocationSize = Req.size;
    Mai.memoryTypeIndex = Mt;
    R = VkAllocateMemory(YonaVulkanDevice, &Mai, NULL, &Mem);
    if (R != VK_SUCCESS)
      goto Failure;
    R = VkBindBufferMemory(YonaVulkanDevice, Buf, Mem, 0);
    if (R != VK_SUCCESS)
      goto Failure;

    R = VkMapMemory(YonaVulkanDevice, Mem, 0, Nbytes, 0, &Mapped);
    if (R != VK_SUCCESS)
      goto Failure;
    memcpy(Mapped, HostSrc, (size_t)Nbytes);
  } else {
    R = VkMapMemory(YonaVulkanDevice, MemStg, 0, Nbytes, 0, &Mapped);
    if (R != VK_SUCCESS)
      goto Failure;
    memcpy(Mapped, HostSrc, (size_t)Nbytes);
  }

  VkDescriptorBufferInfo Dbi = {0};
  Dbi.buffer = UseStaging ? BufDev : Buf;
  Dbi.offset = 0;
  Dbi.range = Nbytes;

  VkWriteDescriptorSet W = {0};
  W.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  W.dstSet = Dset;
  W.dstBinding = 0;
  W.descriptorCount = 1;
  W.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  W.pBufferInfo = &Dbi;
  VkUpdateDescriptorSets(YonaVulkanDevice, 1, &W, 0, NULL);

  VkCommandPoolCreateInfo Cpci0 = {0};
  Cpci0.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  Cpci0.queueFamilyIndex = YonaVulkanQueueFamily;
  Cpci0.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  R = VkCreateCommandPool(YonaVulkanDevice, &Cpci0, NULL, &Cpool);
  if (R != VK_SUCCESS)
    goto Failure;

  VkCommandBufferAllocateInfo Cbai = {0};
  Cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  Cbai.commandPool = Cpool;
  Cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  Cbai.commandBufferCount = 1;
  R = VkAllocateCommandBuffers(YonaVulkanDevice, &Cbai, &Cmd);
  if (R != VK_SUCCESS)
    goto Failure;

  VkFenceCreateInfo Fci = {0};
  Fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  R = VkCreateFence(YonaVulkanDevice, &Fci, NULL, &Fence);
  if (R != VK_SUCCESS)
    goto Failure;

  VkCommandBufferBeginInfo Bi = {0};
  Bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  Bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  R = VkBeginCommandBuffer(Cmd, &Bi);
  if (R != VK_SUCCESS)
    goto Failure;

  if (UseStaging) {
    VkBufferCopy Region = {0};
    Region.size = Nbytes;
    yonaVulkanBarrierBuffer(
        VkCmdPipelineBarrier, Cmd, BufStg, VK_ACCESS_HOST_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkCmdCopyBuffer(Cmd, BufStg, BufDev, 1, &Region);
    yonaVulkanBarrierBuffer(
        VkCmdPipelineBarrier, Cmd, BufDev, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  }

  VkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, Pipe->Pipeline);
  VkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          Pipe->PipelineLayout, 0, 1, &Dset, 0, NULL);

  char Pc[16];
  uint32_t Ulen = (uint32_t)Len;
  uint32_t PushBytes;
  if (UseI32) {
    int32_t S32 = (int32_t)Scalar;
    memcpy(Pc, &S32, sizeof(int32_t));
    memcpy(Pc + sizeof(int32_t), &Ulen, sizeof(uint32_t));
    PushBytes = 8;
  } else {
    memcpy(Pc, &Scalar, sizeof(int64_t));
    memcpy(Pc + sizeof(int64_t), &Ulen, sizeof(uint32_t));
    PushBytes = 12;
  }
  VkCmdPushConstants(Cmd, Pipe->PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                     PushBytes, Pc);

  uint32_t Groups = ((uint32_t)Len + 63u) / 64u;
  VkCmdDispatch(Cmd, Groups, 1, 1);

  if (UseStaging) {
    VkBufferCopy Region = {0};
    Region.size = Nbytes;
    yonaVulkanBarrierBuffer(
        VkCmdPipelineBarrier, Cmd, BufDev, VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);
    yonaVulkanBarrierBuffer(
        VkCmdPipelineBarrier, Cmd, BufStg, VK_ACCESS_TRANSFER_READ_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkCmdCopyBuffer(Cmd, BufDev, BufStg, 1, &Region);
    yonaVulkanBarrierBuffer(
        VkCmdPipelineBarrier, Cmd, BufStg, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_HOST_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT);
  } else {
    VkMemoryBarrier Mb = {0};
    Mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    Mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    Mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    VkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &Mb, 0, NULL, 0,
                         NULL);
  }

  R = VkEndCommandBuffer(Cmd);
  if (R != VK_SUCCESS)
    goto Failure;

  VkSubmitInfo Si = {0};
  Si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  Si.commandBufferCount = 1;
  Si.pCommandBuffers = &Cmd;
  R = VkQueueSubmit(YonaVulkanQueue, 1, &Si, Fence);
  if (R != VK_SUCCESS)
    goto Failure;

  R = VkWaitForFences(YonaVulkanDevice, 1, &Fence, VK_TRUE, UINT64_MAX);
  if (R != VK_SUCCESS)
    goto Failure;

  if (VkInvalidateMappedMemoryRanges) {
    VkMappedMemoryRange Inv = {0};
    Inv.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    Inv.memory = UseStaging ? MemStg : Mem;
    Inv.offset = 0;
    Inv.size = Nbytes;
    VkInvalidateMappedMemoryRanges(YonaVulkanDevice, 1, &Inv);
  }

  int64_t *Result = YonaRuntimeIntArrayAllocate(Len);
  if (!Result) {
    yonaVulkanNoteCopy("gpu: YonaRuntimeIntArrayAllocate Failed");
    goto Failure;
  }
  if (UseI32) {
    const int32_t *Out32 = (const int32_t *)Mapped;
    for (int64_t I = 0; I < Len; I++)
      Result[1 + I] = (int64_t)Out32[I];
  } else {
    memcpy(Result + 1, Mapped, (size_t)Nbytes);
  }

  VkUnmapMemory(YonaVulkanDevice, UseStaging ? MemStg : Mem);
  Mapped = NULL;

  VkDestroyFence(YonaVulkanDevice, Fence, NULL);
  Fence = VK_NULL_HANDLE;
  VkFreeCommandBuffers(YonaVulkanDevice, Cpool, 1, &Cmd);
  Cmd = VK_NULL_HANDLE;
  VkDestroyCommandPool(YonaVulkanDevice, Cpool, NULL);
  Cpool = VK_NULL_HANDLE;
  if (UseStaging) {
    VkDestroyBuffer(YonaVulkanDevice, BufStg, NULL);
    BufStg = VK_NULL_HANDLE;
    VkFreeMemory(YonaVulkanDevice, MemStg, NULL);
    MemStg = VK_NULL_HANDLE;
    VkDestroyBuffer(YonaVulkanDevice, BufDev, NULL);
    BufDev = VK_NULL_HANDLE;
    VkFreeMemory(YonaVulkanDevice, MemDev, NULL);
    MemDev = VK_NULL_HANDLE;
  } else {
    VkDestroyBuffer(YonaVulkanDevice, Buf, NULL);
    Buf = VK_NULL_HANDLE;
    VkFreeMemory(YonaVulkanDevice, Mem, NULL);
    Mem = VK_NULL_HANDLE;
  }
  VkDestroyDescriptorPool(YonaVulkanDevice, Dpool, NULL);
  Dpool = VK_NULL_HANDLE;

  free(Packed);
  *Out = Result;
  return 1;

Failure:
  free(Packed);
  if (!YonaVulkanLastNote[0]) {
    char B[120];
    snprintf(B, sizeof B, "%s: Vulkan failure VkResult=%d", FailTag, (int)R);
    yonaVulkanNoteCopy(B);
  }
  if (Mapped)
    VkUnmapMemory(YonaVulkanDevice, UseStaging ? MemStg : Mem);
  if (Fence != VK_NULL_HANDLE)
    VkDestroyFence(YonaVulkanDevice, Fence, NULL);
  if (Cmd != VK_NULL_HANDLE && Cpool != VK_NULL_HANDLE)
    VkFreeCommandBuffers(YonaVulkanDevice, Cpool, 1, &Cmd);
  if (Cpool != VK_NULL_HANDLE)
    VkDestroyCommandPool(YonaVulkanDevice, Cpool, NULL);
  if (BufStg != VK_NULL_HANDLE)
    VkDestroyBuffer(YonaVulkanDevice, BufStg, NULL);
  if (MemStg != VK_NULL_HANDLE)
    VkFreeMemory(YonaVulkanDevice, MemStg, NULL);
  if (BufDev != VK_NULL_HANDLE)
    VkDestroyBuffer(YonaVulkanDevice, BufDev, NULL);
  if (MemDev != VK_NULL_HANDLE)
    VkFreeMemory(YonaVulkanDevice, MemDev, NULL);
  if (Buf != VK_NULL_HANDLE)
    VkDestroyBuffer(YonaVulkanDevice, Buf, NULL);
  if (Mem != VK_NULL_HANDLE)
    VkFreeMemory(YonaVulkanDevice, Mem, NULL);
  if (Dpool != VK_NULL_HANDLE)
    VkDestroyDescriptorPool(YonaVulkanDevice, Dpool, NULL);
  return 0;
}

static int gpuVulkanTryMapAddInt64Impl(int64_t Delta, int64_t *Arr,
                                       int64_t **Out) {
  *Out = NULL;
  if (!yonaVulkanEnvMapAdd()) {
    yonaVulkanNoteCopy(
        "mapadd: set YONA_GPU_VULKAN_MAPADD=1 or YONA_GPU_VULKAN_COMPUTE=1");
    return 0;
  }
  if (!yonaVulkanCommonPrecheck(Arr, "mapadd"))
    return 0;

  int64_t MinLen = readMinimumLength("YONA_GPU_VULKAN_MIN_LEN",
                                     "YONA_GPU_VULKAN_MAPADD_MIN_LEN");
  int64_t Len = Arr[0];
  if (Len < MinLen) {
    yonaVulkanNoteCopy(
        "mapadd: IntArray shorter than configured GPU min length");
    return 0;
  }
  if (Len > (int64_t)0x7fffffff) {
    yonaVulkanNoteCopy("mapadd: IntArray length exceeds supported range");
    return 0;
  }

  int UseI32 = yonaVulkanPreferI32();
  if (UseI32 && !yonaVulkanI32MapAddFits(Arr, Delta)) {
    yonaVulkanNoteCopy(
        "mapadd: values exceed int32; GPU i32 path skipped (no shaderInt64)");
    return 0;
  }

  int Ok = 0;
  yonaVulkanComputeSubmitLock();
  if (UseI32)
    Ok = yonaVulkanRunSimpleMap("mapadd", yonaVulkanComputeEnsureMapAddI32Pipe,
                                yonaVulkanComputeMapAddI32Pipe(), Delta, Arr,
                                Out, 1);
  else
    Ok = yonaVulkanRunSimpleMap("mapadd", yonaVulkanComputeEnsureMapAddPipe,
                                yonaVulkanComputeMapAddPipe(), Delta, Arr, Out,
                                0);
  yonaVulkanComputeSubmitUnlock();
  return Ok;
}

static int gpuVulkanTryMapMulInt64Impl(int64_t Factor, int64_t *Arr,
                                       int64_t **Out) {
  *Out = NULL;
  if (!yonaVulkanEnvMapMul()) {
    yonaVulkanNoteCopy(
        "mapmul: set YONA_GPU_VULKAN_MAPMUL=1 or YONA_GPU_VULKAN_COMPUTE=1");
    return 0;
  }
  if (!yonaVulkanCommonPrecheck(Arr, "mapmul"))
    return 0;

  int64_t MinLen = readMinimumLength("YONA_GPU_VULKAN_MIN_LEN",
                                     "YONA_GPU_VULKAN_MAPMUL_MIN_LEN");
  int64_t Len = Arr[0];
  if (Len < MinLen) {
    yonaVulkanNoteCopy(
        "mapmul: IntArray shorter than configured GPU min length");
    return 0;
  }
  if (Len > (int64_t)0x7fffffff) {
    yonaVulkanNoteCopy("mapmul: IntArray length exceeds supported range");
    return 0;
  }

  int UseI32 = yonaVulkanPreferI32();
  if (UseI32 && !yonaVulkanI32MapMulFits(Arr, Factor)) {
    yonaVulkanNoteCopy(
        "mapmul: values exceed int32; GPU i32 path skipped (no shaderInt64)");
    return 0;
  }

  int Ok = 0;
  yonaVulkanComputeSubmitLock();
  if (UseI32)
    Ok = yonaVulkanRunSimpleMap("mapmul", yonaVulkanComputeEnsureMapMulI32Pipe,
                                yonaVulkanComputeMapMulI32Pipe(), Factor, Arr,
                                Out, 1);
  else
    Ok = yonaVulkanRunSimpleMap("mapmul", yonaVulkanComputeEnsureMapMulPipe,
                                yonaVulkanComputeMapMulPipe(), Factor, Arr, Out,
                                0);
  yonaVulkanComputeSubmitUnlock();
  return Ok;
}

static int gpuVulkanTryMapSquareInt64Impl(int64_t *Arr, int64_t **Out) {
  *Out = NULL;
  if (!yonaVulkanEnvMapMul()) {
    yonaVulkanNoteCopy(
        "mapsquare: set YONA_GPU_VULKAN_MAPMUL=1 or YONA_GPU_VULKAN_COMPUTE=1");
    return 0;
  }
  if (!yonaVulkanCommonPrecheck(Arr, "mapsquare"))
    return 0;

  int64_t MinLen = readMinimumLength("YONA_GPU_VULKAN_MIN_LEN",
                                     "YONA_GPU_VULKAN_MAPMUL_MIN_LEN");
  int64_t Len = Arr[0];
  if (Len < MinLen) {
    yonaVulkanNoteCopy(
        "mapsquare: IntArray shorter than configured GPU min length");
    return 0;
  }
  if (Len > (int64_t)0x7fffffff) {
    yonaVulkanNoteCopy("mapsquare: IntArray length exceeds supported range");
    return 0;
  }

  int UseI32 = yonaVulkanPreferI32();
  if (UseI32 && !yonaVulkanI32MapSquareFits(Arr)) {
    yonaVulkanNoteCopy(
        "mapsquare: values exceed int32; GPU i32 path skipped (no "
        "shaderInt64)");
    return 0;
  }

  int Ok = 0;
  yonaVulkanComputeSubmitLock();
  if (UseI32)
    Ok = yonaVulkanRunSimpleMap(
        "mapsquare", yonaVulkanComputeEnsureMapSquareI32Pipe,
        yonaVulkanComputeMapSquareI32Pipe(), 0, Arr, Out, 1);
  else
    Ok = yonaVulkanRunSimpleMap(
        "mapsquare", yonaVulkanComputeEnsureMapSquarePipe,
        yonaVulkanComputeMapSquarePipe(), 0, Arr, Out, 0);
  yonaVulkanComputeSubmitUnlock();
  return Ok;
}

static int gpuVulkanTryReduceSumInt64Impl(int64_t *Arr, int64_t *OutSum) {
  *OutSum = 0;
  if (!yonaVulkanEnvReduce()) {
    yonaVulkanNoteCopy(
        "reduce: set YONA_GPU_VULKAN_REDUCE=1 or YONA_GPU_VULKAN_COMPUTE=1");
    return 0;
  }
  if (!yonaVulkanCommonPrecheck(Arr, "reduce"))
    return 0;

  int64_t MinLen = readMinimumLength("YONA_GPU_VULKAN_MIN_LEN",
                                     "YONA_GPU_VULKAN_REDUCE_MIN_LEN");
  int64_t Len = Arr[0];
  if (Len < MinLen) {
    yonaVulkanNoteCopy(
        "reduce: IntArray shorter than configured GPU min length");
    return 0;
  }
  if (Len > (int64_t)0x7fffffff) {
    yonaVulkanNoteCopy("reduce: IntArray length exceeds supported range");
    return 0;
  }

  int UseI32 = yonaVulkanPreferI32();
  if (UseI32 && !yonaVulkanI32ReduceFits(Arr)) {
    yonaVulkanNoteCopy(
        "reduce: values exceed int32; GPU i32 path skipped (no shaderInt64)");
    return 0;
  }

  PFN_vkCreateDescriptorPool VkCreateDescriptorPool;
  PFN_vkDestroyDescriptorPool VkDestroyDescriptorPool;
  PFN_vkAllocateDescriptorSets VkAllocateDescriptorSets;
  PFN_vkUpdateDescriptorSets VkUpdateDescriptorSets;
  PFN_vkCreateBuffer VkCreateBuffer;
  PFN_vkDestroyBuffer VkDestroyBuffer;
  PFN_vkGetBufferMemoryRequirements VkGetBufferMemoryRequirements;
  PFN_vkAllocateMemory VkAllocateMemory;
  PFN_vkFreeMemory VkFreeMemory;
  PFN_vkBindBufferMemory VkBindBufferMemory;
  PFN_vkMapMemory VkMapMemory;
  PFN_vkUnmapMemory VkUnmapMemory;
  PFN_vkInvalidateMappedMemoryRanges VkInvalidateMappedMemoryRanges;
  PFN_vkCreateCommandPool VkCreateCommandPool;
  PFN_vkDestroyCommandPool VkDestroyCommandPool;
  PFN_vkAllocateCommandBuffers VkAllocateCommandBuffers;
  PFN_vkFreeCommandBuffers VkFreeCommandBuffers;
  PFN_vkBeginCommandBuffer VkBeginCommandBuffer;
  PFN_vkEndCommandBuffer VkEndCommandBuffer;
  PFN_vkCmdBindPipeline VkCmdBindPipeline;
  PFN_vkCmdBindDescriptorSets VkCmdBindDescriptorSets;
  PFN_vkCmdPushConstants VkCmdPushConstants;
  PFN_vkCmdDispatch VkCmdDispatch;
  PFN_vkCmdPipelineBarrier VkCmdPipelineBarrier;
  PFN_vkCmdCopyBuffer VkCmdCopyBuffer;
  PFN_vkCreateFence VkCreateFence;
  PFN_vkDestroyFence VkDestroyFence;
  PFN_vkQueueSubmit VkQueueSubmit;
  PFN_vkWaitForFences VkWaitForFences;
  PFN_vkResetFences VkResetFences;

  if (!yonaVulkanLoadDispatchFunctions(
          &VkCreateDescriptorPool, &VkDestroyDescriptorPool,
          &VkAllocateDescriptorSets, &VkUpdateDescriptorSets, &VkCreateBuffer,
          &VkDestroyBuffer, &VkGetBufferMemoryRequirements, &VkAllocateMemory,
          &VkFreeMemory, &VkBindBufferMemory, &VkMapMemory, &VkUnmapMemory,
          &VkInvalidateMappedMemoryRanges, &VkCreateCommandPool,
          &VkDestroyCommandPool, &VkAllocateCommandBuffers,
          &VkFreeCommandBuffers, &VkBeginCommandBuffer, &VkEndCommandBuffer,
          &VkCmdBindPipeline, &VkCmdBindDescriptorSets, &VkCmdPushConstants,
          &VkCmdDispatch, &VkCmdPipelineBarrier, &VkCreateFence,
          &VkDestroyFence, &VkQueueSubmit, &VkWaitForFences, &VkResetFences)) {
    yonaVulkanNoteCopy(
        "gpu: vkGetDeviceProcAddr returned null for A required entry point");
    return 0;
  }
  VkCmdCopyBuffer = YONA_VULKAN_DEVICE_PROCEDURE(vkCmdCopyBuffer);

  yonaVulkanComputeSubmitLock();

  VkResult R = VK_SUCCESS;
  YonaVulkanReducePipeline *Rp =
      UseI32 ? yonaVulkanComputeReduceI32Pipe() : yonaVulkanComputeReducePipe();
  R = UseI32 ? yonaVulkanComputeEnsureReduceI32Pipe()
             : yonaVulkanComputeEnsureReducePipe();
  if (R != VK_SUCCESS) {
    char B[120];
    snprintf(B, sizeof B, "reduce: pipeline ensure VkResult=%d", (int)R);
    yonaVulkanNoteCopy(B);
    yonaVulkanComputeSubmitUnlock();
    return 0;
  }

  uint32_t Ulen = (uint32_t)Len;
  uint32_t Groups = (Ulen + 63u) / 64u;
  size_t Esz = UseI32 ? sizeof(int32_t) : sizeof(int64_t);
  VkDeviceSize NbytesIn = (VkDeviceSize)((size_t)Len * Esz);
  VkDeviceSize NbytesSums = (VkDeviceSize)((size_t)Groups * Esz);
  int32_t *PackedIn = NULL;
  if (UseI32) {
    PackedIn = (int32_t *)malloc((size_t)Len * sizeof(int32_t));
    if (!PackedIn) {
      yonaVulkanNoteCopy("reduce: malloc Failed packing i32 column");
      yonaVulkanComputeSubmitUnlock();
      return 0;
    }
    for (int64_t I = 0; I < Len; I++)
      PackedIn[I] = (int32_t)Arr[1 + I];
  }
  const void *ReduceSrc =
      UseI32 ? (const void *)PackedIn : (const void *)(Arr + 1);

  int UseStaging = 0;
  VkDescriptorPool Dpool = VK_NULL_HANDLE;
  VkDescriptorSet Dset = VK_NULL_HANDLE;
  VkBuffer BufIn = VK_NULL_HANDLE;
  VkBuffer BufSums = VK_NULL_HANDLE;
  VkDeviceMemory MemIn = VK_NULL_HANDLE;
  VkDeviceMemory MemSums = VK_NULL_HANDLE;
  VkBuffer BufInDev = VK_NULL_HANDLE;
  VkBuffer BufInStg = VK_NULL_HANDLE;
  VkDeviceMemory MemInDev = VK_NULL_HANDLE;
  VkDeviceMemory MemInStg = VK_NULL_HANDLE;
  VkBuffer BufSumsDev = VK_NULL_HANDLE;
  VkBuffer BufSumsStg = VK_NULL_HANDLE;
  VkDeviceMemory MemSumsDev = VK_NULL_HANDLE;
  VkDeviceMemory MemSumsStg = VK_NULL_HANDLE;
  VkCommandPool Cpool = VK_NULL_HANDLE;
  VkCommandBuffer Cmd = VK_NULL_HANDLE;
  VkFence Fence = VK_NULL_HANDLE;
  void *MappedIn = NULL;
  void *MappedSums = NULL;
  VkDeviceMemory MappedMemIn = VK_NULL_HANDLE;
  VkDeviceMemory MappedMemSums = VK_NULL_HANDLE;

  if (!yonaVulkanForceHostSsbo() && VkCmdCopyBuffer) {
    if (yonaVulkanTryDevStgPair(VkCreateBuffer, VkDestroyBuffer,
                                VkGetBufferMemoryRequirements, VkAllocateMemory,
                                VkFreeMemory, VkBindBufferMemory, NbytesIn,
                                &BufInDev, &MemInDev, &BufInStg, &MemInStg) &&
        yonaVulkanTryDevStgPair(
            VkCreateBuffer, VkDestroyBuffer, VkGetBufferMemoryRequirements,
            VkAllocateMemory, VkFreeMemory, VkBindBufferMemory, NbytesSums,
            &BufSumsDev, &MemSumsDev, &BufSumsStg, &MemSumsStg))
      UseStaging = 1;
    else {
      if (BufSumsStg != VK_NULL_HANDLE) {
        VkDestroyBuffer(YonaVulkanDevice, BufSumsStg, NULL);
        BufSumsStg = VK_NULL_HANDLE;
      }
      if (MemSumsStg != VK_NULL_HANDLE) {
        VkFreeMemory(YonaVulkanDevice, MemSumsStg, NULL);
        MemSumsStg = VK_NULL_HANDLE;
      }
      if (BufSumsDev != VK_NULL_HANDLE) {
        VkDestroyBuffer(YonaVulkanDevice, BufSumsDev, NULL);
        BufSumsDev = VK_NULL_HANDLE;
      }
      if (MemSumsDev != VK_NULL_HANDLE) {
        VkFreeMemory(YonaVulkanDevice, MemSumsDev, NULL);
        MemSumsDev = VK_NULL_HANDLE;
      }
      if (BufInStg != VK_NULL_HANDLE) {
        VkDestroyBuffer(YonaVulkanDevice, BufInStg, NULL);
        BufInStg = VK_NULL_HANDLE;
      }
      if (MemInStg != VK_NULL_HANDLE) {
        VkFreeMemory(YonaVulkanDevice, MemInStg, NULL);
        MemInStg = VK_NULL_HANDLE;
      }
      if (BufInDev != VK_NULL_HANDLE) {
        VkDestroyBuffer(YonaVulkanDevice, BufInDev, NULL);
        BufInDev = VK_NULL_HANDLE;
      }
      if (MemInDev != VK_NULL_HANDLE) {
        VkFreeMemory(YonaVulkanDevice, MemInDev, NULL);
        MemInDev = VK_NULL_HANDLE;
      }
    }
  }

  VkDescriptorPoolSize Dps = {0};
  Dps.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Dps.descriptorCount = 2;

  VkDescriptorPoolCreateInfo Dpci = {0};
  Dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  Dpci.maxSets = 1;
  Dpci.poolSizeCount = 1;
  Dpci.pPoolSizes = &Dps;
  R = VkCreateDescriptorPool(YonaVulkanDevice, &Dpci, NULL, &Dpool);
  if (R != VK_SUCCESS)
    goto ReduceFail;

  VkDescriptorSetAllocateInfo Dsai = {0};
  Dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  Dsai.descriptorPool = Dpool;
  Dsai.descriptorSetCount = 1;
  Dsai.pSetLayouts = &Rp->DescriptorSetLayout;
  R = VkAllocateDescriptorSets(YonaVulkanDevice, &Dsai, &Dset);
  if (R != VK_SUCCESS)
    goto ReduceFail;

  if (!UseStaging) {
    VkBufferCreateInfo Bci = {0};
    Bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    Bci.size = NbytesIn;
    Bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    Bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    R = VkCreateBuffer(YonaVulkanDevice, &Bci, NULL, &BufIn);
    if (R != VK_SUCCESS)
      goto ReduceFail;

    Bci.size = NbytesSums;
    R = VkCreateBuffer(YonaVulkanDevice, &Bci, NULL, &BufSums);
    if (R != VK_SUCCESS)
      goto ReduceFail;

    VkMemoryRequirements ReqIn, ReqS;
    VkGetBufferMemoryRequirements(YonaVulkanDevice, BufIn, &ReqIn);
    VkGetBufferMemoryRequirements(YonaVulkanDevice, BufSums, &ReqS);
    uint32_t MtIn = 0, MtS = 0;
    if (yonaVulkanPickMemoryType(ReqIn.memoryTypeBits,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 &MtIn) != 0 ||
        yonaVulkanPickMemoryType(ReqS.memoryTypeBits,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 &MtS) != 0) {
      yonaVulkanNoteCopy(
          "reduce: no HOST_VISIBLE|HOST_COHERENT memory for buffers");
      goto ReduceFail;
    }

    VkMemoryAllocateInfo Mai = {0};
    Mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    Mai.allocationSize = ReqIn.size;
    Mai.memoryTypeIndex = MtIn;
    R = VkAllocateMemory(YonaVulkanDevice, &Mai, NULL, &MemIn);
    if (R != VK_SUCCESS)
      goto ReduceFail;
    R = VkBindBufferMemory(YonaVulkanDevice, BufIn, MemIn, 0);
    if (R != VK_SUCCESS)
      goto ReduceFail;

    Mai.allocationSize = ReqS.size;
    Mai.memoryTypeIndex = MtS;
    R = VkAllocateMemory(YonaVulkanDevice, &Mai, NULL, &MemSums);
    if (R != VK_SUCCESS)
      goto ReduceFail;
    R = VkBindBufferMemory(YonaVulkanDevice, BufSums, MemSums, 0);
    if (R != VK_SUCCESS)
      goto ReduceFail;

    R = VkMapMemory(YonaVulkanDevice, MemIn, 0, NbytesIn, 0, &MappedIn);
    if (R != VK_SUCCESS)
      goto ReduceFail;
    MappedMemIn = MemIn;
    R = VkMapMemory(YonaVulkanDevice, MemSums, 0, NbytesSums, 0, &MappedSums);
    if (R != VK_SUCCESS)
      goto ReduceFail;
    MappedMemSums = MemSums;
    memcpy(MappedIn, ReduceSrc, (size_t)NbytesIn);
    memset(MappedSums, 0, (size_t)NbytesSums);
  } else {
    R = VkMapMemory(YonaVulkanDevice, MemInStg, 0, NbytesIn, 0, &MappedIn);
    if (R != VK_SUCCESS)
      goto ReduceFail;
    MappedMemIn = MemInStg;
    R = VkMapMemory(YonaVulkanDevice, MemSumsStg, 0, NbytesSums, 0,
                    &MappedSums);
    if (R != VK_SUCCESS)
      goto ReduceFail;
    MappedMemSums = MemSumsStg;
    memcpy(MappedIn, ReduceSrc, (size_t)NbytesIn);
    memset(MappedSums, 0, (size_t)NbytesSums);
  }

  VkDescriptorBufferInfo Dbi[2] = {0};
  Dbi[0].buffer = UseStaging ? BufInDev : BufIn;
  Dbi[0].offset = 0;
  Dbi[0].range = NbytesIn;
  Dbi[1].buffer = UseStaging ? BufSumsDev : BufSums;
  Dbi[1].offset = 0;
  Dbi[1].range = NbytesSums;

  VkWriteDescriptorSet W[2] = {0};
  W[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  W[0].dstSet = Dset;
  W[0].dstBinding = 0;
  W[0].descriptorCount = 1;
  W[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  W[0].pBufferInfo = &Dbi[0];
  W[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  W[1].dstSet = Dset;
  W[1].dstBinding = 1;
  W[1].descriptorCount = 1;
  W[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  W[1].pBufferInfo = &Dbi[1];
  VkUpdateDescriptorSets(YonaVulkanDevice, 2, W, 0, NULL);

  VkCommandPoolCreateInfo Cpci0 = {0};
  Cpci0.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  Cpci0.queueFamilyIndex = YonaVulkanQueueFamily;
  Cpci0.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  R = VkCreateCommandPool(YonaVulkanDevice, &Cpci0, NULL, &Cpool);
  if (R != VK_SUCCESS)
    goto ReduceFail;

  VkCommandBufferAllocateInfo Cbai = {0};
  Cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  Cbai.commandPool = Cpool;
  Cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  Cbai.commandBufferCount = 1;
  R = VkAllocateCommandBuffers(YonaVulkanDevice, &Cbai, &Cmd);
  if (R != VK_SUCCESS)
    goto ReduceFail;

  VkFenceCreateInfo Fci = {0};
  Fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  R = VkCreateFence(YonaVulkanDevice, &Fci, NULL, &Fence);
  if (R != VK_SUCCESS)
    goto ReduceFail;

  VkCommandBufferBeginInfo Bi = {0};
  Bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  Bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  R = VkBeginCommandBuffer(Cmd, &Bi);
  if (R != VK_SUCCESS)
    goto ReduceFail;

  if (UseStaging) {
    VkBufferCopy C0 = {0};
    C0.size = NbytesIn;
    yonaVulkanBarrierBuffer(
        VkCmdPipelineBarrier, Cmd, BufInStg, VK_ACCESS_HOST_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkCmdCopyBuffer(Cmd, BufInStg, BufInDev, 1, &C0);
    yonaVulkanBarrierBuffer(
        VkCmdPipelineBarrier, Cmd, BufInDev, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    yonaVulkanBarrierBuffer(VkCmdPipelineBarrier, Cmd, BufSumsDev, 0u,
                            VK_ACCESS_SHADER_WRITE_BIT,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  }

  VkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, Rp->Pipeline);
  VkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          Rp->PipelineLayout, 0, 1, &Dset, 0, NULL);
  VkCmdPushConstants(Cmd, Rp->PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4,
                     &Ulen);
  VkCmdDispatch(Cmd, Groups, 1, 1);

  if (UseStaging) {
    VkBufferCopy C1 = {0};
    C1.size = NbytesSums;
    yonaVulkanBarrierBuffer(
        VkCmdPipelineBarrier, Cmd, BufSumsDev, VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);
    yonaVulkanBarrierBuffer(
        VkCmdPipelineBarrier, Cmd, BufSumsStg, VK_ACCESS_TRANSFER_READ_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkCmdCopyBuffer(Cmd, BufSumsDev, BufSumsStg, 1, &C1);
    yonaVulkanBarrierBuffer(
        VkCmdPipelineBarrier, Cmd, BufSumsStg, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_HOST_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT);
  } else {
    VkMemoryBarrier Mb = {0};
    Mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    Mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    Mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    VkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &Mb, 0, NULL, 0,
                         NULL);
  }

  R = VkEndCommandBuffer(Cmd);
  if (R != VK_SUCCESS)
    goto ReduceFail;

  VkSubmitInfo Si = {0};
  Si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  Si.commandBufferCount = 1;
  Si.pCommandBuffers = &Cmd;
  R = VkQueueSubmit(YonaVulkanQueue, 1, &Si, Fence);
  if (R != VK_SUCCESS)
    goto ReduceFail;

  R = VkWaitForFences(YonaVulkanDevice, 1, &Fence, VK_TRUE, UINT64_MAX);
  if (R != VK_SUCCESS)
    goto ReduceFail;

  if (VkInvalidateMappedMemoryRanges) {
    if (UseStaging) {
      VkMappedMemoryRange Inv = {0};
      Inv.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      Inv.memory = MemSumsStg;
      Inv.offset = 0;
      Inv.size = NbytesSums;
      VkInvalidateMappedMemoryRanges(YonaVulkanDevice, 1, &Inv);
    } else {
      VkMappedMemoryRange Inv[2] = {0};
      Inv[0].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      Inv[0].memory = MemIn;
      Inv[0].offset = 0;
      Inv[0].size = NbytesIn;
      Inv[1].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      Inv[1].memory = MemSums;
      Inv[1].offset = 0;
      Inv[1].size = NbytesSums;
      VkInvalidateMappedMemoryRanges(YonaVulkanDevice, 2, Inv);
    }
  }

  int64_t Total = 0;
  if (UseI32) {
    const int32_t *Lanes = (const int32_t *)MappedSums;
    for (uint32_t I = 0; I < Groups; I++)
      Total += (int64_t)Lanes[I];
  } else {
    const int64_t *Lanes = (const int64_t *)MappedSums;
    for (uint32_t I = 0; I < Groups; I++)
      Total += Lanes[I];
  }
  *OutSum = Total;

  VkUnmapMemory(YonaVulkanDevice, MappedMemIn);
  VkUnmapMemory(YonaVulkanDevice, MappedMemSums);
  MappedIn = NULL;
  MappedSums = NULL;

  VkDestroyFence(YonaVulkanDevice, Fence, NULL);
  Fence = VK_NULL_HANDLE;
  VkFreeCommandBuffers(YonaVulkanDevice, Cpool, 1, &Cmd);
  Cmd = VK_NULL_HANDLE;
  VkDestroyCommandPool(YonaVulkanDevice, Cpool, NULL);
  Cpool = VK_NULL_HANDLE;
  if (UseStaging) {
    VkDestroyBuffer(YonaVulkanDevice, BufInStg, NULL);
    BufInStg = VK_NULL_HANDLE;
    VkFreeMemory(YonaVulkanDevice, MemInStg, NULL);
    MemInStg = VK_NULL_HANDLE;
    VkDestroyBuffer(YonaVulkanDevice, BufInDev, NULL);
    BufInDev = VK_NULL_HANDLE;
    VkFreeMemory(YonaVulkanDevice, MemInDev, NULL);
    MemInDev = VK_NULL_HANDLE;
    VkDestroyBuffer(YonaVulkanDevice, BufSumsStg, NULL);
    BufSumsStg = VK_NULL_HANDLE;
    VkFreeMemory(YonaVulkanDevice, MemSumsStg, NULL);
    MemSumsStg = VK_NULL_HANDLE;
    VkDestroyBuffer(YonaVulkanDevice, BufSumsDev, NULL);
    BufSumsDev = VK_NULL_HANDLE;
    VkFreeMemory(YonaVulkanDevice, MemSumsDev, NULL);
    MemSumsDev = VK_NULL_HANDLE;
  } else {
    VkDestroyBuffer(YonaVulkanDevice, BufIn, NULL);
    VkDestroyBuffer(YonaVulkanDevice, BufSums, NULL);
    BufIn = VK_NULL_HANDLE;
    BufSums = VK_NULL_HANDLE;
    VkFreeMemory(YonaVulkanDevice, MemIn, NULL);
    VkFreeMemory(YonaVulkanDevice, MemSums, NULL);
    MemIn = VK_NULL_HANDLE;
    MemSums = VK_NULL_HANDLE;
  }
  VkDestroyDescriptorPool(YonaVulkanDevice, Dpool, NULL);
  Dpool = VK_NULL_HANDLE;

  free(PackedIn);
  yonaVulkanComputeSubmitUnlock();
  return 1;

ReduceFail:
  if (!YonaVulkanLastNote[0]) {
    char B[120];
    snprintf(B, sizeof B, "reduce: Vulkan failure VkResult=%d", (int)R);
    yonaVulkanNoteCopy(B);
  }
  if (MappedMemIn != VK_NULL_HANDLE)
    VkUnmapMemory(YonaVulkanDevice, MappedMemIn);
  if (MappedMemSums != VK_NULL_HANDLE)
    VkUnmapMemory(YonaVulkanDevice, MappedMemSums);
  if (Fence != VK_NULL_HANDLE)
    VkDestroyFence(YonaVulkanDevice, Fence, NULL);
  if (Cmd != VK_NULL_HANDLE && Cpool != VK_NULL_HANDLE)
    VkFreeCommandBuffers(YonaVulkanDevice, Cpool, 1, &Cmd);
  if (Cpool != VK_NULL_HANDLE)
    VkDestroyCommandPool(YonaVulkanDevice, Cpool, NULL);
  if (UseStaging) {
    if (BufInStg != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufInStg, NULL);
    if (MemInStg != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemInStg, NULL);
    if (BufInDev != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufInDev, NULL);
    if (MemInDev != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemInDev, NULL);
    if (BufSumsStg != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufSumsStg, NULL);
    if (MemSumsStg != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemSumsStg, NULL);
    if (BufSumsDev != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufSumsDev, NULL);
    if (MemSumsDev != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemSumsDev, NULL);
  } else {
    if (BufIn != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufIn, NULL);
    if (BufSums != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufSums, NULL);
    if (MemIn != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemIn, NULL);
    if (MemSums != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemSums, NULL);
  }
  if (Dpool != VK_NULL_HANDLE)
    VkDestroyDescriptorPool(YonaVulkanDevice, Dpool, NULL);
  free(PackedIn);
  yonaVulkanComputeSubmitUnlock();
  return 0;
}

static int yonaVulkanTryFilterInt64(int64_t Threshold, int64_t *Arr,
                                    int64_t **Out, int LessThan);

static int gpuVulkanTryFilterGreaterThanInt64Impl(int64_t Threshold,
                                                  int64_t *Arr, int64_t **Out) {
  return yonaVulkanTryFilterInt64(Threshold, Arr, Out, 0);
}

static int gpuVulkanTryFilterLessThanInt64Impl(int64_t Threshold, int64_t *Arr,
                                               int64_t **Out) {
  return yonaVulkanTryFilterInt64(Threshold, Arr, Out, 1);
}

static int yonaVulkanTryFilterInt64(int64_t Threshold, int64_t *Arr,
                                    int64_t **Out, int LessThan) {
  *Out = NULL;
  if (!yonaVulkanEnvFilter()) {
    yonaVulkanNoteCopy(
        "filter: set YONA_GPU_VULKAN_FILTER=1 or YONA_GPU_VULKAN_COMPUTE=1");
    return 0;
  }
  if (!yonaVulkanCommonPrecheck(Arr, "filter"))
    return 0;

  int64_t MinLen = readMinimumLength("YONA_GPU_VULKAN_MIN_LEN",
                                     "YONA_GPU_VULKAN_FILTER_MIN_LEN");
  int64_t Len = Arr[0];
  if (Len < MinLen) {
    yonaVulkanNoteCopy(
        "filter: IntArray shorter than configured GPU min length");
    return 0;
  }
  if (Len > (int64_t)0x7fffffff) {
    yonaVulkanNoteCopy("filter: IntArray length exceeds supported range");
    return 0;
  }

  int UseI32 = yonaVulkanPreferI32();
  if (UseI32 && !yonaVulkanI32FilterFits(Arr, Threshold)) {
    yonaVulkanNoteCopy(
        "filter: values exceed int32; GPU i32 path skipped (no shaderInt64)");
    return 0;
  }
  int32_t *PackedIn = NULL;

  PFN_vkCreateDescriptorPool VkCreateDescriptorPool;
  PFN_vkDestroyDescriptorPool VkDestroyDescriptorPool;
  PFN_vkAllocateDescriptorSets VkAllocateDescriptorSets;
  PFN_vkUpdateDescriptorSets VkUpdateDescriptorSets;
  PFN_vkCreateBuffer VkCreateBuffer;
  PFN_vkDestroyBuffer VkDestroyBuffer;
  PFN_vkGetBufferMemoryRequirements VkGetBufferMemoryRequirements;
  PFN_vkAllocateMemory VkAllocateMemory;
  PFN_vkFreeMemory VkFreeMemory;
  PFN_vkBindBufferMemory VkBindBufferMemory;
  PFN_vkMapMemory VkMapMemory;
  PFN_vkUnmapMemory VkUnmapMemory;
  PFN_vkInvalidateMappedMemoryRanges VkInvalidateMappedMemoryRanges;
  PFN_vkFlushMappedMemoryRanges VkFlushMappedMemoryRanges;
  PFN_vkCreateCommandPool VkCreateCommandPool;
  PFN_vkDestroyCommandPool VkDestroyCommandPool;
  PFN_vkAllocateCommandBuffers VkAllocateCommandBuffers;
  PFN_vkFreeCommandBuffers VkFreeCommandBuffers;
  PFN_vkBeginCommandBuffer VkBeginCommandBuffer;
  PFN_vkEndCommandBuffer VkEndCommandBuffer;
  PFN_vkCmdBindPipeline VkCmdBindPipeline;
  PFN_vkCmdBindDescriptorSets VkCmdBindDescriptorSets;
  PFN_vkCmdPushConstants VkCmdPushConstants;
  PFN_vkCmdDispatch VkCmdDispatch;
  PFN_vkCmdPipelineBarrier VkCmdPipelineBarrier;
  PFN_vkCreateFence VkCreateFence;
  PFN_vkDestroyFence VkDestroyFence;
  PFN_vkQueueSubmit VkQueueSubmit;
  PFN_vkWaitForFences VkWaitForFences;
  PFN_vkResetFences VkResetFences;

  if (!yonaVulkanLoadDispatchFunctions(
          &VkCreateDescriptorPool, &VkDestroyDescriptorPool,
          &VkAllocateDescriptorSets, &VkUpdateDescriptorSets, &VkCreateBuffer,
          &VkDestroyBuffer, &VkGetBufferMemoryRequirements, &VkAllocateMemory,
          &VkFreeMemory, &VkBindBufferMemory, &VkMapMemory, &VkUnmapMemory,
          &VkInvalidateMappedMemoryRanges, &VkCreateCommandPool,
          &VkDestroyCommandPool, &VkAllocateCommandBuffers,
          &VkFreeCommandBuffers, &VkBeginCommandBuffer, &VkEndCommandBuffer,
          &VkCmdBindPipeline, &VkCmdBindDescriptorSets, &VkCmdPushConstants,
          &VkCmdDispatch, &VkCmdPipelineBarrier, &VkCreateFence,
          &VkDestroyFence, &VkQueueSubmit, &VkWaitForFences, &VkResetFences)) {
    yonaVulkanNoteCopy(
        "gpu: vkGetDeviceProcAddr returned null for A required entry point");
    return 0;
  }
  VkFlushMappedMemoryRanges =
      YONA_VULKAN_DEVICE_PROCEDURE(vkFlushMappedMemoryRanges);

  yonaVulkanComputeSubmitLock();

  VkResult R = VK_SUCCESS;
  YonaVulkanReducePipeline *Mp = NULL;
  if (LessThan)
    Mp = UseI32 ? yonaVulkanComputeFilterMarkLtI32Pipe()
                : yonaVulkanComputeFilterMarkLtPipe();
  else
    Mp = UseI32 ? yonaVulkanComputeFilterMarkI32Pipe()
                : yonaVulkanComputeFilterMarkPipe();
  YonaVulkanScatterPipeline *Sp = UseI32
                                      ? yonaVulkanComputeFilterScatterI32Pipe()
                                      : yonaVulkanComputeFilterScatterPipe();
  YonaVulkanReducePipeline *Ft = NULL;
  YonaVulkanReducePipeline *Pp = NULL;
  YonaVulkanScatterPipeline *Ix = NULL;
  if (LessThan)
    R = UseI32 ? yonaVulkanComputeEnsureFilterMarkLtI32Pipe()
               : yonaVulkanComputeEnsureFilterMarkLtPipe();
  else
    R = UseI32 ? yonaVulkanComputeEnsureFilterMarkI32Pipe()
               : yonaVulkanComputeEnsureFilterMarkPipe();
  if (R != VK_SUCCESS) {
    char B[120];
    snprintf(B, sizeof B, "filter: mark pipeline ensure VkResult=%d", (int)R);
    yonaVulkanNoteCopy(B);
    yonaVulkanComputeSubmitUnlock();
    return 0;
  }
  R = UseI32 ? yonaVulkanComputeEnsureFilterScatterI32Pipe()
             : yonaVulkanComputeEnsureFilterScatterPipe();
  if (R != VK_SUCCESS) {
    char B[120];
    snprintf(B, sizeof B, "filter: scatter pipeline ensure VkResult=%d",
             (int)R);
    yonaVulkanNoteCopy(B);
    yonaVulkanComputeSubmitUnlock();
    return 0;
  }
  R = UseI32 ? yonaVulkanComputeEnsureFilterFlagsToI32Pipe()
             : yonaVulkanComputeEnsureFilterFlagsToInt64Pipe();
  if (R != VK_SUCCESS) {
    char B[120];
    snprintf(B, sizeof B, "filter: FlagsToI32/i64 pipeline ensure VkResult=%d",
             (int)R);
    yonaVulkanNoteCopy(B);
    yonaVulkanComputeSubmitUnlock();
    return 0;
  }
  R = UseI32 ? yonaVulkanComputeEnsureFilterPrefixI32Pipe()
             : yonaVulkanComputeEnsureFilterPrefixPipe();
  if (R != VK_SUCCESS) {
    char B[120];
    snprintf(B, sizeof B, "filter: prefix pipeline ensure VkResult=%d", (int)R);
    yonaVulkanNoteCopy(B);
    yonaVulkanComputeSubmitUnlock();
    return 0;
  }
  R = UseI32 ? yonaVulkanComputeEnsureFilterIncToExcI32Pipe()
             : yonaVulkanComputeEnsureFilterIncToExcPipe();
  if (R != VK_SUCCESS) {
    char B[120];
    snprintf(B, sizeof B, "filter: IncToExc pipeline ensure VkResult=%d",
             (int)R);
    yonaVulkanNoteCopy(B);
    yonaVulkanComputeSubmitUnlock();
    return 0;
  }
  Ft = UseI32 ? yonaVulkanComputeFilterFlagsToI32Pipe()
              : yonaVulkanComputeFilterFlagsToInt64Pipe();
  Pp = UseI32 ? yonaVulkanComputeFilterPrefixI32Pipe()
              : yonaVulkanComputeFilterPrefixPipe();
  Ix = UseI32 ? yonaVulkanComputeFilterIncToExcI32Pipe()
              : yonaVulkanComputeFilterIncToExcPipe();

  PFN_vkCmdCopyBuffer VkCmdCopyBuffer =
      YONA_VULKAN_DEVICE_PROCEDURE(vkCmdCopyBuffer);

  uint32_t Ulen = (uint32_t)Len;
  size_t Esz = UseI32 ? sizeof(int32_t) : sizeof(int64_t);
  VkDeviceSize NbytesIn = (VkDeviceSize)((size_t)Len * Esz);
  VkDeviceSize NbytesFlags = (VkDeviceSize)((size_t)Ulen * sizeof(int32_t));
  VkDeviceSize NbytesPrefix = NbytesIn;
  VkDeviceSize NbytesOut = NbytesIn;
  if (UseI32) {
    PackedIn = (int32_t *)malloc((size_t)Len * sizeof(int32_t));
    if (!PackedIn) {
      yonaVulkanNoteCopy("filter: malloc Failed packing i32 column");
      yonaVulkanComputeSubmitUnlock();
      return 0;
    }
    for (int64_t I = 0; I < Len; I++)
      PackedIn[I] = (int32_t)Arr[1 + I];
  }
  const void *FilterSrc =
      UseI32 ? (const void *)PackedIn : (const void *)(Arr + 1);

  int UseStaging = 0;
  VkDescriptorPool Dpool = VK_NULL_HANDLE;
  VkDescriptorSet Dsets[6] = {VK_NULL_HANDLE};
  VkBuffer BufIn = VK_NULL_HANDLE;
  VkBuffer BufFlags = VK_NULL_HANDLE;
  VkBuffer BufPrefix = VK_NULL_HANDLE;
  VkBuffer BufOut = VK_NULL_HANDLE;
  VkDeviceMemory MemIn = VK_NULL_HANDLE;
  VkDeviceMemory MemFlags = VK_NULL_HANDLE;
  VkDeviceMemory MemPrefix = VK_NULL_HANDLE;
  VkDeviceMemory MemOut = VK_NULL_HANDLE;
  VkBuffer BufInDev = VK_NULL_HANDLE;
  VkBuffer BufInStg = VK_NULL_HANDLE;
  VkBuffer BufFlagsDev = VK_NULL_HANDLE;
  VkBuffer BufFlagsStg = VK_NULL_HANDLE;
  VkBuffer BufPrefixDev = VK_NULL_HANDLE;
  VkBuffer BufPrefixStg = VK_NULL_HANDLE;
  VkBuffer BufOutDev = VK_NULL_HANDLE;
  VkBuffer BufOutStg = VK_NULL_HANDLE;
  VkDeviceMemory MemInDev = VK_NULL_HANDLE;
  VkDeviceMemory MemInStg = VK_NULL_HANDLE;
  VkDeviceMemory MemFlagsDev = VK_NULL_HANDLE;
  VkDeviceMemory MemFlagsStg = VK_NULL_HANDLE;
  VkDeviceMemory MemPrefixDev = VK_NULL_HANDLE;
  VkDeviceMemory MemPrefixStg = VK_NULL_HANDLE;
  VkDeviceMemory MemOutDev = VK_NULL_HANDLE;
  VkDeviceMemory MemOutStg = VK_NULL_HANDLE;
  VkCommandPool Cpool = VK_NULL_HANDLE;
  VkCommandBuffer Cmds[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
  VkFence Fence = VK_NULL_HANDLE;
  void *PIn = NULL;
  void *PFlags = NULL;
  void *PPrefix = NULL;
  void *POut = NULL;
  VkDeviceMemory MapMemIn = VK_NULL_HANDLE;
  VkDeviceMemory MapMemFlags = VK_NULL_HANDLE;
  VkDeviceMemory MapMemPrefix = VK_NULL_HANDLE;
  VkDeviceMemory MapMemOut = VK_NULL_HANDLE;

  VkBuffer BufScan0 = VK_NULL_HANDLE;
  VkBuffer BufScan1 = VK_NULL_HANDLE;
  VkDeviceMemory MemScan0 = VK_NULL_HANDLE;
  VkDeviceMemory MemScan1 = VK_NULL_HANDLE;
  void *PScan0 = NULL;
  void *PScan1 = NULL;
  VkDeviceMemory MapMemScan0 = VK_NULL_HANDLE;
  VkDeviceMemory MapMemScan1 = VK_NULL_HANDLE;
  VkBuffer BufScan0Dev = VK_NULL_HANDLE;
  VkBuffer BufScan1Dev = VK_NULL_HANDLE;
  VkDeviceMemory MemScan0Dev = VK_NULL_HANDLE;
  VkDeviceMemory MemScan1Dev = VK_NULL_HANDLE;
  VkBuffer BufCountStg = VK_NULL_HANDLE;
  VkDeviceMemory MemCountStg = VK_NULL_HANDLE;
  void *PCount = NULL;
  VkDeviceMemory MapMemCount = VK_NULL_HANDLE;

  const uint32_t FilterDescSets = 6u;
  const uint32_t FilterDescWrites = 15u;

  VkDescriptorPoolSize Dps[1] = {0};
  Dps[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Dps[0].descriptorCount = FilterDescWrites;

  VkDescriptorPoolCreateInfo Dpci = {0};
  Dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  Dpci.maxSets = FilterDescSets;
  Dpci.poolSizeCount = 1;
  Dpci.pPoolSizes = Dps;
  R = VkCreateDescriptorPool(YonaVulkanDevice, &Dpci, NULL, &Dpool);
  if (R != VK_SUCCESS)
    goto FiltFail;

  VkDescriptorSetLayout LayoutsAlloc[6];
  LayoutsAlloc[0] = Mp->DescriptorSetLayout;
  LayoutsAlloc[1] = Sp->DescriptorSetLayout;
  LayoutsAlloc[2] = Ft->DescriptorSetLayout;
  LayoutsAlloc[3] = Pp->DescriptorSetLayout;
  LayoutsAlloc[4] = Pp->DescriptorSetLayout;
  LayoutsAlloc[5] = Ix->DescriptorSetLayout;
  VkDescriptorSetAllocateInfo Dsai = {0};
  Dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  Dsai.descriptorPool = Dpool;
  Dsai.descriptorSetCount = FilterDescSets;
  Dsai.pSetLayouts = LayoutsAlloc;
  R = VkAllocateDescriptorSets(YonaVulkanDevice, &Dsai, Dsets);
  if (R != VK_SUCCESS)
    goto FiltFail;

  if (!yonaVulkanForceHostSsbo() && VkCmdCopyBuffer &&
      yonaVulkanTryDevStgPair(VkCreateBuffer, VkDestroyBuffer,
                              VkGetBufferMemoryRequirements, VkAllocateMemory,
                              VkFreeMemory, VkBindBufferMemory, NbytesIn,
                              &BufInDev, &MemInDev, &BufInStg, &MemInStg) &&
      yonaVulkanTryDevStgPair(
          VkCreateBuffer, VkDestroyBuffer, VkGetBufferMemoryRequirements,
          VkAllocateMemory, VkFreeMemory, VkBindBufferMemory, NbytesFlags,
          &BufFlagsDev, &MemFlagsDev, &BufFlagsStg, &MemFlagsStg) &&
      yonaVulkanTryDevStgPair(
          VkCreateBuffer, VkDestroyBuffer, VkGetBufferMemoryRequirements,
          VkAllocateMemory, VkFreeMemory, VkBindBufferMemory, NbytesPrefix,
          &BufPrefixDev, &MemPrefixDev, &BufPrefixStg, &MemPrefixStg) &&
      yonaVulkanTryDevStgPair(VkCreateBuffer, VkDestroyBuffer,
                              VkGetBufferMemoryRequirements, VkAllocateMemory,
                              VkFreeMemory, VkBindBufferMemory, NbytesOut,
                              &BufOutDev, &MemOutDev, &BufOutStg, &MemOutStg))
    UseStaging = 1;
  else {
    if (BufOutStg != VK_NULL_HANDLE) {
      VkDestroyBuffer(YonaVulkanDevice, BufOutStg, NULL);
      BufOutStg = VK_NULL_HANDLE;
    }
    if (MemOutStg != VK_NULL_HANDLE) {
      VkFreeMemory(YonaVulkanDevice, MemOutStg, NULL);
      MemOutStg = VK_NULL_HANDLE;
    }
    if (BufOutDev != VK_NULL_HANDLE) {
      VkDestroyBuffer(YonaVulkanDevice, BufOutDev, NULL);
      BufOutDev = VK_NULL_HANDLE;
    }
    if (MemOutDev != VK_NULL_HANDLE) {
      VkFreeMemory(YonaVulkanDevice, MemOutDev, NULL);
      MemOutDev = VK_NULL_HANDLE;
    }
    if (BufPrefixStg != VK_NULL_HANDLE) {
      VkDestroyBuffer(YonaVulkanDevice, BufPrefixStg, NULL);
      BufPrefixStg = VK_NULL_HANDLE;
    }
    if (MemPrefixStg != VK_NULL_HANDLE) {
      VkFreeMemory(YonaVulkanDevice, MemPrefixStg, NULL);
      MemPrefixStg = VK_NULL_HANDLE;
    }
    if (BufPrefixDev != VK_NULL_HANDLE) {
      VkDestroyBuffer(YonaVulkanDevice, BufPrefixDev, NULL);
      BufPrefixDev = VK_NULL_HANDLE;
    }
    if (MemPrefixDev != VK_NULL_HANDLE) {
      VkFreeMemory(YonaVulkanDevice, MemPrefixDev, NULL);
      MemPrefixDev = VK_NULL_HANDLE;
    }
    if (BufFlagsStg != VK_NULL_HANDLE) {
      VkDestroyBuffer(YonaVulkanDevice, BufFlagsStg, NULL);
      BufFlagsStg = VK_NULL_HANDLE;
    }
    if (MemFlagsStg != VK_NULL_HANDLE) {
      VkFreeMemory(YonaVulkanDevice, MemFlagsStg, NULL);
      MemFlagsStg = VK_NULL_HANDLE;
    }
    if (BufFlagsDev != VK_NULL_HANDLE) {
      VkDestroyBuffer(YonaVulkanDevice, BufFlagsDev, NULL);
      BufFlagsDev = VK_NULL_HANDLE;
    }
    if (MemFlagsDev != VK_NULL_HANDLE) {
      VkFreeMemory(YonaVulkanDevice, MemFlagsDev, NULL);
      MemFlagsDev = VK_NULL_HANDLE;
    }
    if (BufInStg != VK_NULL_HANDLE) {
      VkDestroyBuffer(YonaVulkanDevice, BufInStg, NULL);
      BufInStg = VK_NULL_HANDLE;
    }
    if (MemInStg != VK_NULL_HANDLE) {
      VkFreeMemory(YonaVulkanDevice, MemInStg, NULL);
      MemInStg = VK_NULL_HANDLE;
    }
    if (BufInDev != VK_NULL_HANDLE) {
      VkDestroyBuffer(YonaVulkanDevice, BufInDev, NULL);
      BufInDev = VK_NULL_HANDLE;
    }
    if (MemInDev != VK_NULL_HANDLE) {
      VkFreeMemory(YonaVulkanDevice, MemInDev, NULL);
      MemInDev = VK_NULL_HANDLE;
    }
  }

  if (!UseStaging) {
    VkBufferCreateInfo Bci = {0};
    Bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    Bci.size = NbytesIn;
    Bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    Bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    R = VkCreateBuffer(YonaVulkanDevice, &Bci, NULL, &BufIn);
    if (R != VK_SUCCESS)
      goto FiltFail;

    Bci.size = NbytesFlags;
    R = VkCreateBuffer(YonaVulkanDevice, &Bci, NULL, &BufFlags);
    if (R != VK_SUCCESS)
      goto FiltFail;

    Bci.size = NbytesPrefix;
    R = VkCreateBuffer(YonaVulkanDevice, &Bci, NULL, &BufPrefix);
    if (R != VK_SUCCESS)
      goto FiltFail;

    Bci.size = NbytesOut;
    R = VkCreateBuffer(YonaVulkanDevice, &Bci, NULL, &BufOut);
    if (R != VK_SUCCESS)
      goto FiltFail;

    VkMemoryRequirements RqIn, RqF, RqP, RqO;
    VkGetBufferMemoryRequirements(YonaVulkanDevice, BufIn, &RqIn);
    VkGetBufferMemoryRequirements(YonaVulkanDevice, BufFlags, &RqF);
    VkGetBufferMemoryRequirements(YonaVulkanDevice, BufPrefix, &RqP);
    VkGetBufferMemoryRequirements(YonaVulkanDevice, BufOut, &RqO);
    uint32_t MtIn = 0, MtF = 0, MtP = 0, MtO = 0;
    const VkMemoryPropertyFlags Want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (yonaVulkanPickMemoryType(RqIn.memoryTypeBits, Want, &MtIn) != 0 ||
        yonaVulkanPickMemoryType(RqF.memoryTypeBits, Want, &MtF) != 0 ||
        yonaVulkanPickMemoryType(RqP.memoryTypeBits, Want, &MtP) != 0 ||
        yonaVulkanPickMemoryType(RqO.memoryTypeBits, Want, &MtO) != 0) {
      yonaVulkanNoteCopy(
          "filter: no HOST_VISIBLE|HOST_COHERENT memory for buffers");
      goto FiltFail;
    }

    VkMemoryAllocateInfo Mai = {0};
    Mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    Mai.allocationSize = RqIn.size;
    Mai.memoryTypeIndex = MtIn;
    R = VkAllocateMemory(YonaVulkanDevice, &Mai, NULL, &MemIn);
    if (R != VK_SUCCESS)
      goto FiltFail;
    R = VkBindBufferMemory(YonaVulkanDevice, BufIn, MemIn, 0);
    if (R != VK_SUCCESS)
      goto FiltFail;

    Mai.allocationSize = RqF.size;
    Mai.memoryTypeIndex = MtF;
    R = VkAllocateMemory(YonaVulkanDevice, &Mai, NULL, &MemFlags);
    if (R != VK_SUCCESS)
      goto FiltFail;
    R = VkBindBufferMemory(YonaVulkanDevice, BufFlags, MemFlags, 0);
    if (R != VK_SUCCESS)
      goto FiltFail;

    Mai.allocationSize = RqP.size;
    Mai.memoryTypeIndex = MtP;
    R = VkAllocateMemory(YonaVulkanDevice, &Mai, NULL, &MemPrefix);
    if (R != VK_SUCCESS)
      goto FiltFail;
    R = VkBindBufferMemory(YonaVulkanDevice, BufPrefix, MemPrefix, 0);
    if (R != VK_SUCCESS)
      goto FiltFail;

    Mai.allocationSize = RqO.size;
    Mai.memoryTypeIndex = MtO;
    R = VkAllocateMemory(YonaVulkanDevice, &Mai, NULL, &MemOut);
    if (R != VK_SUCCESS)
      goto FiltFail;
    R = VkBindBufferMemory(YonaVulkanDevice, BufOut, MemOut, 0);
    if (R != VK_SUCCESS)
      goto FiltFail;

    R = VkMapMemory(YonaVulkanDevice, MemIn, 0, NbytesIn, 0, &PIn);
    if (R != VK_SUCCESS)
      goto FiltFail;
    MapMemIn = MemIn;
    R = VkMapMemory(YonaVulkanDevice, MemFlags, 0, NbytesFlags, 0, &PFlags);
    if (R != VK_SUCCESS)
      goto FiltFail;
    MapMemFlags = MemFlags;
    R = VkMapMemory(YonaVulkanDevice, MemPrefix, 0, NbytesPrefix, 0, &PPrefix);
    if (R != VK_SUCCESS)
      goto FiltFail;
    MapMemPrefix = MemPrefix;
    R = VkMapMemory(YonaVulkanDevice, MemOut, 0, NbytesOut, 0, &POut);
    if (R != VK_SUCCESS)
      goto FiltFail;
    MapMemOut = MemOut;
  } else {
    R = VkMapMemory(YonaVulkanDevice, MemInStg, 0, NbytesIn, 0, &PIn);
    if (R != VK_SUCCESS)
      goto FiltFail;
    MapMemIn = MemInStg;
    R = VkMapMemory(YonaVulkanDevice, MemFlagsStg, 0, NbytesFlags, 0, &PFlags);
    if (R != VK_SUCCESS)
      goto FiltFail;
    MapMemFlags = MemFlagsStg;
    R = VkMapMemory(YonaVulkanDevice, MemPrefixStg, 0, NbytesPrefix, 0,
                    &PPrefix);
    if (R != VK_SUCCESS)
      goto FiltFail;
    MapMemPrefix = MemPrefixStg;
    R = VkMapMemory(YonaVulkanDevice, MemOutStg, 0, NbytesOut, 0, &POut);
    if (R != VK_SUCCESS)
      goto FiltFail;
    MapMemOut = MemOutStg;
  }

  if (!UseStaging) {
    VkBufferCreateInfo Bcs = {0};
    Bcs.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    Bcs.size = NbytesPrefix;
    Bcs.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    Bcs.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    R = VkCreateBuffer(YonaVulkanDevice, &Bcs, NULL, &BufScan0);
    if (R != VK_SUCCESS)
      goto FiltFail;
    R = VkCreateBuffer(YonaVulkanDevice, &Bcs, NULL, &BufScan1);
    if (R != VK_SUCCESS)
      goto FiltFail;
    VkMemoryRequirements RqSc0, RqSc1;
    VkGetBufferMemoryRequirements(YonaVulkanDevice, BufScan0, &RqSc0);
    VkGetBufferMemoryRequirements(YonaVulkanDevice, BufScan1, &RqSc1);
    uint32_t MtSc0 = 0, MtSc1 = 0;
    const VkMemoryPropertyFlags WantSc = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (yonaVulkanPickMemoryType(RqSc0.memoryTypeBits, WantSc, &MtSc0) != 0 ||
        yonaVulkanPickMemoryType(RqSc1.memoryTypeBits, WantSc, &MtSc1) != 0) {
      yonaVulkanNoteCopy("filter: no host memory for scan buffers");
      goto FiltFail;
    }
    VkMemoryAllocateInfo Mas = {0};
    Mas.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    Mas.allocationSize = RqSc0.size;
    Mas.memoryTypeIndex = MtSc0;
    R = VkAllocateMemory(YonaVulkanDevice, &Mas, NULL, &MemScan0);
    if (R != VK_SUCCESS)
      goto FiltFail;
    R = VkBindBufferMemory(YonaVulkanDevice, BufScan0, MemScan0, 0);
    if (R != VK_SUCCESS)
      goto FiltFail;
    Mas.allocationSize = RqSc1.size;
    Mas.memoryTypeIndex = MtSc1;
    R = VkAllocateMemory(YonaVulkanDevice, &Mas, NULL, &MemScan1);
    if (R != VK_SUCCESS)
      goto FiltFail;
    R = VkBindBufferMemory(YonaVulkanDevice, BufScan1, MemScan1, 0);
    if (R != VK_SUCCESS)
      goto FiltFail;
    R = VkMapMemory(YonaVulkanDevice, MemScan0, 0, NbytesPrefix, 0, &PScan0);
    if (R != VK_SUCCESS)
      goto FiltFail;
    MapMemScan0 = MemScan0;
    R = VkMapMemory(YonaVulkanDevice, MemScan1, 0, NbytesPrefix, 0, &PScan1);
    if (R != VK_SUCCESS)
      goto FiltFail;
    MapMemScan1 = MemScan1;
  } else {
    R = yonaVulkanCreateDeviceLocalSsbo(
        VkCreateBuffer, VkDestroyBuffer, VkGetBufferMemoryRequirements,
        VkAllocateMemory, VkFreeMemory, VkBindBufferMemory, NbytesPrefix,
        &BufScan0Dev, &MemScan0Dev);
    if (R != VK_SUCCESS)
      goto FiltFail;
    R = yonaVulkanCreateDeviceLocalSsbo(
        VkCreateBuffer, VkDestroyBuffer, VkGetBufferMemoryRequirements,
        VkAllocateMemory, VkFreeMemory, VkBindBufferMemory, NbytesPrefix,
        &BufScan1Dev, &MemScan1Dev);
    if (R != VK_SUCCESS)
      goto FiltFail;
    VkBufferCreateInfo Bcc = {0};
    Bcc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    Bcc.size = 64;
    Bcc.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    Bcc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    R = VkCreateBuffer(YonaVulkanDevice, &Bcc, NULL, &BufCountStg);
    if (R != VK_SUCCESS)
      goto FiltFail;
    VkMemoryRequirements RqC;
    VkGetBufferMemoryRequirements(YonaVulkanDevice, BufCountStg, &RqC);
    uint32_t MtC = 0;
    if (yonaVulkanPickMemoryType(RqC.memoryTypeBits,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 &MtC) != 0) {
      yonaVulkanNoteCopy("filter: no host memory for Count staging");
      goto FiltFail;
    }
    VkMemoryAllocateInfo Mac = {0};
    Mac.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    Mac.allocationSize = RqC.size;
    Mac.memoryTypeIndex = MtC;
    R = VkAllocateMemory(YonaVulkanDevice, &Mac, NULL, &MemCountStg);
    if (R != VK_SUCCESS)
      goto FiltFail;
    R = VkBindBufferMemory(YonaVulkanDevice, BufCountStg, MemCountStg, 0);
    if (R != VK_SUCCESS)
      goto FiltFail;
    R = VkMapMemory(YonaVulkanDevice, MemCountStg, 0, RqC.size, 0, &PCount);
    if (R != VK_SUCCESS)
      goto FiltFail;
    MapMemCount = MemCountStg;
  }

  uint32_t PrefixPasses = 0u;
  for (uint32_t Sx = 1u; Sx < Ulen; Sx <<= 1u)
    PrefixPasses++;
  const int Scan1HasInclusive = (PrefixPasses & 1u) != 0u;
  VkBuffer IncBufHost = Scan1HasInclusive ? BufScan1 : BufScan0;
  VkBuffer IncBufDev = Scan1HasInclusive ? BufScan1Dev : BufScan0Dev;

  memcpy(PIn, FilterSrc, (size_t)NbytesIn);
  memset(PFlags, 0, (size_t)NbytesFlags);
  memset(PPrefix, 0, (size_t)NbytesPrefix);
  memset(POut, 0, (size_t)NbytesOut);
  if (!UseStaging) {
    memset(PScan0, 0, (size_t)NbytesPrefix);
    memset(PScan1, 0, (size_t)NbytesPrefix);
  }
  if (UseStaging && PCount)
    memset(PCount, 0, 64);

  VkDescriptorBufferInfo DbiM0 = {UseStaging ? BufInDev : BufIn, 0, NbytesIn};
  VkDescriptorBufferInfo DbiM1 = {UseStaging ? BufFlagsDev : BufFlags, 0,
                                  NbytesFlags};
  VkWriteDescriptorSet Wm[2] = {0};
  Wm[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  Wm[0].dstSet = Dsets[0];
  Wm[0].dstBinding = 0;
  Wm[0].descriptorCount = 1;
  Wm[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Wm[0].pBufferInfo = &DbiM0;
  Wm[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  Wm[1].dstSet = Dsets[0];
  Wm[1].dstBinding = 1;
  Wm[1].descriptorCount = 1;
  Wm[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Wm[1].pBufferInfo = &DbiM1;
  VkUpdateDescriptorSets(YonaVulkanDevice, 2, Wm, 0, NULL);

  VkDescriptorBufferInfo DbiS0 = {UseStaging ? BufInDev : BufIn, 0, NbytesIn};
  VkDescriptorBufferInfo DbiS1 = {UseStaging ? BufFlagsDev : BufFlags, 0,
                                  NbytesFlags};
  VkDescriptorBufferInfo DbiS2 = {UseStaging ? BufPrefixDev : BufPrefix, 0,
                                  NbytesPrefix};
  VkDescriptorBufferInfo DbiS3 = {UseStaging ? BufOutDev : BufOut, 0,
                                  NbytesOut};
  VkWriteDescriptorSet Ws[4] = {0};
  uint32_t Si;
  for (Si = 0; Si < 4u; Si++) {
    Ws[Si].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Ws[Si].dstSet = Dsets[1];
    Ws[Si].dstBinding = Si;
    Ws[Si].descriptorCount = 1;
    Ws[Si].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  }
  Ws[0].pBufferInfo = &DbiS0;
  Ws[1].pBufferInfo = &DbiS1;
  Ws[2].pBufferInfo = &DbiS2;
  Ws[3].pBufferInfo = &DbiS3;
  VkUpdateDescriptorSets(YonaVulkanDevice, 4, Ws, 0, NULL);

  VkDescriptorBufferInfo DbiF0 = {UseStaging ? BufFlagsDev : BufFlags, 0,
                                  NbytesFlags};
  VkDescriptorBufferInfo DbiF1 = {UseStaging ? BufScan0Dev : BufScan0, 0,
                                  NbytesPrefix};
  VkWriteDescriptorSet Wf[2] = {0};
  Wf[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  Wf[0].dstSet = Dsets[2];
  Wf[0].dstBinding = 0;
  Wf[0].descriptorCount = 1;
  Wf[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Wf[0].pBufferInfo = &DbiF0;
  Wf[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  Wf[1].dstSet = Dsets[2];
  Wf[1].dstBinding = 1;
  Wf[1].descriptorCount = 1;
  Wf[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Wf[1].pBufferInfo = &DbiF1;
  VkDescriptorBufferInfo DbiPe0 = {UseStaging ? BufScan0Dev : BufScan0, 0,
                                   NbytesPrefix};
  VkDescriptorBufferInfo DbiPe1 = {UseStaging ? BufScan1Dev : BufScan1, 0,
                                   NbytesPrefix};
  VkWriteDescriptorSet Wpe[2] = {0};
  Wpe[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  Wpe[0].dstSet = Dsets[3];
  Wpe[0].dstBinding = 0;
  Wpe[0].descriptorCount = 1;
  Wpe[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Wpe[0].pBufferInfo = &DbiPe0;
  Wpe[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  Wpe[1].dstSet = Dsets[3];
  Wpe[1].dstBinding = 1;
  Wpe[1].descriptorCount = 1;
  Wpe[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Wpe[1].pBufferInfo = &DbiPe1;
  VkDescriptorBufferInfo DbiPo0 = {UseStaging ? BufScan1Dev : BufScan1, 0,
                                   NbytesPrefix};
  VkDescriptorBufferInfo DbiPo1 = {UseStaging ? BufScan0Dev : BufScan0, 0,
                                   NbytesPrefix};
  VkWriteDescriptorSet Wpo[2] = {0};
  Wpo[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  Wpo[0].dstSet = Dsets[4];
  Wpo[0].dstBinding = 0;
  Wpo[0].descriptorCount = 1;
  Wpo[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Wpo[0].pBufferInfo = &DbiPo0;
  Wpo[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  Wpo[1].dstSet = Dsets[4];
  Wpo[1].dstBinding = 1;
  Wpo[1].descriptorCount = 1;
  Wpo[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Wpo[1].pBufferInfo = &DbiPo1;
  VkDescriptorBufferInfo DbiX0 = {UseStaging ? IncBufDev : IncBufHost, 0,
                                  NbytesPrefix};
  VkDescriptorBufferInfo DbiX1 = {UseStaging ? BufFlagsDev : BufFlags, 0,
                                  NbytesFlags};
  VkDescriptorBufferInfo DbiX2 = {UseStaging ? BufPrefixDev : BufPrefix, 0,
                                  NbytesPrefix};
  VkWriteDescriptorSet Wx[3] = {0};
  for (uint32_t Xi = 0; Xi < 3u; Xi++) {
    Wx[Xi].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Wx[Xi].dstSet = Dsets[5];
    Wx[Xi].dstBinding = Xi;
    Wx[Xi].descriptorCount = 1;
    Wx[Xi].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  }
  Wx[0].pBufferInfo = &DbiX0;
  Wx[1].pBufferInfo = &DbiX1;
  Wx[2].pBufferInfo = &DbiX2;
  VkWriteDescriptorSet Wgpu[9];
  memcpy(Wgpu, Wf, sizeof Wf);
  memcpy(Wgpu + 2, Wpe, sizeof Wpe);
  memcpy(Wgpu + 4, Wpo, sizeof Wpo);
  memcpy(Wgpu + 6, Wx, sizeof Wx);
  VkUpdateDescriptorSets(YonaVulkanDevice, 9, Wgpu, 0, NULL);

  VkCommandPoolCreateInfo Cpci0 = {0};
  Cpci0.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  Cpci0.queueFamilyIndex = YonaVulkanQueueFamily;
  Cpci0.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  R = VkCreateCommandPool(YonaVulkanDevice, &Cpci0, NULL, &Cpool);
  if (R != VK_SUCCESS)
    goto FiltFail;

  VkCommandBufferAllocateInfo Cbai = {0};
  Cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  Cbai.commandPool = Cpool;
  Cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  Cbai.commandBufferCount = 2;
  R = VkAllocateCommandBuffers(YonaVulkanDevice, &Cbai, Cmds);
  if (R != VK_SUCCESS)
    goto FiltFail;

  VkFenceCreateInfo Fci = {0};
  Fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  R = VkCreateFence(YonaVulkanDevice, &Fci, NULL, &Fence);
  if (R != VK_SUCCESS)
    goto FiltFail;

  VkCommandBufferBeginInfo Bi = {0};
  Bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  Bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  R = VkBeginCommandBuffer(Cmds[0], &Bi);
  if (R != VK_SUCCESS)
    goto FiltFail;
  if (UseStaging) {
    VkBufferCopy CIn = {0};
    CIn.size = NbytesIn;
    yonaVulkanBarrierBuffer(
        VkCmdPipelineBarrier, Cmds[0], BufInStg, VK_ACCESS_HOST_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkCmdCopyBuffer(Cmds[0], BufInStg, BufInDev, 1, &CIn);
    yonaVulkanBarrierBuffer(
        VkCmdPipelineBarrier, Cmds[0], BufInDev, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    yonaVulkanBarrierBuffer(VkCmdPipelineBarrier, Cmds[0], BufFlagsDev, 0u,
                            VK_ACCESS_SHADER_WRITE_BIT,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  }
  VkCmdBindPipeline(Cmds[0], VK_PIPELINE_BIND_POINT_COMPUTE, Mp->Pipeline);
  VkCmdBindDescriptorSets(Cmds[0], VK_PIPELINE_BIND_POINT_COMPUTE,
                          Mp->PipelineLayout, 0, 1, &Dsets[0], 0, NULL);
  char PcMark[16];
  uint32_t MarkPush;
  if (UseI32) {
    int32_t T32 = (int32_t)Threshold;
    memcpy(PcMark, &T32, sizeof(int32_t));
    memcpy(PcMark + sizeof(int32_t), &Ulen, sizeof(uint32_t));
    MarkPush = 8;
  } else {
    memcpy(PcMark, &Threshold, sizeof(int64_t));
    memcpy(PcMark + sizeof(int64_t), &Ulen, sizeof(uint32_t));
    MarkPush = 12;
  }
  VkCmdPushConstants(Cmds[0], Mp->PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                     0, MarkPush, PcMark);
  VkCmdDispatch(Cmds[0], (Ulen + 63u) / 64u, 1, 1);
  yonaVulkanBarrierBuffer(VkCmdPipelineBarrier, Cmds[0],
                          UseStaging ? BufFlagsDev : BufFlags,
                          VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  VkCmdBindPipeline(Cmds[0], VK_PIPELINE_BIND_POINT_COMPUTE, Ft->Pipeline);
  VkCmdBindDescriptorSets(Cmds[0], VK_PIPELINE_BIND_POINT_COMPUTE,
                          Ft->PipelineLayout, 0, 1, &Dsets[2], 0, NULL);
  VkCmdPushConstants(Cmds[0], Ft->PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                     0, 4, &Ulen);
  VkCmdDispatch(Cmds[0], (Ulen + 63u) / 64u, 1, 1);
  VkBuffer Scan0b = UseStaging ? BufScan0Dev : BufScan0;
  yonaVulkanBarrierBuffer(VkCmdPipelineBarrier, Cmds[0], Scan0b,
                          VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  VkCmdBindPipeline(Cmds[0], VK_PIPELINE_BIND_POINT_COMPUTE, Pp->Pipeline);
  uint32_t Stride = 1u;
  int EvenPass = 1;
  while (Stride < Ulen) {
    VkDescriptorSet Pset = EvenPass ? Dsets[3] : Dsets[4];
    VkCmdBindDescriptorSets(Cmds[0], VK_PIPELINE_BIND_POINT_COMPUTE,
                            Pp->PipelineLayout, 0, 1, &Pset, 0, NULL);
    char PcPr[8];
    memcpy(PcPr, &Stride, sizeof(uint32_t));
    memcpy(PcPr + sizeof(uint32_t), &Ulen, sizeof(uint32_t));
    VkCmdPushConstants(Cmds[0], Pp->PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, 8, PcPr);
    VkCmdDispatch(Cmds[0], (Ulen + 63u) / 64u, 1, 1);
    VkMemoryBarrier MbMid = {0};
    MbMid.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    MbMid.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    MbMid.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkCmdPipelineBarrier(Cmds[0], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &MbMid, 0,
                         NULL, 0, NULL);
    Stride <<= 1u;
    EvenPass = !EvenPass;
  }
  VkBuffer Incb = UseStaging ? IncBufDev : IncBufHost;
  yonaVulkanBarrierBuffer(
      VkCmdPipelineBarrier, Cmds[0], Incb, VK_ACCESS_SHADER_WRITE_BIT,
      UseStaging ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_HOST_READ_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      UseStaging ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_HOST_BIT);
  if (UseStaging) {
    VkBufferCopy Cc = {0};
    Cc.srcOffset = (VkDeviceSize)((size_t)(Ulen - 1u) * Esz);
    Cc.size = (VkDeviceSize)Esz;
    VkCmdCopyBuffer(Cmds[0], Incb, BufCountStg, 1, &Cc);
    yonaVulkanBarrierBuffer(
        VkCmdPipelineBarrier, Cmds[0], BufCountStg,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT);
  }
  R = VkEndCommandBuffer(Cmds[0]);
  if (R != VK_SUCCESS)
    goto FiltFail;

  VkSubmitInfo Si0 = {0};
  Si0.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  Si0.commandBufferCount = 1;
  Si0.pCommandBuffers = &Cmds[0];
  R = VkQueueSubmit(YonaVulkanQueue, 1, &Si0, Fence);
  if (R != VK_SUCCESS)
    goto FiltFail;
  R = VkWaitForFences(YonaVulkanDevice, 1, &Fence, VK_TRUE, UINT64_MAX);
  if (R != VK_SUCCESS)
    goto FiltFail;

  int64_t Count = 0;
  if (UseStaging) {
    if (VkInvalidateMappedMemoryRanges) {
      VkMappedMemoryRange Invc = {0};
      Invc.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      Invc.memory = MapMemCount;
      Invc.offset = 0;
      Invc.size = (VkDeviceSize)Esz;
      VkInvalidateMappedMemoryRanges(YonaVulkanDevice, 1, &Invc);
    }
    Count = UseI32 ? (int64_t)(*(int32_t *)PCount) : *(int64_t *)PCount;
  } else {
    void *PInc = Scan1HasInclusive ? PScan1 : PScan0;
    if (VkInvalidateMappedMemoryRanges) {
      VkMappedMemoryRange Invi = {0};
      Invi.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
      Invi.memory = Scan1HasInclusive ? MapMemScan1 : MapMemScan0;
      Invi.offset = (VkDeviceSize)((size_t)(Ulen - 1u) * Esz);
      Invi.size = (VkDeviceSize)Esz;
      VkInvalidateMappedMemoryRanges(YonaVulkanDevice, 1, &Invi);
    }
    Count = UseI32 ? (int64_t)((int32_t *)PInc)[(size_t)Ulen - 1u]
                   : ((int64_t *)PInc)[(size_t)Ulen - 1u];
  }

  R = VkResetFences(YonaVulkanDevice, 1, &Fence);
  if (R != VK_SUCCESS)
    goto FiltFail;

  R = VkBeginCommandBuffer(Cmds[1], &Bi);
  if (R != VK_SUCCESS)
    goto FiltFail;
  VkBuffer Incb2 = UseStaging ? IncBufDev : IncBufHost;
  yonaVulkanBarrierBuffer(
      VkCmdPipelineBarrier, Cmds[1], Incb2, 0u, VK_ACCESS_SHADER_READ_BIT,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  VkCmdBindPipeline(Cmds[1], VK_PIPELINE_BIND_POINT_COMPUTE, Ix->Pipeline);
  VkCmdBindDescriptorSets(Cmds[1], VK_PIPELINE_BIND_POINT_COMPUTE,
                          Ix->PipelineLayout, 0, 1, &Dsets[5], 0, NULL);
  VkCmdPushConstants(Cmds[1], Ix->PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                     0, 4, &Ulen);
  VkCmdDispatch(Cmds[1], (Ulen + 63u) / 64u, 1, 1);
  yonaVulkanBarrierBuffer(VkCmdPipelineBarrier, Cmds[1],
                          UseStaging ? BufPrefixDev : BufPrefix,
                          VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  VkCmdBindPipeline(Cmds[1], VK_PIPELINE_BIND_POINT_COMPUTE, Sp->Pipeline);
  VkCmdBindDescriptorSets(Cmds[1], VK_PIPELINE_BIND_POINT_COMPUTE,
                          Sp->PipelineLayout, 0, 1, &Dsets[1], 0, NULL);
  VkCmdPushConstants(Cmds[1], Sp->PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                     0, 4, &Ulen);
  VkCmdDispatch(Cmds[1], (Ulen + 63u) / 64u, 1, 1);
  if (UseStaging) {
    VkBufferCopy COu = {0};
    COu.size = NbytesOut;
    yonaVulkanBarrierBuffer(
        VkCmdPipelineBarrier, Cmds[1], BufOutDev, VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);
    yonaVulkanBarrierBuffer(
        VkCmdPipelineBarrier, Cmds[1], BufOutStg, VK_ACCESS_TRANSFER_READ_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkCmdCopyBuffer(Cmds[1], BufOutDev, BufOutStg, 1, &COu);
    yonaVulkanBarrierBuffer(
        VkCmdPipelineBarrier, Cmds[1], BufOutStg, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_HOST_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT);
  } else {
    VkMemoryBarrier Mb2 = {0};
    Mb2.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    Mb2.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    Mb2.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    VkCmdPipelineBarrier(Cmds[1], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &Mb2, 0, NULL, 0,
                         NULL);
  }
  R = VkEndCommandBuffer(Cmds[1]);
  if (R != VK_SUCCESS)
    goto FiltFail;

  VkSubmitInfo Si1 = {0};
  Si1.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  Si1.commandBufferCount = 1;
  Si1.pCommandBuffers = &Cmds[1];
  R = VkQueueSubmit(YonaVulkanQueue, 1, &Si1, Fence);
  if (R != VK_SUCCESS)
    goto FiltFail;
  R = VkWaitForFences(YonaVulkanDevice, 1, &Fence, VK_TRUE, UINT64_MAX);
  if (R != VK_SUCCESS)
    goto FiltFail;

  if (VkInvalidateMappedMemoryRanges) {
    VkMappedMemoryRange Inv = {0};
    Inv.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    Inv.memory = MapMemOut;
    Inv.offset = 0;
    Inv.size = NbytesOut;
    VkInvalidateMappedMemoryRanges(YonaVulkanDevice, 1, &Inv);
  }

  int64_t *Result = YonaRuntimeIntArrayAllocate(Count);
  if (!Result) {
    yonaVulkanNoteCopy("filter: YonaRuntimeIntArrayAllocate Failed");
    goto FiltFail;
  }
  if (UseI32) {
    const int32_t *Packed32 = (const int32_t *)POut;
    for (int64_t I = 0; I < Count; I++)
      Result[1 + I] = (int64_t)Packed32[I];
  } else {
    const int64_t *Packed = (const int64_t *)POut;
    for (int64_t I = 0; I < Count; I++)
      Result[1 + I] = Packed[I];
  }

  if (MapMemIn != VK_NULL_HANDLE)
    VkUnmapMemory(YonaVulkanDevice, MapMemIn);
  if (MapMemFlags != VK_NULL_HANDLE)
    VkUnmapMemory(YonaVulkanDevice, MapMemFlags);
  if (MapMemPrefix != VK_NULL_HANDLE)
    VkUnmapMemory(YonaVulkanDevice, MapMemPrefix);
  if (MapMemOut != VK_NULL_HANDLE)
    VkUnmapMemory(YonaVulkanDevice, MapMemOut);
  if (MapMemScan0 != VK_NULL_HANDLE)
    VkUnmapMemory(YonaVulkanDevice, MapMemScan0);
  if (MapMemScan1 != VK_NULL_HANDLE)
    VkUnmapMemory(YonaVulkanDevice, MapMemScan1);
  if (MapMemCount != VK_NULL_HANDLE)
    VkUnmapMemory(YonaVulkanDevice, MapMemCount);
  PIn = PFlags = PPrefix = POut = NULL;
  PScan0 = PScan1 = PCount = NULL;
  MapMemIn = MapMemFlags = MapMemPrefix = MapMemOut = VK_NULL_HANDLE;
  MapMemScan0 = MapMemScan1 = MapMemCount = VK_NULL_HANDLE;

  VkDestroyFence(YonaVulkanDevice, Fence, NULL);
  Fence = VK_NULL_HANDLE;
  VkFreeCommandBuffers(YonaVulkanDevice, Cpool, 2, Cmds);
  Cmds[0] = Cmds[1] = VK_NULL_HANDLE;
  VkDestroyCommandPool(YonaVulkanDevice, Cpool, NULL);
  Cpool = VK_NULL_HANDLE;
  if (UseStaging) {
    VkDestroyBuffer(YonaVulkanDevice, BufInStg, NULL);
    VkFreeMemory(YonaVulkanDevice, MemInStg, NULL);
    VkDestroyBuffer(YonaVulkanDevice, BufInDev, NULL);
    VkFreeMemory(YonaVulkanDevice, MemInDev, NULL);
    VkDestroyBuffer(YonaVulkanDevice, BufFlagsStg, NULL);
    VkFreeMemory(YonaVulkanDevice, MemFlagsStg, NULL);
    VkDestroyBuffer(YonaVulkanDevice, BufFlagsDev, NULL);
    VkFreeMemory(YonaVulkanDevice, MemFlagsDev, NULL);
    VkDestroyBuffer(YonaVulkanDevice, BufPrefixStg, NULL);
    VkFreeMemory(YonaVulkanDevice, MemPrefixStg, NULL);
    VkDestroyBuffer(YonaVulkanDevice, BufPrefixDev, NULL);
    VkFreeMemory(YonaVulkanDevice, MemPrefixDev, NULL);
    VkDestroyBuffer(YonaVulkanDevice, BufOutStg, NULL);
    VkFreeMemory(YonaVulkanDevice, MemOutStg, NULL);
    VkDestroyBuffer(YonaVulkanDevice, BufOutDev, NULL);
    VkFreeMemory(YonaVulkanDevice, MemOutDev, NULL);
    BufInStg = BufInDev = VK_NULL_HANDLE;
    MemInStg = MemInDev = VK_NULL_HANDLE;
    BufFlagsStg = BufFlagsDev = VK_NULL_HANDLE;
    MemFlagsStg = MemFlagsDev = VK_NULL_HANDLE;
    BufPrefixStg = BufPrefixDev = VK_NULL_HANDLE;
    MemPrefixStg = MemPrefixDev = VK_NULL_HANDLE;
    BufOutStg = BufOutDev = VK_NULL_HANDLE;
    MemOutStg = MemOutDev = VK_NULL_HANDLE;
    if (BufScan0Dev != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufScan0Dev, NULL);
    if (MemScan0Dev != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemScan0Dev, NULL);
    if (BufScan1Dev != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufScan1Dev, NULL);
    if (MemScan1Dev != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemScan1Dev, NULL);
    if (BufCountStg != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufCountStg, NULL);
    if (MemCountStg != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemCountStg, NULL);
    BufScan0Dev = BufScan1Dev = VK_NULL_HANDLE;
    MemScan0Dev = MemScan1Dev = VK_NULL_HANDLE;
    BufCountStg = VK_NULL_HANDLE;
    MemCountStg = VK_NULL_HANDLE;
  } else {
    VkDestroyBuffer(YonaVulkanDevice, BufIn, NULL);
    VkDestroyBuffer(YonaVulkanDevice, BufFlags, NULL);
    VkDestroyBuffer(YonaVulkanDevice, BufPrefix, NULL);
    VkDestroyBuffer(YonaVulkanDevice, BufOut, NULL);
    if (BufScan0 != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufScan0, NULL);
    if (BufScan1 != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufScan1, NULL);
    if (MemScan0 != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemScan0, NULL);
    if (MemScan1 != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemScan1, NULL);
    BufScan0 = BufScan1 = VK_NULL_HANDLE;
    MemScan0 = MemScan1 = VK_NULL_HANDLE;
    BufIn = BufFlags = BufPrefix = BufOut = VK_NULL_HANDLE;
    VkFreeMemory(YonaVulkanDevice, MemIn, NULL);
    VkFreeMemory(YonaVulkanDevice, MemFlags, NULL);
    VkFreeMemory(YonaVulkanDevice, MemPrefix, NULL);
    VkFreeMemory(YonaVulkanDevice, MemOut, NULL);
    MemIn = MemFlags = MemPrefix = MemOut = VK_NULL_HANDLE;
  }
  VkDestroyDescriptorPool(YonaVulkanDevice, Dpool, NULL);
  Dpool = VK_NULL_HANDLE;

  *Out = Result;
  free(PackedIn);
  yonaVulkanComputeSubmitUnlock();
  return 1;

FiltFail:
  if (!YonaVulkanLastNote[0]) {
    char B[120];
    snprintf(B, sizeof B, "filter: Vulkan failure VkResult=%d", (int)R);
    yonaVulkanNoteCopy(B);
  }
  if (MapMemIn != VK_NULL_HANDLE)
    VkUnmapMemory(YonaVulkanDevice, MapMemIn);
  if (MapMemFlags != VK_NULL_HANDLE)
    VkUnmapMemory(YonaVulkanDevice, MapMemFlags);
  if (MapMemPrefix != VK_NULL_HANDLE)
    VkUnmapMemory(YonaVulkanDevice, MapMemPrefix);
  if (MapMemOut != VK_NULL_HANDLE)
    VkUnmapMemory(YonaVulkanDevice, MapMemOut);
  if (MapMemScan0 != VK_NULL_HANDLE)
    VkUnmapMemory(YonaVulkanDevice, MapMemScan0);
  if (MapMemScan1 != VK_NULL_HANDLE)
    VkUnmapMemory(YonaVulkanDevice, MapMemScan1);
  if (MapMemCount != VK_NULL_HANDLE)
    VkUnmapMemory(YonaVulkanDevice, MapMemCount);
  if (Fence != VK_NULL_HANDLE)
    VkDestroyFence(YonaVulkanDevice, Fence, NULL);
  if (Cmds[0] != VK_NULL_HANDLE && Cpool != VK_NULL_HANDLE)
    VkFreeCommandBuffers(YonaVulkanDevice, Cpool, 2, Cmds);
  if (Cpool != VK_NULL_HANDLE)
    VkDestroyCommandPool(YonaVulkanDevice, Cpool, NULL);
  if (UseStaging) {
    if (BufInStg != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufInStg, NULL);
    if (MemInStg != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemInStg, NULL);
    if (BufInDev != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufInDev, NULL);
    if (MemInDev != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemInDev, NULL);
    if (BufFlagsStg != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufFlagsStg, NULL);
    if (MemFlagsStg != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemFlagsStg, NULL);
    if (BufFlagsDev != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufFlagsDev, NULL);
    if (MemFlagsDev != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemFlagsDev, NULL);
    if (BufPrefixStg != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufPrefixStg, NULL);
    if (MemPrefixStg != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemPrefixStg, NULL);
    if (BufPrefixDev != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufPrefixDev, NULL);
    if (MemPrefixDev != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemPrefixDev, NULL);
    if (BufOutStg != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufOutStg, NULL);
    if (MemOutStg != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemOutStg, NULL);
    if (BufOutDev != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufOutDev, NULL);
    if (MemOutDev != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemOutDev, NULL);
    if (BufScan0Dev != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufScan0Dev, NULL);
    if (MemScan0Dev != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemScan0Dev, NULL);
    if (BufScan1Dev != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufScan1Dev, NULL);
    if (MemScan1Dev != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemScan1Dev, NULL);
    if (BufCountStg != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufCountStg, NULL);
    if (MemCountStg != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemCountStg, NULL);
  } else {
    if (BufIn != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufIn, NULL);
    if (BufFlags != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufFlags, NULL);
    if (BufPrefix != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufPrefix, NULL);
    if (BufOut != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufOut, NULL);
    if (BufScan0 != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufScan0, NULL);
    if (BufScan1 != VK_NULL_HANDLE)
      VkDestroyBuffer(YonaVulkanDevice, BufScan1, NULL);
    if (MemIn != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemIn, NULL);
    if (MemFlags != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemFlags, NULL);
    if (MemPrefix != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemPrefix, NULL);
    if (MemOut != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemOut, NULL);
    if (MemScan0 != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemScan0, NULL);
    if (MemScan1 != VK_NULL_HANDLE)
      VkFreeMemory(YonaVulkanDevice, MemScan1, NULL);
  }
  if (Dpool != VK_NULL_HANDLE)
    VkDestroyDescriptorPool(YonaVulkanDevice, Dpool, NULL);
  free(PackedIn);
  yonaVulkanComputeSubmitUnlock();
  return 0;
}

static int yonaVulkanEnvGraph(void) {
  if (yonaVulkanEnvCompute())
    return 1;
  const char *G = getenv("YONA_GPU_VULKAN_GRAPH");
  return G && strcmp(G, "1") == 0;
}

static void
yonaVulkanBarrier2Ssbo(PFN_vkCmdPipelineBarrier2KHR PfnB2, VkCommandBuffer Cmd,
                       VkPipelineStageFlags2 Src, VkPipelineStageFlags2 Dst,
                       VkAccessFlags2 SrcAcc, VkAccessFlags2 DstAcc) {
  VkMemoryBarrier2 Mb = {0};
  Mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
  Mb.srcStageMask = Src;
  Mb.dstStageMask = Dst;
  Mb.srcAccessMask = SrcAcc;
  Mb.dstAccessMask = DstAcc;
  VkDependencyInfo Dep = {0};
  Dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  Dep.memoryBarrierCount = 1;
  Dep.pMemoryBarriers = &Mb;
  PfnB2(Cmd, &Dep);
}

static int gpuVulkanTryMapReduceGraphInt64Impl(int64_t *Stages, int64_t *Arr,
                                               int64_t *OutSum) {
  *OutSum = 0;
  if (!Stages || !Arr)
    return 0;
  if (!yonaVulkanEnvGraph()) {
    yonaVulkanNoteCopy(
        "graph: set YONA_GPU_VULKAN_GRAPH=1 or YONA_GPU_VULKAN_COMPUTE=1");
    return 0;
  }
  if (!yonaVulkanCommonPrecheck(Arr, "graph"))
    return 0;

  int64_t Nflat = Stages[0];
  if (Nflat < 0 || (Nflat % 2) != 0) {
    yonaVulkanNoteCopy(
        "graph: stage IntArray length must be even (Op, Arg) pairs");
    return 0;
  }
  int64_t Nstages = Nflat / 2;
  if (Nstages > 64) {
    yonaVulkanNoteCopy("graph: too many map Stages");
    return 0;
  }
  for (int64_t S = 0; S < Nstages; S++) {
    int64_t Op = Stages[1 + S * 2];
    if (Op != 0 && Op != 1 && Op != 2) {
      yonaVulkanNoteCopy("graph: unknown map Op tag");
      return 0;
    }
  }

  int64_t MinLen = readMinimumLength("YONA_GPU_VULKAN_MIN_LEN",
                                     "YONA_GPU_VULKAN_GRAPH_MIN_LEN");
  int64_t Len = Arr[0];
  if (Len < MinLen) {
    yonaVulkanNoteCopy(
        "graph: IntArray shorter than configured GPU min length");
    return 0;
  }
  if (Len > (int64_t)0x7fffffff) {
    yonaVulkanNoteCopy("graph: IntArray length exceeds supported range");
    return 0;
  }

  int UseI32 = yonaVulkanPreferI32();
  if (UseI32) {
    int64_t *Scratch = (int64_t *)malloc((size_t)(Len + 1) * sizeof(int64_t));
    if (!Scratch)
      return 0;
    Scratch[0] = Len;
    memcpy(Scratch + 1, Arr + 1, (size_t)Len * sizeof(int64_t));
    for (int64_t S = 0; S < Nstages; S++) {
      int64_t Op = Stages[1 + S * 2];
      int64_t Arg = Stages[1 + S * 2 + 1];
      int Okfit = 0;
      if (Op == 0)
        Okfit = yonaVulkanI32MapAddFits(Scratch, Arg);
      else if (Op == 1)
        Okfit = yonaVulkanI32MapMulFits(Scratch, Arg);
      else if (Op == 2)
        Okfit = yonaVulkanI32MapSquareFits(Scratch);
      else {
        free(Scratch);
        yonaVulkanNoteCopy("graph: unknown map Op tag");
        return 0;
      }
      if (!Okfit) {
        free(Scratch);
        yonaVulkanNoteCopy("graph: values exceed int32; GPU i32 path skipped");
        return 0;
      }
      for (int64_t I = 0; I < Len; I++) {
        if (Op == 0)
          Scratch[1 + I] += Arg;
        else if (Op == 1)
          Scratch[1 + I] *= Arg;
        else
          Scratch[1 + I] *= Scratch[1 + I];
      }
    }
    if (!yonaVulkanI32ReduceFits(Scratch)) {
      free(Scratch);
      yonaVulkanNoteCopy("graph: reduce exceeds int32; GPU i32 path skipped");
      return 0;
    }
    free(Scratch);
  }

  PFN_vkCreateDescriptorPool VkCreateDescriptorPool;
  PFN_vkDestroyDescriptorPool VkDestroyDescriptorPool;
  PFN_vkAllocateDescriptorSets VkAllocateDescriptorSets;
  PFN_vkUpdateDescriptorSets VkUpdateDescriptorSets;
  PFN_vkCreateBuffer VkCreateBuffer;
  PFN_vkDestroyBuffer VkDestroyBuffer;
  PFN_vkGetBufferMemoryRequirements VkGetBufferMemoryRequirements;
  PFN_vkAllocateMemory VkAllocateMemory;
  PFN_vkFreeMemory VkFreeMemory;
  PFN_vkBindBufferMemory VkBindBufferMemory;
  PFN_vkMapMemory VkMapMemory;
  PFN_vkUnmapMemory VkUnmapMemory;
  PFN_vkInvalidateMappedMemoryRanges VkInvalidateMappedMemoryRanges;
  PFN_vkCreateCommandPool VkCreateCommandPool;
  PFN_vkDestroyCommandPool VkDestroyCommandPool;
  PFN_vkAllocateCommandBuffers VkAllocateCommandBuffers;
  PFN_vkFreeCommandBuffers VkFreeCommandBuffers;
  PFN_vkBeginCommandBuffer VkBeginCommandBuffer;
  PFN_vkEndCommandBuffer VkEndCommandBuffer;
  PFN_vkCmdBindPipeline VkCmdBindPipeline;
  PFN_vkCmdBindDescriptorSets VkCmdBindDescriptorSets;
  PFN_vkCmdPushConstants VkCmdPushConstants;
  PFN_vkCmdDispatch VkCmdDispatch;
  PFN_vkCmdPipelineBarrier VkCmdPipelineBarrier;
  PFN_vkCreateFence VkCreateFence;
  PFN_vkDestroyFence VkDestroyFence;
  PFN_vkQueueSubmit VkQueueSubmit;
  PFN_vkWaitForFences VkWaitForFences;
  PFN_vkResetFences VkResetFences;
  if (!yonaVulkanLoadDispatchFunctions(
          &VkCreateDescriptorPool, &VkDestroyDescriptorPool,
          &VkAllocateDescriptorSets, &VkUpdateDescriptorSets, &VkCreateBuffer,
          &VkDestroyBuffer, &VkGetBufferMemoryRequirements, &VkAllocateMemory,
          &VkFreeMemory, &VkBindBufferMemory, &VkMapMemory, &VkUnmapMemory,
          &VkInvalidateMappedMemoryRanges, &VkCreateCommandPool,
          &VkDestroyCommandPool, &VkAllocateCommandBuffers,
          &VkFreeCommandBuffers, &VkBeginCommandBuffer, &VkEndCommandBuffer,
          &VkCmdBindPipeline, &VkCmdBindDescriptorSets, &VkCmdPushConstants,
          &VkCmdDispatch, &VkCmdPipelineBarrier, &VkCreateFence,
          &VkDestroyFence, &VkQueueSubmit, &VkWaitForFences, &VkResetFences)) {
    yonaVulkanNoteCopy(
        "graph: vkGetDeviceProcAddr returned null for A required entry point");
    return 0;
  }

  PFN_vkQueueSubmit2KHR PfnSubmit2 =
      (PFN_vkQueueSubmit2KHR)(void *)YonaVulkanGetDeviceProcAddress(
          YonaVulkanDevice, "vkQueueSubmit2KHR");
  if (!PfnSubmit2)
    PfnSubmit2 = (PFN_vkQueueSubmit2KHR)(void *)YonaVulkanGetDeviceProcAddress(
        YonaVulkanDevice, "vkQueueSubmit2");
  PFN_vkWaitSemaphoresKHR PfnWaitSem =
      (PFN_vkWaitSemaphoresKHR)(void *)YonaVulkanGetDeviceProcAddress(
          YonaVulkanDevice, "vkWaitSemaphoresKHR");
  if (!PfnWaitSem)
    PfnWaitSem =
        (PFN_vkWaitSemaphoresKHR)(void *)YonaVulkanGetDeviceProcAddress(
            YonaVulkanDevice, "vkWaitSemaphores");
  PFN_vkCmdPipelineBarrier2KHR PfnB2 =
      (PFN_vkCmdPipelineBarrier2KHR)(void *)YonaVulkanGetDeviceProcAddress(
          YonaVulkanDevice, "vkCmdPipelineBarrier2KHR");
  if (!PfnB2)
    PfnB2 =
        (PFN_vkCmdPipelineBarrier2KHR)(void *)YonaVulkanGetDeviceProcAddress(
            YonaVulkanDevice, "vkCmdPipelineBarrier2");
  PFN_vkCreateSemaphore VkCreateSemaphore =
      YONA_VULKAN_DEVICE_PROCEDURE(vkCreateSemaphore);
  PFN_vkDestroySemaphore VkDestroySemaphore =
      YONA_VULKAN_DEVICE_PROCEDURE(vkDestroySemaphore);

  int UseSync2 = YonaVulkanSynchronization2Enabled && PfnSubmit2 &&
                 PfnWaitSem && PfnB2 && VkCreateSemaphore && VkDestroySemaphore;

  yonaVulkanComputeSubmitLock();

  VkResult R;
  if (UseI32)
    R = yonaVulkanComputeEnsureMapAddI32Pipe();
  else
    R = yonaVulkanComputeEnsureMapAddPipe();
  if (R != VK_SUCCESS) {
    yonaVulkanComputeSubmitUnlock();
    return 0;
  }
  if (UseI32)
    R = yonaVulkanComputeEnsureMapMulI32Pipe();
  else
    R = yonaVulkanComputeEnsureMapMulPipe();
  if (R != VK_SUCCESS) {
    yonaVulkanComputeSubmitUnlock();
    return 0;
  }
  if (UseI32)
    R = yonaVulkanComputeEnsureMapSquareI32Pipe();
  else
    R = yonaVulkanComputeEnsureMapSquarePipe();
  if (R != VK_SUCCESS) {
    yonaVulkanComputeSubmitUnlock();
    return 0;
  }
  if (UseI32)
    R = yonaVulkanComputeEnsureReduceI32Pipe();
  else
    R = yonaVulkanComputeEnsureReducePipe();
  if (R != VK_SUCCESS) {
    yonaVulkanComputeSubmitUnlock();
    return 0;
  }

  YonaVulkanSimplePipeline *PAdd =
      UseI32 ? yonaVulkanComputeMapAddI32Pipe() : yonaVulkanComputeMapAddPipe();
  YonaVulkanSimplePipeline *PMul =
      UseI32 ? yonaVulkanComputeMapMulI32Pipe() : yonaVulkanComputeMapMulPipe();
  YonaVulkanSimplePipeline *PSq = UseI32 ? yonaVulkanComputeMapSquareI32Pipe()
                                         : yonaVulkanComputeMapSquarePipe();
  YonaVulkanReducePipeline *PRed =
      UseI32 ? yonaVulkanComputeReduceI32Pipe() : yonaVulkanComputeReducePipe();

  uint32_t Ulen = (uint32_t)Len;
  uint32_t Groups = (Ulen + 63u) / 64u;
  size_t Esz = UseI32 ? sizeof(int32_t) : sizeof(int64_t);
  VkDeviceSize NbytesIn = (VkDeviceSize)((size_t)Len * Esz);
  VkDeviceSize NbytesSums = (VkDeviceSize)((size_t)Groups * Esz);
  int32_t *PackedIn = NULL;
  if (UseI32) {
    PackedIn = (int32_t *)malloc((size_t)Len * sizeof(int32_t));
    if (!PackedIn) {
      yonaVulkanComputeSubmitUnlock();
      return 0;
    }
    for (int64_t I = 0; I < Len; I++)
      PackedIn[I] = (int32_t)Arr[1 + I];
  }
  const void *HostSrc =
      UseI32 ? (const void *)PackedIn : (const void *)(Arr + 1);

  VkDescriptorPool Dpool = VK_NULL_HANDLE;
  VkDescriptorSet DsetAdd = VK_NULL_HANDLE;
  VkDescriptorSet DsetMul = VK_NULL_HANDLE;
  VkDescriptorSet DsetSq = VK_NULL_HANDLE;
  VkDescriptorSet DsetRed = VK_NULL_HANDLE;
  VkBuffer BufIn = VK_NULL_HANDLE;
  VkBuffer BufSums = VK_NULL_HANDLE;
  VkDeviceMemory MemIn = VK_NULL_HANDLE;
  VkDeviceMemory MemSums = VK_NULL_HANDLE;
  VkCommandPool Cpool = VK_NULL_HANDLE;
  VkCommandBuffer Cmd = VK_NULL_HANDLE;
  VkFence Fence = VK_NULL_HANDLE;
  VkSemaphore Timeline = VK_NULL_HANDLE;
  void *MappedIn = NULL;
  void *MappedSums = NULL;
  int Ok = 0;

  VkDescriptorPoolSize Dps = {0};
  Dps.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  Dps.descriptorCount = 8;
  VkDescriptorPoolCreateInfo Dpci = {0};
  Dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  Dpci.maxSets = 4;
  Dpci.poolSizeCount = 1;
  Dpci.pPoolSizes = &Dps;
  R = VkCreateDescriptorPool(YonaVulkanDevice, &Dpci, NULL, &Dpool);
  if (R != VK_SUCCESS)
    goto GraphFail;

  VkDescriptorSetAllocateInfo DsaiA = {0};
  DsaiA.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  DsaiA.descriptorPool = Dpool;
  DsaiA.descriptorSetCount = 1;
  DsaiA.pSetLayouts = &PAdd->DescriptorSetLayout;
  R = VkAllocateDescriptorSets(YonaVulkanDevice, &DsaiA, &DsetAdd);
  if (R != VK_SUCCESS)
    goto GraphFail;
  VkDescriptorSetAllocateInfo DsaiM = {0};
  DsaiM.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  DsaiM.descriptorPool = Dpool;
  DsaiM.descriptorSetCount = 1;
  DsaiM.pSetLayouts = &PMul->DescriptorSetLayout;
  R = VkAllocateDescriptorSets(YonaVulkanDevice, &DsaiM, &DsetMul);
  if (R != VK_SUCCESS)
    goto GraphFail;
  VkDescriptorSetAllocateInfo DsaiS = {0};
  DsaiS.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  DsaiS.descriptorPool = Dpool;
  DsaiS.descriptorSetCount = 1;
  DsaiS.pSetLayouts = &PSq->DescriptorSetLayout;
  R = VkAllocateDescriptorSets(YonaVulkanDevice, &DsaiS, &DsetSq);
  if (R != VK_SUCCESS)
    goto GraphFail;
  VkDescriptorSetAllocateInfo DsaiR = {0};
  DsaiR.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  DsaiR.descriptorPool = Dpool;
  DsaiR.descriptorSetCount = 1;
  DsaiR.pSetLayouts = &PRed->DescriptorSetLayout;
  R = VkAllocateDescriptorSets(YonaVulkanDevice, &DsaiR, &DsetRed);
  if (R != VK_SUCCESS)
    goto GraphFail;

  VkBufferCreateInfo Bci = {0};
  Bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  Bci.size = NbytesIn;
  Bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  Bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  R = VkCreateBuffer(YonaVulkanDevice, &Bci, NULL, &BufIn);
  if (R != VK_SUCCESS)
    goto GraphFail;
  Bci.size = NbytesSums;
  R = VkCreateBuffer(YonaVulkanDevice, &Bci, NULL, &BufSums);
  if (R != VK_SUCCESS)
    goto GraphFail;

  VkMemoryRequirements RqIn, RqSums;
  VkGetBufferMemoryRequirements(YonaVulkanDevice, BufIn, &RqIn);
  VkGetBufferMemoryRequirements(YonaVulkanDevice, BufSums, &RqSums);
  uint32_t MtIn = 0, MtSums = 0;
  if (yonaVulkanPickMemoryType(RqIn.memoryTypeBits,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               &MtIn) != 0) {
    yonaVulkanNoteCopy(
        "graph: no HOST_VISIBLE|HOST_COHERENT memory for column SSBO");
    goto GraphFail;
  }
  if (yonaVulkanPickMemoryType(RqSums.memoryTypeBits,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               &MtSums) != 0) {
    yonaVulkanNoteCopy(
        "graph: no HOST_VISIBLE|HOST_COHERENT memory for reduce SSBO");
    goto GraphFail;
  }
  VkMemoryAllocateInfo Mai = {0};
  Mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  Mai.allocationSize = RqIn.size;
  Mai.memoryTypeIndex = MtIn;
  R = VkAllocateMemory(YonaVulkanDevice, &Mai, NULL, &MemIn);
  if (R != VK_SUCCESS)
    goto GraphFail;
  Mai.allocationSize = RqSums.size;
  Mai.memoryTypeIndex = MtSums;
  R = VkAllocateMemory(YonaVulkanDevice, &Mai, NULL, &MemSums);
  if (R != VK_SUCCESS)
    goto GraphFail;
  R = VkBindBufferMemory(YonaVulkanDevice, BufIn, MemIn, 0);
  if (R != VK_SUCCESS)
    goto GraphFail;
  R = VkBindBufferMemory(YonaVulkanDevice, BufSums, MemSums, 0);
  if (R != VK_SUCCESS)
    goto GraphFail;

  R = VkMapMemory(YonaVulkanDevice, MemIn, 0, NbytesIn, 0, &MappedIn);
  if (R != VK_SUCCESS)
    goto GraphFail;
  R = VkMapMemory(YonaVulkanDevice, MemSums, 0, NbytesSums, 0, &MappedSums);
  if (R != VK_SUCCESS)
    goto GraphFail;
  memcpy(MappedIn, HostSrc, (size_t)NbytesIn);
  memset(MappedSums, 0, (size_t)NbytesSums);

  VkDescriptorBufferInfo DbiIn = {0};
  DbiIn.buffer = BufIn;
  DbiIn.offset = 0;
  DbiIn.range = NbytesIn;
  VkWriteDescriptorSet WAdd = {0};
  WAdd.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  WAdd.dstSet = DsetAdd;
  WAdd.dstBinding = 0;
  WAdd.descriptorCount = 1;
  WAdd.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  WAdd.pBufferInfo = &DbiIn;
  VkWriteDescriptorSet WMul = WAdd;
  WMul.dstSet = DsetMul;
  VkWriteDescriptorSet WSq = WAdd;
  WSq.dstSet = DsetSq;
  VkUpdateDescriptorSets(YonaVulkanDevice, 1, &WAdd, 0, NULL);
  VkUpdateDescriptorSets(YonaVulkanDevice, 1, &WMul, 0, NULL);
  VkUpdateDescriptorSets(YonaVulkanDevice, 1, &WSq, 0, NULL);

  VkDescriptorBufferInfo DbiRed[2] = {0};
  DbiRed[0] = DbiIn;
  DbiRed[1].buffer = BufSums;
  DbiRed[1].offset = 0;
  DbiRed[1].range = NbytesSums;
  VkWriteDescriptorSet WRed[2] = {0};
  WRed[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  WRed[0].dstSet = DsetRed;
  WRed[0].dstBinding = 0;
  WRed[0].descriptorCount = 1;
  WRed[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  WRed[0].pBufferInfo = &DbiRed[0];
  WRed[1] = WRed[0];
  WRed[1].dstBinding = 1;
  WRed[1].pBufferInfo = &DbiRed[1];
  VkUpdateDescriptorSets(YonaVulkanDevice, 2, WRed, 0, NULL);

  VkCommandPoolCreateInfo Cpci0 = {0};
  Cpci0.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  Cpci0.queueFamilyIndex = YonaVulkanQueueFamily;
  Cpci0.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  R = VkCreateCommandPool(YonaVulkanDevice, &Cpci0, NULL, &Cpool);
  if (R != VK_SUCCESS)
    goto GraphFail;
  VkCommandBufferAllocateInfo Cbai = {0};
  Cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  Cbai.commandPool = Cpool;
  Cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  Cbai.commandBufferCount = 1;
  R = VkAllocateCommandBuffers(YonaVulkanDevice, &Cbai, &Cmd);
  if (R != VK_SUCCESS)
    goto GraphFail;

  VkCommandBufferBeginInfo Bi = {0};
  Bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  Bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  R = VkBeginCommandBuffer(Cmd, &Bi);
  if (R != VK_SUCCESS)
    goto GraphFail;

  if (UseSync2) {
    yonaVulkanBarrier2Ssbo(
        PfnB2, Cmd, VK_PIPELINE_STAGE_2_HOST_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_HOST_WRITE_BIT,
        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
  } else {
    VkMemoryBarrier Mb = {0};
    Mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    Mb.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    Mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    VkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_HOST_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &Mb, 0,
                         NULL, 0, NULL);
  }

  for (int64_t S = 0; S < Nstages; S++) {
    int64_t Op = Stages[1 + S * 2];
    int64_t Arg = Stages[1 + S * 2 + 1];
    YonaVulkanSimplePipeline *Pipe;
    VkDescriptorSet Dset;
    if (Op == 0) {
      Pipe = PAdd;
      Dset = DsetAdd;
    } else if (Op == 1) {
      Pipe = PMul;
      Dset = DsetMul;
    } else {
      Pipe = PSq;
      Dset = DsetSq;
    }
    VkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, Pipe->Pipeline);
    VkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            Pipe->PipelineLayout, 0, 1, &Dset, 0, NULL);
    char Pc[16];
    uint32_t PushBytes;
    if (UseI32) {
      int32_t S32 = (int32_t)Arg;
      memcpy(Pc, &S32, sizeof(int32_t));
      memcpy(Pc + sizeof(int32_t), &Ulen, sizeof(uint32_t));
      PushBytes = 8;
    } else {
      memcpy(Pc, &Arg, sizeof(int64_t));
      memcpy(Pc + sizeof(int64_t), &Ulen, sizeof(uint32_t));
      PushBytes = 12;
    }
    VkCmdPushConstants(Cmd, Pipe->PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, PushBytes, Pc);
    VkCmdDispatch(Cmd, Groups, 1, 1);
    if (UseSync2) {
      yonaVulkanBarrier2Ssbo(
          PfnB2, Cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
          VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
    } else {
      VkMemoryBarrier Mb = {0};
      Mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
      Mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
      Mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
      VkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &Mb, 0,
                           NULL, 0, NULL);
    }
  }

  VkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, PRed->Pipeline);
  VkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          PRed->PipelineLayout, 0, 1, &DsetRed, 0, NULL);
  VkCmdPushConstants(Cmd, PRed->PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                     4, &Ulen);
  VkCmdDispatch(Cmd, Groups, 1, 1);

  if (UseSync2) {
    yonaVulkanBarrier2Ssbo(PfnB2, Cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_2_HOST_BIT,
                           VK_ACCESS_2_SHADER_WRITE_BIT,
                           VK_ACCESS_2_HOST_READ_BIT);
  } else {
    VkMemoryBarrier Mb = {0};
    Mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    Mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    Mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    VkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &Mb, 0, NULL, 0,
                         NULL);
  }

  R = VkEndCommandBuffer(Cmd);
  if (R != VK_SUCCESS)
    goto GraphFail;

  uint64_t TlVal = 1;
  if (UseSync2) {
    VkSemaphoreTypeCreateInfo Sti = {0};
    Sti.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    Sti.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo Sci = {0};
    Sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    Sci.pNext = &Sti;
    R = VkCreateSemaphore(YonaVulkanDevice, &Sci, NULL, &Timeline);
    if (R != VK_SUCCESS)
      goto GraphFail;

    VkCommandBufferSubmitInfo Cbs = {0};
    Cbs.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    Cbs.commandBuffer = Cmd;
    VkSemaphoreSubmitInfo Sig = {0};
    Sig.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    Sig.semaphore = Timeline;
    Sig.value = TlVal;
    Sig.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 Si2 = {0};
    Si2.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    Si2.commandBufferInfoCount = 1;
    Si2.pCommandBufferInfos = &Cbs;
    Si2.signalSemaphoreInfoCount = 1;
    Si2.pSignalSemaphoreInfos = &Sig;
    R = PfnSubmit2(YonaVulkanQueue, 1, &Si2, VK_NULL_HANDLE);
    if (R != VK_SUCCESS)
      goto GraphFail;
    VkSemaphoreWaitInfo Wi = {0};
    Wi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    Wi.semaphoreCount = 1;
    Wi.pSemaphores = &Timeline;
    Wi.pValues = &TlVal;
    R = PfnWaitSem(YonaVulkanDevice, &Wi, UINT64_MAX);
    if (R != VK_SUCCESS)
      goto GraphFail;
  } else {
    VkFenceCreateInfo Fci = {0};
    Fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    R = VkCreateFence(YonaVulkanDevice, &Fci, NULL, &Fence);
    if (R != VK_SUCCESS)
      goto GraphFail;
    VkSubmitInfo Si = {0};
    Si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    Si.commandBufferCount = 1;
    Si.pCommandBuffers = &Cmd;
    R = VkQueueSubmit(YonaVulkanQueue, 1, &Si, Fence);
    if (R != VK_SUCCESS)
      goto GraphFail;
    R = VkWaitForFences(YonaVulkanDevice, 1, &Fence, VK_TRUE, UINT64_MAX);
    if (R != VK_SUCCESS)
      goto GraphFail;
  }

  if (VkInvalidateMappedMemoryRanges) {
    VkMappedMemoryRange Inv = {0};
    Inv.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    Inv.memory = MemSums;
    Inv.offset = 0;
    Inv.size = NbytesSums;
    VkInvalidateMappedMemoryRanges(YonaVulkanDevice, 1, &Inv);
  }

  int64_t Total = 0;
  if (UseI32) {
    const int32_t *Lanes = (const int32_t *)MappedSums;
    for (uint32_t I = 0; I < Groups; I++)
      Total += (int64_t)Lanes[I];
  } else {
    const int64_t *Lanes = (const int64_t *)MappedSums;
    for (uint32_t I = 0; I < Groups; I++)
      Total += Lanes[I];
  }
  *OutSum = Total;
  Ok = 1;

GraphFail:
  if (MappedIn)
    VkUnmapMemory(YonaVulkanDevice, MemIn);
  if (MappedSums)
    VkUnmapMemory(YonaVulkanDevice, MemSums);
  if (Fence != VK_NULL_HANDLE)
    VkDestroyFence(YonaVulkanDevice, Fence, NULL);
  if (Timeline != VK_NULL_HANDLE)
    VkDestroySemaphore(YonaVulkanDevice, Timeline, NULL);
  if (Cmd != VK_NULL_HANDLE && Cpool != VK_NULL_HANDLE)
    VkFreeCommandBuffers(YonaVulkanDevice, Cpool, 1, &Cmd);
  if (Cpool != VK_NULL_HANDLE)
    VkDestroyCommandPool(YonaVulkanDevice, Cpool, NULL);
  if (BufIn != VK_NULL_HANDLE)
    VkDestroyBuffer(YonaVulkanDevice, BufIn, NULL);
  if (BufSums != VK_NULL_HANDLE)
    VkDestroyBuffer(YonaVulkanDevice, BufSums, NULL);
  if (MemIn != VK_NULL_HANDLE)
    VkFreeMemory(YonaVulkanDevice, MemIn, NULL);
  if (MemSums != VK_NULL_HANDLE)
    VkFreeMemory(YonaVulkanDevice, MemSums, NULL);
  if (Dpool != VK_NULL_HANDLE)
    VkDestroyDescriptorPool(YonaVulkanDevice, Dpool, NULL);
  free(PackedIn);
  yonaVulkanComputeSubmitUnlock();
  return Ok;
}

int YonaRuntimeGpuVulkanTryMapAddInt64(int64_t Delta, int64_t *Arr,
                                       int64_t **Out) {
  if (Out)
    *Out = NULL;
  if (!yonaVulkanOperationBegin())
    return 0;
  int Result = gpuVulkanTryMapAddInt64Impl(Delta, Arr, Out);
  yonaVulkanOperationEnd();
  return Result;
}

int YonaRuntimeGpuVulkanTryMapMulInt64(int64_t Factor, int64_t *Arr,
                                       int64_t **Out) {
  if (Out)
    *Out = NULL;
  if (!yonaVulkanOperationBegin())
    return 0;
  int Result = gpuVulkanTryMapMulInt64Impl(Factor, Arr, Out);
  yonaVulkanOperationEnd();
  return Result;
}

int YonaRuntimeGpuVulkanTryMapSquareInt64(int64_t *Arr, int64_t **Out) {
  if (Out)
    *Out = NULL;
  if (!yonaVulkanOperationBegin())
    return 0;
  int Result = gpuVulkanTryMapSquareInt64Impl(Arr, Out);
  yonaVulkanOperationEnd();
  return Result;
}

int YonaRuntimeGpuVulkanTryReduceSumInt64(int64_t *Arr, int64_t *OutSum) {
  if (OutSum)
    *OutSum = 0;
  if (!yonaVulkanOperationBegin())
    return 0;
  int Result = gpuVulkanTryReduceSumInt64Impl(Arr, OutSum);
  yonaVulkanOperationEnd();
  return Result;
}

int YonaRuntimeGpuVulkanTryFilterGreaterThanInt64(int64_t Threshold,
                                                  int64_t *Arr, int64_t **Out) {
  if (Out)
    *Out = NULL;
  if (!yonaVulkanOperationBegin())
    return 0;
  int Result = gpuVulkanTryFilterGreaterThanInt64Impl(Threshold, Arr, Out);
  yonaVulkanOperationEnd();
  return Result;
}

int YonaRuntimeGpuVulkanTryFilterLessThanInt64(int64_t Threshold, int64_t *Arr,
                                               int64_t **Out) {
  if (Out)
    *Out = NULL;
  if (!yonaVulkanOperationBegin())
    return 0;
  int Result = gpuVulkanTryFilterLessThanInt64Impl(Threshold, Arr, Out);
  yonaVulkanOperationEnd();
  return Result;
}

int YonaRuntimeGpuVulkanTryMapReduceGraphInt64(int64_t *Stages, int64_t *Arr,
                                               int64_t *OutSum) {
  if (OutSum)
    *OutSum = 0;
  if (!yonaVulkanOperationBegin())
    return 0;
  int Result = gpuVulkanTryMapReduceGraphInt64Impl(Stages, Arr, OutSum);
  yonaVulkanOperationEnd();
  return Result;
}

#endif /* YONA_GPU_VULKAN_ENABLED */
