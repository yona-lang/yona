#include <cstdint>
#include <doctest/doctest.h>
#include <string>
#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include "yona/runtime/gpu_vulkan_device.h"

extern "C" int64_t yona_Std_GPU_raw__hasGpu(int64_t unused);

TEST_CASE("gpu vulkan device: shutdown is idempotent") {
  yona_gpu_vulkan_device_shutdown();
  yona_gpu_vulkan_device_shutdown();
  CHECK(yona_gpu_vulkan_device_ready() == 0);
}

TEST_CASE("gpu vulkan device: try_init when built without Vulkan headers") {
#if !defined(YONA_COMPILE_GPU_VULKAN) || (YONA_COMPILE_GPU_VULKAN + 0) != 1
  CHECK(yona_gpu_vulkan_device_try_init() < 0);
  CHECK(yona_gpu_vulkan_device_ready() == 0);
#else
  yona_gpu_vulkan_device_shutdown();
  int rc = yona_gpu_vulkan_device_try_init();
  (void)rc;
  yona_gpu_vulkan_device_shutdown();
  CHECK(yona_gpu_vulkan_device_ready() == 0);
#endif
}

TEST_CASE("gpu vulkan device: open_loader is closable") {
  yona_gpu_vulkan_device_shutdown();
  void *lib = yona_gpu_vulkan_open_loader();
  if (lib) {
#if defined(_WIN32)
    CHECK(FreeLibrary((HMODULE)lib) != 0);
#else
    CHECK(dlclose(lib) == 0);
#endif
  }
}

TEST_CASE("gpu vulkan device: try_init device-ready or loader-only") {
#if defined(YONA_COMPILE_GPU_VULKAN) && (YONA_COMPILE_GPU_VULKAN + 0) == 1
  yona_gpu_vulkan_device_shutdown();
  int rc = yona_gpu_vulkan_device_try_init();
#if defined(__APPLE__)
  /* Portability enumeration + MoltenVK ICD: a visible Darwin loader must
   * produce a compute-capable device. CI without MoltenVK skips this. */
  if (yona_gpu_vulkan_loader_available()) {
    REQUIRE_MESSAGE(rc == 0, std::string(yona_gpu_vulkan_device_last_note()));
  }
#endif
  if (rc == 0) {
    CHECK(yona_gpu_vulkan_device_ready() == 1);
    CHECK(std::string(yona_gpu_vulkan_device_status_name()) == "vulkan-device");
    /* MoltenVK/Metal usually lacks shaderInt64; desktop Vulkan often has it. */
        if (yona_gpu_vulkan_device_shader_int64() == 0) {
            std::string note = yona_gpu_vulkan_device_last_note();
            CHECK(note.find("shaderInt64") != std::string::npos);
            CHECK(note.find("i32") != std::string::npos);
            CHECK(yona_Std_GPU_raw__hasGpu(0) == 1);
        } else {
            CHECK(yona_Std_GPU_raw__hasGpu(0) == 1);
        }
  } else {
    CHECK(yona_gpu_vulkan_device_ready() == 0);
  }
  yona_gpu_vulkan_device_shutdown();
  CHECK(yona_gpu_vulkan_device_ready() == 0);
#else
  CHECK(yona_gpu_vulkan_device_try_init() < 0);
  CHECK(yona_gpu_vulkan_device_ready() == 0);
#endif
}

TEST_CASE("gpu vulkan device: timeline_semaphore query after shutdown") {
  yona_gpu_vulkan_device_shutdown();
  CHECK(yona_gpu_vulkan_device_timeline_semaphore() == 0);
#if defined(YONA_COMPILE_GPU_VULKAN) && (YONA_COMPILE_GPU_VULKAN + 0) == 1
  /* After init, flag is driver-dependent (0 or 1); only check it does not crash. */
  (void)yona_gpu_vulkan_device_try_init();
  (void)yona_gpu_vulkan_device_timeline_semaphore();
  yona_gpu_vulkan_device_shutdown();
  CHECK(yona_gpu_vulkan_device_timeline_semaphore() == 0);
#endif
}
