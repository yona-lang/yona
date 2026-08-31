#include "yona/Runtime/Concurrency/Async.h"
#include "yona/Runtime/Gpu/Api.h"

#include <doctest/doctest.h>

#include <cstdlib>
#include <string>

TEST_CASE("GPU: available is a boolean (0 without Vulkan probe, 1 when compute "
          "queue exists)") {
  const int64_t a = YonaStdGpuAvailable(0);
  CHECK((a == 0 || a == 1));
}

TEST_CASE("GPU: physicalDeviceCount is non-negative") {
  CHECK(YonaStdGpuPhysicalDeviceCount(0) >= 0);
}

TEST_CASE("GPU: disable override forces the CPU discovery fallback") {
  const char *Previous = std::getenv("YONA_GPU_DISABLE_VULKAN");
  const bool HadPrevious = Previous != nullptr;
  const std::string Saved = HadPrevious ? Previous : "";
#if defined(_WIN32)
  (void)_putenv_s("YONA_GPU_DISABLE_VULKAN", "1");
#else
  (void)setenv("YONA_GPU_DISABLE_VULKAN", "1", 1);
#endif

  CHECK(YonaStdGpuAvailable(0) == 0);
  CHECK(YonaStdGpuPhysicalDeviceCount(0) == 0);

#if defined(_WIN32)
  (void)_putenv_s("YONA_GPU_DISABLE_VULKAN", HadPrevious ? Saved.c_str() : "");
#else
  if (HadPrevious)
    (void)setenv("YONA_GPU_DISABLE_VULKAN", Saved.c_str(), 1);
  else
    (void)unsetenv("YONA_GPU_DISABLE_VULKAN");
#endif
}

TEST_CASE("GPU: vulkan ctx init when discovery says compute is available") {
  if (YonaStdGpuAvailable(0) != 1) {
    return;
  }
  CHECK(YonaRuntimeGpuVulkanContextInitialize() == 0);
  YonaRuntimeGpuVulkanContextShutdown();
}

/* Set YONA_GPU_TEST_DISPATCH=1 to exercise submit + fence (driver-sensitive;
 * default off). */
TEST_CASE("GPU: optional nop dispatch when YONA_GPU_TEST_DISPATCH is set") {
  const char *flag = std::getenv("YONA_GPU_TEST_DISPATCH");
  if (flag == nullptr || flag[0] == '\0') {
    return;
  }
  if (YonaStdGpuAvailable(0) != 1) {
    return;
  }
  CHECK(YonaRuntimeGpuVulkanContextInitialize() == 0);
  CHECK(YonaRuntimeGpuVulkanDispatchNopOnce() == 0);
  YonaRuntimeGpuVulkanContextShutdown();
}

TEST_CASE("GPU: float mul2 uses f64 or f32 when compute is available") {
  if (YonaStdGpuAvailable(0) != 1) {
    return;
  }
  CHECK(YonaRuntimeGpuVulkanContextInitialize() == 0);
  double data[4] = {1.0, 2.0, 3.0, 4.0};
  const int r = YonaRuntimeGpuVulkanFloat64BufferMultiply2InPlace(data, 4);
  REQUIRE(r == 0);
  CHECK(data[0] == doctest::Approx(2.0));
  CHECK(data[1] == doctest::Approx(4.0));
  CHECK(data[2] == doctest::Approx(6.0));
  CHECK(data[3] == doctest::Approx(8.0));
  YonaRuntimeGpuVulkanContextShutdown();
}

TEST_CASE("GPU: float reduce sum uses f64 or f32 when compute is available") {
  if (YonaStdGpuAvailable(0) != 1) {
    return;
  }
  CHECK(YonaRuntimeGpuVulkanContextInitialize() == 0);
  double data[4] = {1.0, 2.0, 3.0, 4.0};
  double sum = 0.0;
  const int r = YonaRuntimeGpuVulkanFloat64BufferReduceSum(data, 4, &sum);
  REQUIRE(r == 0);
  CHECK(sum == doctest::Approx(10.0));
  YonaRuntimeGpuVulkanContextShutdown();
}

/* Set YONA_GPU_TEST_F64_MUL2=1 to run GPU double×2 on a stack buffer (f64 or
 * f32 fallback). */
TEST_CASE("GPU: optional f64 mul2 when YONA_GPU_TEST_F64_MUL2 is set") {
  const char *flag = std::getenv("YONA_GPU_TEST_F64_MUL2");
  if (flag == nullptr || flag[0] == '\0') {
    return;
  }
  if (YonaStdGpuAvailable(0) != 1) {
    return;
  }
  CHECK(YonaRuntimeGpuVulkanContextInitialize() == 0);
  double data[4] = {1.0, 2.0, 3.0, 4.0};
  const int r = YonaRuntimeGpuVulkanFloat64BufferMultiply2InPlace(data, 4);
  if (r == -20) {
    YonaRuntimeGpuVulkanContextShutdown();
    return;
  }
  REQUIRE(r == 0);
  CHECK(data[0] == doctest::Approx(2.0));
  CHECK(data[1] == doctest::Approx(4.0));
  CHECK(data[2] == doctest::Approx(6.0));
  CHECK(data[3] == doctest::Approx(8.0));
  YonaRuntimeGpuVulkanContextShutdown();
}

/* Set YONA_GPU_TEST_F64_MUL2_ASYNC=1 to await GPU mul2 via promise (dedicated
 * fence thread). */
