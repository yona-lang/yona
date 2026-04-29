#include <doctest/doctest.h>
#include "yona/runtime/gpu_vulkan_device.h"

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
