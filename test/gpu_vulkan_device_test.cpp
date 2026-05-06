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
