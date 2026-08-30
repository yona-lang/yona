/* ===== Std\Gpu CPU backend =====
 *
 * Portable columnar accelerator fallback. The API deliberately copies on
 * upload/materialize so the CPU path preserves device-style ownership
 * boundaries while remaining available on every host.
 */

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
#include <emmintrin.h>
#define YONA_GPU_CPU_HAS_SSE2 1
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define YONA_GPU_CPU_HAS_NEON 1
#endif

#include "Runtime/Core/Internal.h"
#include "yona/Runtime/Collections/Sequence.h"
#include "yona/Runtime/Core/Api.h"
#include "yona/Runtime/Gpu/BuildConfig.h"
#include "yona/Runtime/Gpu/VulkanDevice.h"

#define YONA_RC_TYPE_ADT YONA_RUNTIME_TYPE_ADT
#define YONA_RC_TYPE_STRING YONA_RUNTIME_TYPE_STRING

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Defined by the core runtime component. Keep all ADT consumers on the
 *
 * canonical accessor API. */
#if YONA_GPU_VULKAN_ENABLED
static atomic_int HasCachedGpu = ATOMIC_VAR_INIT(-1);
#endif

void YonaRuntimeGpuVulkanInvalidateCapabilityCache(void) {
#if YONA_GPU_VULKAN_ENABLED
  atomic_store_explicit(&HasCachedGpu, -1, memory_order_release);
#endif
}

static char *makeGpuString(const char *S) {
  size_t Len = strlen(S);
  char *Out = (char *)YonaRuntimeAllocate(YONA_RC_TYPE_STRING, Len + 1);
  memcpy(Out, S, Len + 1);
  return Out;
}

static int gpuCpuHasSimd(void) {
#if defined(YONA_GPU_CPU_HAS_SSE2) || defined(YONA_GPU_CPU_HAS_NEON)
  return 1;
#else
  return 0;
#endif
}

static int64_t *copyGpuIntArray(int64_t *Arr) {
  int64_t Len = Arr[0];
  int64_t *Out = YonaRuntimeIntArrayAllocate(Len);
  memcpy(Out + 1, Arr + 1, (size_t)Len * sizeof(int64_t));
  return Out;
}

static int64_t *makeGpuBuffer(int64_t *Values) {
  int64_t *Buffer =
      (int64_t *)YonaRuntimeAllocate(YONA_RC_TYPE_ADT, 4 * sizeof(int64_t));
  Buffer[0] = 0; /* Buffer */
  Buffer[1] = 1;
  Buffer[2] = 1; /* field 0 is heap-owned */
  Buffer[3] = (int64_t)(intptr_t)Values;
  return Buffer;
}

static int64_t *gpuBufferValues(int64_t Buffer) {
  int64_t *Adt = (int64_t *)(intptr_t)Buffer;
  return (int64_t *)(intptr_t)Adt[3];
}

const char *YonaStdGpuRawBackendName(int64_t Unused) {
  (void)Unused;
  return makeGpuString(gpuCpuHasSimd() ? "cpu-simd" : "cpu-scalar");
}

const char *YonaStdGpuRawVulkanStatus(int64_t Unused) {
  (void)Unused;
  return makeGpuString(YonaRuntimeGpuVulkanStatusName());
}

const char *YonaStdGpuRawVulkanLastNote(int64_t Unused) {
  (void)Unused;
#if YONA_GPU_VULKAN_ENABLED
  return makeGpuString(YonaRuntimeGpuVulkanDeviceLastNote());
#else
  return makeGpuString("");
#endif
}

int64_t YonaStdGpuRawVulkanLastIssueKind(int64_t Unused) {
  (void)Unused;
#if YONA_GPU_VULKAN_ENABLED
  return (int64_t)YonaRuntimeGpuVulkanDeviceLastIssueKind();
#else
  return 0;
#endif
}

