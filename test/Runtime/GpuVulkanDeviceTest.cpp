#include <doctest/doctest.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include "yona/Runtime/Gpu/VulkanDevice.h"

extern "C" int64_t YonaStdGpuRawHasGpu(int64_t unused);

TEST_CASE("gpu vulkan device: shutdown is idempotent") {
  YonaRuntimeGpuVulkanDeviceShutdown();
  YonaRuntimeGpuVulkanDeviceShutdown();
  CHECK(YonaRuntimeGpuVulkanDeviceIsReady() == 0);
}

TEST_CASE("gpu vulkan device: initialization and shutdown are synchronized") {
  std::atomic<bool> start{false};
  std::thread probe([&] {
    while (!start.load(std::memory_order_acquire)) {
    }
    for (int i = 0; i < 8; ++i) {
      (void)YonaRuntimeGpuVulkanDeviceTryInitialize();
      (void)YonaRuntimeGpuVulkanDeviceIsReady();
      (void)std::string(YonaRuntimeGpuVulkanDeviceLastNote());
    }
  });
  std::thread reset([&] {
    start.store(true, std::memory_order_release);
    for (int i = 0; i < 8; ++i) {
      YonaRuntimeGpuVulkanDeviceShutdown();
    }
  });

  probe.join();
  reset.join();
  YonaRuntimeGpuVulkanDeviceShutdown();
  CHECK(YonaRuntimeGpuVulkanDeviceIsReady() == 0);
}

TEST_CASE("gpu vulkan device: try_init when built without Vulkan headers") {
#if !YONA_GPU_VULKAN_ENABLED
  CHECK(YonaRuntimeGpuVulkanDeviceTryInitialize() < 0);
  CHECK(YonaRuntimeGpuVulkanDeviceIsReady() == 0);
#else
  YonaRuntimeGpuVulkanDeviceShutdown();
  int rc = YonaRuntimeGpuVulkanDeviceTryInitialize();
  (void)rc;
  YonaRuntimeGpuVulkanDeviceShutdown();
  CHECK(YonaRuntimeGpuVulkanDeviceIsReady() == 0);
#endif
}

TEST_CASE("gpu vulkan device: open_loader is closable") {
  YonaRuntimeGpuVulkanDeviceShutdown();
  void *lib = YonaRuntimeGpuVulkanOpenLoader();
  if (lib) {
#if defined(_WIN32)
    CHECK(FreeLibrary((HMODULE)lib) != 0);
#else
    CHECK(dlclose(lib) == 0);
#endif
  }
}

TEST_CASE("gpu vulkan device: try_init device-ready or loader-only") {
#if YONA_GPU_VULKAN_ENABLED
  YonaRuntimeGpuVulkanDeviceShutdown();
  int rc = YonaRuntimeGpuVulkanDeviceTryInitialize();
#if defined(__APPLE__)
  /* Portability enumeration + MoltenVK ICD: a visible Darwin loader must
   * produce a compute-capable device. CI without MoltenVK skips this. */
  if (YonaRuntimeGpuVulkanLoaderAvailable()) {
    REQUIRE_MESSAGE(rc == 0, std::string(YonaRuntimeGpuVulkanDeviceLastNote()));
  }
#endif
  if (rc == 0) {
    CHECK(YonaRuntimeGpuVulkanDeviceIsReady() == 1);
    CHECK(std::string(YonaRuntimeGpuVulkanDeviceStatusName()) ==
          "vulkan-device");
    /* MoltenVK/Metal usually lacks shaderInt64; desktop Vulkan often has it. */
    if (YonaRuntimeGpuVulkanDeviceHasShaderInt64() == 0) {
      std::string note = YonaRuntimeGpuVulkanDeviceLastNote();
      CHECK(note.find("shaderInt64") != std::string::npos);
      CHECK(note.find("i32") != std::string::npos);
      CHECK(YonaStdGpuRawHasGpu(0) == 1);
    } else {
      CHECK(YonaStdGpuRawHasGpu(0) == 1);
    }
  } else {
    CHECK(YonaRuntimeGpuVulkanDeviceIsReady() == 0);
  }
  YonaRuntimeGpuVulkanDeviceShutdown();
  CHECK(YonaRuntimeGpuVulkanDeviceIsReady() == 0);
#else
  CHECK(YonaRuntimeGpuVulkanDeviceTryInitialize() < 0);
  CHECK(YonaRuntimeGpuVulkanDeviceIsReady() == 0);
#endif
}

TEST_CASE("gpu vulkan device: timeline_semaphore query after shutdown") {
  YonaRuntimeGpuVulkanDeviceShutdown();
  CHECK(YonaRuntimeGpuVulkanDeviceHasTimelineSemaphore() == 0);
#if YONA_GPU_VULKAN_ENABLED
  /* After init, flag is driver-dependent (0 or 1); only check it does not
   * crash. */
  (void)YonaRuntimeGpuVulkanDeviceTryInitialize();
  (void)YonaRuntimeGpuVulkanDeviceHasTimelineSemaphore();
  YonaRuntimeGpuVulkanDeviceShutdown();
  CHECK(YonaRuntimeGpuVulkanDeviceHasTimelineSemaphore() == 0);
#endif
}
