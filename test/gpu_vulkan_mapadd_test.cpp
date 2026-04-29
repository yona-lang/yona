#include <doctest/doctest.h>

#include <cstdint>
#include <cstdlib>

#include "yona/runtime/gpu_vulkan_device.h"

extern "C" void yona_rt_rc_dec(void* ptr);

#if defined(YONA_COMPILE_GPU_VULKAN) && (YONA_COMPILE_GPU_VULKAN + 0) == 1

#ifndef _WIN32
#include <unistd.h>
#endif

static void mapadd_env_enable_small_arrays(void) {
#ifdef _WIN32
    (void)_putenv_s("YONA_GPU_VULKAN_MAPADD", "1");
    (void)_putenv_s("YONA_GPU_VULKAN_MAPADD_MIN_LEN", "1");
#else
    (void)setenv("YONA_GPU_VULKAN_MAPADD", "1", 1);
    (void)setenv("YONA_GPU_VULKAN_MAPADD_MIN_LEN", "1", 1);
#endif
}

static void mapadd_env_clear(void) {
#ifdef _WIN32
    (void)_putenv_s("YONA_GPU_VULKAN_MAPADD", "");
    (void)_putenv_s("YONA_GPU_VULKAN_MAPADD_MIN_LEN", "");
#else
    (void)unsetenv("YONA_GPU_VULKAN_MAPADD");
    (void)unsetenv("YONA_GPU_VULKAN_MAPADD_MIN_LEN");
#endif
}

static void mapmul_env_enable_small_arrays(void) {
#ifdef _WIN32
    (void)_putenv_s("YONA_GPU_VULKAN_MAPMUL", "1");
    (void)_putenv_s("YONA_GPU_VULKAN_MAPMUL_MIN_LEN", "1");
#else
    (void)setenv("YONA_GPU_VULKAN_MAPMUL", "1", 1);
    (void)setenv("YONA_GPU_VULKAN_MAPMUL_MIN_LEN", "1", 1);
#endif
}

static void mapmul_env_clear(void) {
#ifdef _WIN32
    (void)_putenv_s("YONA_GPU_VULKAN_MAPMUL", "");
    (void)_putenv_s("YONA_GPU_VULKAN_MAPMUL_MIN_LEN", "");
#else
    (void)unsetenv("YONA_GPU_VULKAN_MAPMUL");
    (void)unsetenv("YONA_GPU_VULKAN_MAPMUL_MIN_LEN");
#endif
}

static void reduce_env_enable_small_arrays(void) {
#ifdef _WIN32
    (void)_putenv_s("YONA_GPU_VULKAN_REDUCE", "1");
    (void)_putenv_s("YONA_GPU_VULKAN_REDUCE_MIN_LEN", "1");
#else
    (void)setenv("YONA_GPU_VULKAN_REDUCE", "1", 1);
    (void)setenv("YONA_GPU_VULKAN_REDUCE_MIN_LEN", "1", 1);
#endif
}

static void reduce_env_clear(void) {
#ifdef _WIN32
    (void)_putenv_s("YONA_GPU_VULKAN_REDUCE", "");
    (void)_putenv_s("YONA_GPU_VULKAN_REDUCE_MIN_LEN", "");
#else
    (void)unsetenv("YONA_GPU_VULKAN_REDUCE");
    (void)unsetenv("YONA_GPU_VULKAN_REDUCE_MIN_LEN");
#endif
}

TEST_CASE("gpu vulkan mapadd: optional gpu roundtrip") {
    yona_gpu_vulkan_device_shutdown();
    mapadd_env_enable_small_arrays();

    int64_t buf[1 + 8];
    buf[0] = 8;
    for (int i = 0; i < 8; i++)
        buf[1 + i] = (int64_t)i;

    int64_t* out = nullptr;
    int ok = yona_gpu_vulkan_try_map_add_int64(100, buf, &out);

    mapadd_env_clear();
    yona_gpu_vulkan_device_shutdown();

    if (!ok) {
        MESSAGE("Vulkan mapAdd skipped: ", yona_gpu_vulkan_device_last_note());
        return;
    }

    REQUIRE(out != nullptr);
    CHECK(out[0] == 8);
    for (int i = 0; i < 8; i++)
        CHECK(out[1 + i] == (int64_t)i + 100);

    yona_rt_rc_dec((void*)out);
}

TEST_CASE("gpu vulkan mapmul: optional gpu roundtrip") {
    yona_gpu_vulkan_device_shutdown();
    mapmul_env_enable_small_arrays();

    int64_t buf[1 + 8];
    buf[0] = 8;
    for (int i = 0; i < 8; i++)
        buf[1 + i] = (int64_t)(i + 1);

    int64_t* out = nullptr;
    int ok = yona_gpu_vulkan_try_map_mul_int64(10, buf, &out);

    mapmul_env_clear();
    yona_gpu_vulkan_device_shutdown();

    if (!ok) {
        MESSAGE("Vulkan mapMul skipped: ", yona_gpu_vulkan_device_last_note());
        return;
    }

    REQUIRE(out != nullptr);
    CHECK(out[0] == 8);
    for (int i = 0; i < 8; i++)
        CHECK(out[1 + i] == ((int64_t)(i + 1)) * 10);

    yona_rt_rc_dec((void*)out);
}

TEST_CASE("gpu vulkan reduce: optional gpu roundtrip") {
    yona_gpu_vulkan_device_shutdown();
    reduce_env_enable_small_arrays();

    int64_t buf[1 + 8];
    buf[0] = 8;
    for (int i = 0; i < 8; i++)
        buf[1 + i] = (int64_t)(i + 1);

    int64_t sum = 0;
    int ok = yona_gpu_vulkan_try_reduce_sum_int64(buf, &sum);

    reduce_env_clear();
    yona_gpu_vulkan_device_shutdown();

    if (!ok) {
        MESSAGE("Vulkan reduceSum skipped: ", yona_gpu_vulkan_device_last_note());
        return;
    }

    CHECK(sum == 36);
}

#endif /* YONA_COMPILE_GPU_VULKAN */