int64_t YonaStdGpuRawHasGpu(int64_t Unused) {
  (void)Unused;
#if YONA_GPU_VULKAN_ENABLED
  int Cached = atomic_load_explicit(&HasCachedGpu, memory_order_acquire);
  if (Cached >= 0)
    return (int64_t)Cached;
  const char *Dis = getenv("YONA_GPU_DISABLE_VULKAN");
  if (Dis && Dis[0] && strcmp(Dis, "0") != 0) {
    atomic_store_explicit(&HasCachedGpu, 0, memory_order_release);
    return 0;
  }
  int Ti = YonaRuntimeGpuVulkanDeviceTryInitialize();
  if (Ti != 0) {
    atomic_store_explicit(&HasCachedGpu, 0, memory_order_release);
    return 0;
  }
  /* Device ready is enough: i64 kernels when shaderInt64, else i32 when values
   * fit. */
  atomic_store_explicit(&HasCachedGpu, 1, memory_order_release);
  return 1;
#else
  return 0;
#endif
}

int64_t YonaStdGpuRawVulkanAvailable(int64_t Unused) {
  (void)Unused;
  return YonaRuntimeGpuVulkanLoaderAvailable() ? 1 : 0;
}

int64_t YonaStdGpuRawVulkanTimelineSemaphore(int64_t Unused) {
  (void)Unused;
#if YONA_GPU_VULKAN_ENABLED
  if (YonaRuntimeGpuVulkanDeviceTryInitialize() != 0)
    return 0;
  return YonaRuntimeGpuVulkanDeviceHasTimelineSemaphore() ? 1 : 0;
#else
  return 0;
#endif
}

int64_t YonaStdGpuRawHasSimd(int64_t Unused) {
  (void)Unused;
  return gpuCpuHasSimd() ? 1 : 0;
}

int64_t *YonaStdGpuRawUpload(int64_t *Arr) { return copyGpuIntArray(Arr); }

int64_t *YonaStdGpuRawMaterialize(int64_t *Arr) { return copyGpuIntArray(Arr); }

int64_t YonaStdGpuRawLength(int64_t *Arr) {
  return YonaRuntimeIntArrayLength(Arr);
}

int64_t *YonaStdGpuRawMapAdd(int64_t Delta, int64_t *Arr) {
#if YONA_GPU_VULKAN_ENABLED
  int64_t *GpuOut = NULL;
  if (YonaRuntimeGpuVulkanTryMapAddInt64(Delta, Arr, &GpuOut))
    return GpuOut;
#endif
  int64_t Len = Arr[0];
  int64_t *Out = YonaRuntimeIntArrayAllocate(Len);
  for (int64_t I = 0; I < Len; I++)
    Out[1 + I] = Arr[1 + I] + Delta;
  return Out;
}

int64_t *YonaStdGpuRawMapMul(int64_t Factor, int64_t *Arr) {
#if YONA_GPU_VULKAN_ENABLED
  int64_t *GpuOut = NULL;
  if (YonaRuntimeGpuVulkanTryMapMulInt64(Factor, Arr, &GpuOut))
    return GpuOut;
#endif
  int64_t Len = Arr[0];
  int64_t *Out = YonaRuntimeIntArrayAllocate(Len);
  for (int64_t I = 0; I < Len; I++)
    Out[1 + I] = Arr[1 + I] * Factor;
  return Out;
}

int64_t *YonaStdGpuRawFilterGreaterThan(int64_t Threshold, int64_t *Arr) {
#if YONA_GPU_VULKAN_ENABLED
  int64_t *GpuOut = NULL;
  if (YonaRuntimeGpuVulkanTryFilterGreaterThanInt64(Threshold, Arr, &GpuOut))
    return GpuOut;
#endif
  int64_t Len = Arr[0];
  int64_t Count = 0;
  for (int64_t I = 0; I < Len; I++)
    if (Arr[1 + I] > Threshold)
      Count++;

  int64_t *Out = YonaRuntimeIntArrayAllocate(Count);
  int64_t J = 0;
  for (int64_t I = 0; I < Len; I++) {
    int64_t V = Arr[1 + I];
    if (V > Threshold)
      Out[1 + J++] = V;
  }
  return Out;
}

int64_t *YonaStdGpuRawMapSquare(int64_t *Arr) {
#if YONA_GPU_VULKAN_ENABLED
  int64_t *GpuOut = NULL;
  if (YonaRuntimeGpuVulkanTryMapSquareInt64(Arr, &GpuOut))
    return GpuOut;
#endif
  int64_t Len = Arr[0];
  int64_t *Out = YonaRuntimeIntArrayAllocate(Len);
  for (int64_t I = 0; I < Len; I++)
    Out[1 + I] = Arr[1 + I] * Arr[1 + I];
  return Out;
}