TEST_CASE(
    "GPU: optional async f64 mul2 when YONA_GPU_TEST_F64_MUL2_ASYNC is set") {
  const char *flag = std::getenv("YONA_GPU_TEST_F64_MUL2_ASYNC");
  if (flag == nullptr || flag[0] == '\0') {
    return;
  }
  if (YonaStdGpuAvailable(0) != 1) {
    return;
  }
  CHECK(YonaRuntimeGpuVulkanContextInitialize() == 0);
  double data[4] = {1.0, 2.0, 3.0, 4.0};
  YonaTaskRef pr = YonaRuntimeGpuVulkanFloat64BufferMultiply2Async(
      data, 4, &YonaRuntimeUnmanagedTypeDescriptor, nullptr);
  REQUIRE(pr != nullptr);
  const int64_t r = YonaRuntimeTaskAwait(pr);
  if (r == -20) {
    YonaRuntimeGpuVulkanContextShutdown();
    return;
  }
  CHECK(r == 0);
  CHECK(data[0] == doctest::Approx(2.0));
  CHECK(data[1] == doctest::Approx(4.0));
  CHECK(data[2] == doctest::Approx(6.0));
  CHECK(data[3] == doctest::Approx(8.0));
  YonaRuntimeGpuVulkanContextShutdown();
}

TEST_CASE("GPU: pinned floats alloc prefers vulkan-mapped or host-malloc") {
  double *host = nullptr;
  void *opaque = nullptr;
  const int r = YonaRuntimeGpuVulkanAllocatePinnedFloats(4, &host, &opaque);
  if (r != 0) {
    /* CPU-only build or no device: the runtime returns -1. */
    CHECK(r == -1);
    CHECK(host == nullptr);
    CHECK(opaque == nullptr);
    return;
  }
  REQUIRE(host != nullptr);
  REQUIRE(opaque != nullptr);
  host[0] = 1.5;
  host[1] = 2.5;
  CHECK(host[0] == doctest::Approx(1.5));
  CHECK(host[1] == doctest::Approx(2.5));
  YonaRuntimeGpuVulkanFreePinnedFloats(opaque);
  YonaRuntimeGpuVulkanContextShutdown();
}

extern "C" {
int64_t YonaStdGpuRawAllocPinnedFloats(int64_t n);
int64_t YonaStdGpuRawClosePinnedFloats(int64_t handle);
const char *YonaStdGpuRawPinnedBackend(int64_t handle);
int64_t YonaStdGpuRawPinnedSet(int64_t handle, int64_t i, double v);
double YonaStdGpuRawPinnedGet(int64_t handle, int64_t i);
int64_t YonaStdGpuRawMapFloatPinnedScale(double scale, int64_t handle);
}

TEST_CASE("GPU: Std\\Gpu pinned malloc fallback and in-place scale") {
#if defined(_WIN32)
  (void)_putenv_s("YONA_GPU_PINNED_HOST_MALLOC", "1");
#else
  setenv("YONA_GPU_PINNED_HOST_MALLOC", "1", 1);
#endif
  int64_t h = YonaStdGpuRawAllocPinnedFloats(2);
  REQUIRE(h != 0);
  const char *backend = YonaStdGpuRawPinnedBackend(h);
  REQUIRE(backend != nullptr);
  CHECK(std::string(backend) == "host-malloc");
  CHECK(YonaStdGpuRawPinnedSet(h, 0, 3.0) == 0);
  CHECK(YonaStdGpuRawPinnedSet(h, 1, 4.0) == 0);
  CHECK(YonaStdGpuRawMapFloatPinnedScale(2.0, h) == 0);
  CHECK(YonaStdGpuRawPinnedGet(h, 0) == doctest::Approx(6.0));
  CHECK(YonaStdGpuRawPinnedGet(h, 1) == doctest::Approx(8.0));
  CHECK(YonaStdGpuRawClosePinnedFloats(h) == 0);
#if defined(_WIN32)
  (void)_putenv_s("YONA_GPU_PINNED_HOST_MALLOC", "");
#else
  unsetenv("YONA_GPU_PINNED_HOST_MALLOC");
#endif
}

/* Set YONA_GPU_TEST_F64_GROUP_CANCEL=1: grouped mul2 promise completes -887 on
 * cancel (may finish before the GPU drain; host buffer write is discarded when
 * cancelled). */
TEST_CASE("GPU: optional grouped f64 mul2 cancel") {
  const char *flag = std::getenv("YONA_GPU_TEST_F64_GROUP_CANCEL");
  if (flag == nullptr || flag[0] == '\0') {
    return;
  }
  if (YonaStdGpuAvailable(0) != 1) {
    return;
  }
  CHECK(YonaRuntimeGpuVulkanContextInitialize() == 0);
  double data[4] = {1.0, 2.0, 3.0, 4.0};
  YonaTaskGroupRef g = YonaRuntimeTaskGroupBegin();
  REQUIRE(g != nullptr);
  YonaTaskRef pr = YonaRuntimeGpuVulkanFloat64BufferMultiply2Async(
      data, 4, &YonaRuntimeUnmanagedTypeDescriptor, g);
  REQUIRE(pr != nullptr);
  YonaRuntimeTaskGroupCancel(g);
  const int64_t r = YonaRuntimeTaskAwaitKeep(pr);
  if (r == -20) {
    YonaRuntimeTaskGroupEnd(g);
    YonaRuntimeGpuVulkanContextShutdown();
    return;
  }
  CHECK(r == -887);
  YonaRuntimeTaskGroupEnd(g);
  YonaRuntimeGpuVulkanContextShutdown();
}