int64_t *YonaStdGpuRawFilterLessThan(int64_t Threshold, int64_t *Arr) {
#if YONA_GPU_VULKAN_ENABLED
  int64_t *GpuOut = NULL;
  if (YonaRuntimeGpuVulkanTryFilterLessThanInt64(Threshold, Arr, &GpuOut))
    return GpuOut;
#endif
  int64_t Len = Arr[0];
  int64_t Count = 0;
  for (int64_t I = 0; I < Len; I++)
    if (Arr[1 + I] < Threshold)
      Count++;

  int64_t *Out = YonaRuntimeIntArrayAllocate(Count);
  int64_t J = 0;
  for (int64_t I = 0; I < Len; I++) {
    int64_t V = Arr[1 + I];
    if (V < Threshold)
      Out[1 + J++] = V;
  }
  return Out;
}

int64_t YonaStdGpuRawReduceSum(int64_t *Arr) {
#if YONA_GPU_VULKAN_ENABLED
  int64_t GpuSum = 0;
  if (YonaRuntimeGpuVulkanTryReduceSumInt64(Arr, &GpuSum))
    return GpuSum;
#endif
  int64_t Len = Arr[0];
  int64_t *Data = Arr + 1;
  int64_t I = 0;
  int64_t Sum = 0;

#if defined(YONA_GPU_CPU_HAS_SSE2)
  __m128i Acc = _mm_setzero_si128();
  for (; I + 1 < Len; I += 2) {
    __m128i V = _mm_loadu_si128((const __m128i *)(const void *)(Data + I));
    Acc = _mm_add_epi64(Acc, V);
  }
  int64_t Lanes[2];
  _mm_storeu_si128((__m128i *)(void *)Lanes, Acc);
  Sum = Lanes[0] + Lanes[1];
#elif defined(YONA_GPU_CPU_HAS_NEON)
  int64x2_t Acc = vdupq_n_s64(0);
  for (; I + 1 < Len; I += 2) {
    int64x2_t V = vld1q_s64(Data + I);
    Acc = vaddq_s64(Acc, V);
  }
  int64_t Lanes[2];
  vst1q_s64(Lanes, Acc);
  Sum = Lanes[0] + Lanes[1];
#endif

  for (; I < Len; I++)
    Sum += Data[I];
  return Sum;
}

const char *YonaStdGpuBackendName(void) { return YonaStdGpuRawBackendName(0); }

const char *YonaStdGpuVulkanStatus(void) {
  return YonaStdGpuRawVulkanStatus(0);
}

const char *YonaStdGpuVulkanLastNote(void) {
  return YonaStdGpuRawVulkanLastNote(0);
}

int64_t YonaStdGpuHasGpu(void) { return YonaStdGpuRawHasGpu(0); }

int64_t YonaStdGpuVulkanLastIssueKind(void) {
  return YonaStdGpuRawVulkanLastIssueKind(0);
}

int64_t YonaStdGpuVulkanAvailable(void) {
  return YonaStdGpuRawVulkanAvailable(0);
}

int64_t YonaStdGpuVulkanTimelineSemaphore(void) {
  return YonaStdGpuRawVulkanTimelineSemaphore(0);
}

int64_t YonaStdGpuHasSimd(void) { return YonaStdGpuRawHasSimd(0); }

int64_t YonaStdGpuUpload(int64_t Arr) {
  int64_t *Values = YonaStdGpuRawUpload((int64_t *)(intptr_t)Arr);
  return (int64_t)(intptr_t)makeGpuBuffer(Values);
}

int64_t YonaStdGpuMaterialize(int64_t Buffer) {
  int64_t *Values = gpuBufferValues(Buffer);
  return (int64_t)(intptr_t)YonaStdGpuRawMaterialize(Values);
}

int64_t YonaStdGpuLength(int64_t Buffer) {
  return YonaStdGpuRawLength(gpuBufferValues(Buffer));
}

int64_t YonaStdGpuMapAdd(int64_t Delta, int64_t Buffer) {
  int64_t *Values = YonaStdGpuRawMapAdd(Delta, gpuBufferValues(Buffer));
  return (int64_t)(intptr_t)makeGpuBuffer(Values);
}

int64_t YonaStdGpuMapMul(int64_t Factor, int64_t Buffer) {
  int64_t *Values = YonaStdGpuRawMapMul(Factor, gpuBufferValues(Buffer));
  return (int64_t)(intptr_t)makeGpuBuffer(Values);
}

int64_t YonaStdGpuFilterGreaterThan(int64_t Threshold, int64_t Buffer) {
  int64_t *Values =
      YonaStdGpuRawFilterGreaterThan(Threshold, gpuBufferValues(Buffer));
  return (int64_t)(intptr_t)makeGpuBuffer(Values);
}

int64_t YonaStdGpuFilterLessThan(int64_t Threshold, int64_t Buffer) {
  int64_t *Values =
      YonaStdGpuRawFilterLessThan(Threshold, gpuBufferValues(Buffer));
  return (int64_t)(intptr_t)makeGpuBuffer(Values);
}

int64_t YonaStdGpuMapSquare(int64_t Buffer) {
  int64_t *Values = YonaStdGpuRawMapSquare(gpuBufferValues(Buffer));
  return (int64_t)(intptr_t)makeGpuBuffer(Values);
}

int64_t YonaStdGpuReduceSum(int64_t Buffer) {
  return YonaStdGpuRawReduceSum(gpuBufferValues(Buffer));
}

double *YonaStdGpuRawMapFloatOp(int64_t *Op, double *Arr);
double YonaStdGpuRawReduceFloatGpu(double *Arr);
int64_t YonaStdGpuRawMapReduceGraph(int64_t *Stages, int64_t *Arr);
int64_t YonaStdGpuRawAllocPinnedFloats(int64_t N);
int64_t YonaStdGpuRawClosePinnedFloats(int64_t Handle);
int64_t YonaStdGpuRawPinnedLength(int64_t Handle);
double YonaStdGpuRawPinnedGet(int64_t Handle, int64_t I);
int64_t YonaStdGpuRawPinnedSet(int64_t Handle, int64_t I, double V);
double *YonaStdGpuRawPinnedToFloatArray(int64_t Handle);
int64_t YonaStdGpuRawCopyFloatArrayToPinned(double *Arr, int64_t Handle);
const char *YonaStdGpuRawPinnedBackend(int64_t Handle);
int64_t YonaStdGpuRawMapFloatPinnedScale(double Scale, int64_t Handle);
int64_t YonaStdGpuRawMapFloatPinnedMul2(int64_t Handle);
int64_t YonaStdGpuRawMapFloatPinnedOp(int64_t *Op, int64_t Handle);

int64_t YonaStdGpuMapGpu(int64_t Op, int64_t Buffer) {
  int64_t *Adt = (int64_t *)(intptr_t)Op;
  int64_t Tag = Adt[0];
  if (Tag == 2)
    return YonaStdGpuMapSquare(Buffer);
  int64_t Arg = Adt[3];
  if (Tag == 0)
    return YonaStdGpuMapAdd(Arg, Buffer);
  return YonaStdGpuMapMul(Arg, Buffer);
}

int64_t YonaStdGpuReduceGpu(int64_t Op, int64_t Buffer) {
  (void)Op;
  return YonaStdGpuReduceSum(Buffer);
}

double *YonaStdGpuMapFloatGpu(int64_t Op, double *Arr) {
  return YonaStdGpuRawMapFloatOp((int64_t *)(intptr_t)Op, Arr);
}

double YonaStdGpuReduceFloatGpu(int64_t Op, double *Arr) {
  (void)Op;
  return YonaStdGpuRawReduceFloatGpu(Arr);
}

int64_t YonaStdGpuMapReduceGraphGpu(int64_t Stages, int64_t Buffer) {
  int64_t *Seq = (int64_t *)(intptr_t)Stages;
  int64_t N = YonaRuntimeSequenceLength(Seq);
  if (N < 0)
    N = 0;
  int64_t *Enc = YonaRuntimeIntArrayAllocate(N * 2);
  for (int64_t I = 0; I < N; I++) {
    void *Op = (void *)(intptr_t)YonaRuntimeSequenceGet(Seq, I);
    int64_t Tag = YonaRuntimeAdtGetTag(Op);
    Enc[1 + I * 2] = Tag;
    Enc[1 + I * 2 + 1] = (Tag == 2) ? 0 : YonaRuntimeAdtGetField(Op, 0);
  }
  int64_t Sum = YonaStdGpuRawMapReduceGraph(Enc, gpuBufferValues(Buffer));
  YonaRuntimeRelease(Enc);
  return Sum;
}

int64_t YonaStdGpuAllocPinnedFloats(int64_t N) {
  int64_t H = YonaStdGpuRawAllocPinnedFloats(N);
  int64_t *Adt =
      (int64_t *)YonaRuntimeAllocate(YONA_RC_TYPE_ADT, 4 * sizeof(int64_t));
  Adt[0] = 0;
  Adt[1] = 1;
  Adt[2] = 0;
  Adt[3] = H;
  return (int64_t)(intptr_t)Adt;
}

static int64_t gpuPinnedHandle(int64_t Pf) {
  int64_t *Adt = (int64_t *)(intptr_t)Pf;
  return Adt[3];
}

int64_t YonaStdGpuClosePinnedFloats(int64_t Pf) {
  return YonaStdGpuRawClosePinnedFloats(gpuPinnedHandle(Pf));
}

int64_t YonaStdGpuPinnedLength(int64_t Pf) {
  return YonaStdGpuRawPinnedLength(gpuPinnedHandle(Pf));
}

double YonaStdGpuPinnedGet(int64_t Pf, int64_t I) {
  return YonaStdGpuRawPinnedGet(gpuPinnedHandle(Pf), I);
}

int64_t YonaStdGpuPinnedSet(int64_t Pf, int64_t I, double V) {
  return YonaStdGpuRawPinnedSet(gpuPinnedHandle(Pf), I, V);
}

double *YonaStdGpuPinnedToFloatArray(int64_t Pf) {
  return YonaStdGpuRawPinnedToFloatArray(gpuPinnedHandle(Pf));
}

int64_t YonaStdGpuCopyFloatArrayToPinned(double *Arr, int64_t Pf) {
  return YonaStdGpuRawCopyFloatArrayToPinned(Arr, gpuPinnedHandle(Pf));
}

const char *YonaStdGpuPinnedBackend(int64_t Pf) {
  return YonaStdGpuRawPinnedBackend(gpuPinnedHandle(Pf));
}

int64_t YonaStdGpuMapFloatPinnedGpu(int64_t Op, int64_t Pf) {
  int64_t *Adt = (int64_t *)(intptr_t)Op;
  int64_t H = gpuPinnedHandle(Pf);
  if (!Adt)
    return -1;
  if (Adt[0] == 0) {
    int64_t Bits = Adt[3];
    double Scale;
    memcpy(&Scale, &Bits, sizeof(double));
    return YonaStdGpuRawMapFloatPinnedScale(Scale, H);
  }
  return YonaStdGpuRawMapFloatPinnedMul2(H);
}

extern int YonaRuntimeGpuVulkanContextInitialize(void);
extern int YonaRuntimeGpuVulkanFloat64BufferScaleInPlace(double *Elements,
                                                         uint32_t Count,
                                                         double Scale);
extern int YonaRuntimeGpuVulkanFloat64BufferReduceSum(const double *Elements,
                                                      uint32_t Count,
                                                      double *OutSum);
extern int YonaRuntimeGpuVulkanAllocatePinnedFloats(int64_t Count,
                                                    double **OutHost,
                                                    void **OutOpaque);
extern void YonaRuntimeGpuVulkanFreePinnedFloats(void *Opaque);
#if YONA_GPU_VULKAN_ENABLED
extern int YonaRuntimeGpuVulkanTryMapReduceGraphInt64(int64_t *Stages,
                                                      int64_t *Arr,
                                                      int64_t *OutSum);
#endif

typedef struct YonaGpuPinnedFloats {
  double *Host;
  int64_t Count;
  int OwnsMalloc; /* 1 = free(host); 0 when Vulkan-mapped (vk opaque owns) */
  void *VkOpaque; /* from YonaRuntimeGpuVulkanAllocatePinnedFloats, or NULL */
} YonaGpuPinnedFloats;

static double *scaleGpuFloatMap(double Scale, double *Arr) {
  int64_t Len = YonaRuntimeFloatArrayLength(Arr);
  double *Out = YonaRuntimeFloatArrayAllocate(Len);
  if (!Out)
    return Arr;
  memcpy(Out, Arr, (size_t)Len * sizeof(double));
  if (Len > 0 && Len <= (int64_t)0xffffffffu) {
    if (YonaRuntimeGpuVulkanContextInitialize() == 0) {
      if (YonaRuntimeGpuVulkanFloat64BufferScaleInPlace(Out, (uint32_t)Len,
                                                        Scale) == 0)
        return Out;
    }
  }
  for (int64_t I = 0; I < Len; I++)
    Out[I] *= Scale;
  return Out;
}

double *YonaStdGpuRawMapFloatScale(double Scale, double *Arr) {
  return scaleGpuFloatMap(Scale, Arr);
}

double *YonaStdGpuRawMapFloatMul2(double *Arr) {
  return scaleGpuFloatMap(2.0, Arr);
}

double *YonaStdGpuRawMapFloatOp(int64_t *Op, double *Arr) {
  if (!Op || !Arr)
    return Arr;
  int64_t Tag = Op[0];
  if (Tag == 0) {
    int64_t Bits = Op[3];
    double Scale;
    memcpy(&Scale, &Bits, sizeof(double));
    return scaleGpuFloatMap(Scale, Arr);
  }
  return scaleGpuFloatMap(2.0, Arr);
}

double YonaStdGpuRawReduceFloatGpu(double *Arr) {
  int64_t Len = YonaRuntimeFloatArrayLength(Arr);
  if (Len > 0 && Len <= (int64_t)0xffffffffu) {
    if (YonaRuntimeGpuVulkanContextInitialize() == 0) {
      double GpuSum = 0.0;
      if (YonaRuntimeGpuVulkanFloat64BufferReduceSum(Arr, (uint32_t)Len,
                                                     &GpuSum) == 0)
        return GpuSum;
    }
  }
  double Sum = 0.0;
  for (int64_t I = 0; I < Len; I++)
    Sum += Arr[I];
  return Sum;
}

static int64_t mapReduceGraphOnCpu(int64_t *Stages, int64_t *Arr) {
  int64_t Nflat = Stages[0];
  if (Nflat < 0 || (Nflat % 2) != 0)
    return YonaStdGpuRawReduceSum(Arr);
  int64_t *Cur = copyGpuIntArray(Arr);
  for (int64_t I = 0; I + 1 < Nflat; I += 2) {
    int64_t Op = Stages[1 + I];
    int64_t Arg = Stages[1 + I + 1];
    int64_t *Next = Cur;
    if (Op == 0)
      Next = YonaStdGpuRawMapAdd(Arg, Cur);
    else if (Op == 1)
      Next = YonaStdGpuRawMapMul(Arg, Cur);
    else if (Op == 2)
      Next = YonaStdGpuRawMapSquare(Cur);
    if (Next != Cur) {
      YonaRuntimeRelease(Cur);
      Cur = Next;
    }
  }
  int64_t Sum = YonaStdGpuRawReduceSum(Cur);
  YonaRuntimeRelease(Cur);
  return Sum;
}

int64_t YonaStdGpuRawMapReduceGraph(int64_t *Stages, int64_t *Arr) {
#if YONA_GPU_VULKAN_ENABLED
  int64_t GpuSum = 0;
  if (YonaRuntimeGpuVulkanTryMapReduceGraphInt64(Stages, Arr, &GpuSum))
    return GpuSum;
#endif
  return mapReduceGraphOnCpu(Stages, Arr);
}

int64_t YonaStdGpuRawAllocPinnedFloats(int64_t N) {
  if (N < 0)
    N = 0;
  YonaGpuPinnedFloats *P = (YonaGpuPinnedFloats *)calloc(1, sizeof(*P));
  if (!P)
    return 0;
  P->Count = N;
  P->OwnsMalloc = 1;
  P->VkOpaque = NULL;
  P->Host = NULL;

  const char *ForceMalloc = getenv("YONA_GPU_PINNED_HOST_MALLOC");
  const int SkipVk =
      (ForceMalloc && ForceMalloc[0] == '1' && ForceMalloc[1] == 0);

  if (!SkipVk) {
    double *Mapped = NULL;
    void *Opaque = NULL;
    if (YonaRuntimeGpuVulkanAllocatePinnedFloats(N, &Mapped, &Opaque) == 0 &&
        Opaque) {
      P->Host = Mapped;
      P->VkOpaque = Opaque;
      P->OwnsMalloc = 0;
      return (int64_t)(intptr_t)P;
    }
  }

  if (N > 0) {
    P->Host = (double *)calloc((size_t)N, sizeof(double));
    if (!P->Host) {
      free(P);
      return 0;
    }
  }
  return (int64_t)(intptr_t)P;
}

int64_t YonaStdGpuRawClosePinnedFloats(int64_t Handle) {
  YonaGpuPinnedFloats *P = (YonaGpuPinnedFloats *)(intptr_t)Handle;
  if (!P)
    return 0;
  if (P->VkOpaque) {
    YonaRuntimeGpuVulkanFreePinnedFloats(P->VkOpaque);
    P->VkOpaque = NULL;
    P->Host = NULL;
  } else if (P->OwnsMalloc && P->Host) {
    free(P->Host);
  }
  free(P);
  return 0;
}

int64_t YonaStdGpuRawPinnedLength(int64_t Handle) {
  YonaGpuPinnedFloats *P = (YonaGpuPinnedFloats *)(intptr_t)Handle;
  return P ? P->Count : 0;
}

double YonaStdGpuRawPinnedGet(int64_t Handle, int64_t I) {
  YonaGpuPinnedFloats *P = (YonaGpuPinnedFloats *)(intptr_t)Handle;
  if (!P || !P->Host || I < 0 || I >= P->Count)
    return 0.0;
  return P->Host[I];
}

int64_t YonaStdGpuRawPinnedSet(int64_t Handle, int64_t I, double V) {
  YonaGpuPinnedFloats *P = (YonaGpuPinnedFloats *)(intptr_t)Handle;
  if (!P || !P->Host || I < 0 || I >= P->Count)
    return -1;
  P->Host[I] = V;
  return 0;
}

double *YonaStdGpuRawPinnedToFloatArray(int64_t Handle) {
  YonaGpuPinnedFloats *P = (YonaGpuPinnedFloats *)(intptr_t)Handle;
  if (!P)
    return YonaRuntimeFloatArrayAllocate(0);
  double *Out = YonaRuntimeFloatArrayAllocate(P->Count);
  if (!Out)
    return NULL;
  if (P->Count > 0 && P->Host)
    memcpy(Out, P->Host, (size_t)P->Count * sizeof(double));
  return Out;
}

int64_t YonaStdGpuRawCopyFloatArrayToPinned(double *Arr, int64_t Handle) {
  YonaGpuPinnedFloats *P = (YonaGpuPinnedFloats *)(intptr_t)Handle;
  if (!P || !Arr)
    return -1;
  int64_t Len = YonaRuntimeFloatArrayLength(Arr);
  if (Len != P->Count || !P->Host)
    return -2;
  memcpy(P->Host, Arr, (size_t)Len * sizeof(double));
  return 0;
}

const char *YonaStdGpuRawPinnedBackend(int64_t Handle) {
  YonaGpuPinnedFloats *P = (YonaGpuPinnedFloats *)(intptr_t)Handle;
  if (!P)
    return makeGpuString("invalid");
  if (P->VkOpaque)
    return makeGpuString("vulkan-mapped");
  return makeGpuString("host-malloc");
}

int64_t YonaStdGpuRawMapFloatPinnedScale(double Scale, int64_t Handle) {
  YonaGpuPinnedFloats *P = (YonaGpuPinnedFloats *)(intptr_t)Handle;
  if (!P || !P->Host)
    return -1;
  int64_t Len = P->Count;
  if (Len <= 0)
    return 0;
  if (Len <= (int64_t)0xffffffffu) {
    if (YonaRuntimeGpuVulkanContextInitialize() == 0) {
      if (YonaRuntimeGpuVulkanFloat64BufferScaleInPlace(P->Host, (uint32_t)Len,
                                                        Scale) == 0)
        return 0;
    }
  }
  for (int64_t I = 0; I < Len; I++)
    P->Host[I] *= Scale;
  return 0;
}

int64_t YonaStdGpuRawMapFloatPinnedMul2(int64_t Handle) {
  return YonaStdGpuRawMapFloatPinnedScale(2.0, Handle);
}

int64_t YonaStdGpuRawMapFloatPinnedOp(int64_t *Op, int64_t Handle) {
  if (!Op)
    return -1;
  int64_t Tag = Op[0];
  if (Tag == 0) {
    int64_t Bits = Op[3];
    double Scale;
    memcpy(&Scale, &Bits, sizeof(double));
    return YonaStdGpuRawMapFloatPinnedScale(Scale, Handle);
  }
  return YonaStdGpuRawMapFloatPinnedMul2(Handle);
}
