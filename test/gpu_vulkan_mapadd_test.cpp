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

static void filter_env_enable_small_arrays(void) {
#ifdef _WIN32
    (void)_putenv_s("YONA_GPU_VULKAN_FILTER", "1");
    (void)_putenv_s("YONA_GPU_VULKAN_FILTER_MIN_LEN", "1");
#else
    (void)setenv("YONA_GPU_VULKAN_FILTER", "1", 1);
    (void)setenv("YONA_GPU_VULKAN_FILTER_MIN_LEN", "1", 1);
#endif
}

static void filter_env_clear(void) {
#ifdef _WIN32
    (void)_putenv_s("YONA_GPU_VULKAN_FILTER", "");
    (void)_putenv_s("YONA_GPU_VULKAN_FILTER_MIN_LEN", "");
    (void)_putenv_s("YONA_GPU_VULKAN_FILTER_CPU_PREFIX", "");
#else
    (void)unsetenv("YONA_GPU_VULKAN_FILTER");
    (void)unsetenv("YONA_GPU_VULKAN_FILTER_MIN_LEN");
    (void)unsetenv("YONA_GPU_VULKAN_FILTER_CPU_PREFIX");
#endif
}

static void filter_cpu_prefix_set(void) {
#ifdef _WIN32
    (void)_putenv_s("YONA_GPU_VULKAN_FILTER_CPU_PREFIX", "1");
#else
    (void)setenv("YONA_GPU_VULKAN_FILTER_CPU_PREFIX", "1", 1);
#endif
}

static void filter_cpu_prefix_clear(void) {
#ifdef _WIN32
    (void)_putenv_s("YONA_GPU_VULKAN_FILTER_CPU_PREFIX", "");
#else
    (void)unsetenv("YONA_GPU_VULKAN_FILTER_CPU_PREFIX");
#endif
}

/** Force host-mapped SSBOs (disables device-local + staging when unset). */
static void vulkan_host_ssbo_set(void) {
#ifdef _WIN32
    (void)_putenv_s("YONA_GPU_VULKAN_HOST_SSBO", "1");
#else
    (void)setenv("YONA_GPU_VULKAN_HOST_SSBO", "1", 1);
#endif
}

static void vulkan_host_ssbo_clear(void) {
#ifdef _WIN32
    (void)_putenv_s("YONA_GPU_VULKAN_HOST_SSBO", "");
#else
    (void)unsetenv("YONA_GPU_VULKAN_HOST_SSBO");
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

TEST_CASE("gpu vulkan reduce: host_ssbo matches default path when gpu runs") {
    yona_gpu_vulkan_device_shutdown();
    reduce_env_enable_small_arrays();

    int64_t buf[1 + 8];
    buf[0] = 8;
    for (int i = 0; i < 8; i++)
        buf[1 + i] = (int64_t)(i + 1);

    int64_t sum_default = 0;
    int ok_default = yona_gpu_vulkan_try_reduce_sum_int64(buf, &sum_default);
    reduce_env_clear();
    yona_gpu_vulkan_device_shutdown();

    if (!ok_default) {
        MESSAGE("Vulkan reduceSum skipped: ", yona_gpu_vulkan_device_last_note());
        return;
    }

    yona_gpu_vulkan_device_shutdown();
    vulkan_host_ssbo_set();
    reduce_env_enable_small_arrays();

    int64_t sum_host = 0;
    int ok_host = yona_gpu_vulkan_try_reduce_sum_int64(buf, &sum_host);

    reduce_env_clear();
    vulkan_host_ssbo_clear();
    yona_gpu_vulkan_device_shutdown();

    REQUIRE(ok_host);
    CHECK(sum_host == sum_default);
}

TEST_CASE("gpu vulkan mapadd: host_ssbo matches default path when gpu runs") {
    yona_gpu_vulkan_device_shutdown();
    mapadd_env_enable_small_arrays();

    int64_t buf[1 + 8];
    buf[0] = 8;
    for (int i = 0; i < 8; i++)
        buf[1 + i] = (int64_t)i;

    int64_t* out_default = nullptr;
    int ok_default = yona_gpu_vulkan_try_map_add_int64(100, buf, &out_default);

    mapadd_env_clear();
    yona_gpu_vulkan_device_shutdown();

    if (!ok_default) {
        MESSAGE("Vulkan mapAdd skipped: ", yona_gpu_vulkan_device_last_note());
        return;
    }

    yona_gpu_vulkan_device_shutdown();
    vulkan_host_ssbo_set();
    mapadd_env_enable_small_arrays();

    int64_t* out_host = nullptr;
    int ok_host = yona_gpu_vulkan_try_map_add_int64(100, buf, &out_host);

    mapadd_env_clear();
    vulkan_host_ssbo_clear();
    yona_gpu_vulkan_device_shutdown();

    REQUIRE(ok_host);
    REQUIRE(out_host != nullptr);
    CHECK(out_default[0] == out_host[0]);
    for (int i = 0; i < 8; i++)
        CHECK(out_default[1 + i] == out_host[1 + i]);

    yona_rt_rc_dec((void*)out_default);
    yona_rt_rc_dec((void*)out_host);
}

TEST_CASE("gpu vulkan filterGreaterThan: optional gpu roundtrip") {
    yona_gpu_vulkan_device_shutdown();
    filter_env_enable_small_arrays();

    int64_t buf[1 + 8];
    buf[0] = 8;
    for (int i = 0; i < 8; i++)
        buf[1 + i] = (int64_t)(i + 1);

    int64_t* out = nullptr;
    int ok = yona_gpu_vulkan_try_filter_greater_than_int64(4, buf, &out);

    filter_env_clear();
    yona_gpu_vulkan_device_shutdown();

    if (!ok) {
        MESSAGE("Vulkan filterGreaterThan skipped: ", yona_gpu_vulkan_device_last_note());
        return;
    }

    REQUIRE(out != nullptr);
    CHECK(out[0] == 4);
    CHECK(out[1] == 5);
    CHECK(out[2] == 6);
    CHECK(out[3] == 7);
    CHECK(out[4] == 8);

    yona_rt_rc_dec((void*)out);
}

TEST_CASE("gpu vulkan filterGreaterThan: gpu prefix matches legacy CPU prefix when gpu runs") {
    yona_gpu_vulkan_device_shutdown();
    filter_env_enable_small_arrays();

    int64_t buf[1 + 16];
    buf[0] = 16;
    for (int i = 0; i < 16; i++)
        buf[1 + i] = (int64_t)(i * 3 - 20);

    int64_t* out_gpu = nullptr;
    int ok_gpu = yona_gpu_vulkan_try_filter_greater_than_int64(-5, buf, &out_gpu);
    filter_env_clear();
    yona_gpu_vulkan_device_shutdown();

    if (!ok_gpu) {
        MESSAGE("Vulkan filterGreaterThan skipped: ", yona_gpu_vulkan_device_last_note());
        return;
    }

    yona_gpu_vulkan_device_shutdown();
    filter_env_enable_small_arrays();
    filter_cpu_prefix_set();

    int64_t* out_cpu = nullptr;
    int ok_cpu = yona_gpu_vulkan_try_filter_greater_than_int64(-5, buf, &out_cpu);

    filter_cpu_prefix_clear();
    filter_env_clear();
    yona_gpu_vulkan_device_shutdown();

    REQUIRE(ok_cpu);
    REQUIRE(out_cpu != nullptr);
    REQUIRE(out_gpu != nullptr);
    CHECK(out_gpu[0] == out_cpu[0]);
    for (int64_t i = 0; i < out_gpu[0]; i++)
        CHECK(out_gpu[1 + i] == out_cpu[1 + i]);

    yona_rt_rc_dec((void*)out_gpu);
    yona_rt_rc_dec((void*)out_cpu);
}

TEST_CASE("gpu vulkan filterGreaterThan: host_ssbo matches default when gpu runs") {
    yona_gpu_vulkan_device_shutdown();
    filter_env_enable_small_arrays();

    int64_t buf[1 + 8];
    buf[0] = 8;
    for (int i = 0; i < 8; i++)
        buf[1 + i] = (int64_t)(i + 1);

    int64_t* out_default = nullptr;
    int ok_default = yona_gpu_vulkan_try_filter_greater_than_int64(4, buf, &out_default);
    filter_env_clear();
    yona_gpu_vulkan_device_shutdown();

    if (!ok_default) {
        MESSAGE("Vulkan filterGreaterThan skipped: ", yona_gpu_vulkan_device_last_note());
        return;
    }

    yona_gpu_vulkan_device_shutdown();
    vulkan_host_ssbo_set();
    filter_env_enable_small_arrays();

    int64_t* out_host = nullptr;
    int ok_host = yona_gpu_vulkan_try_filter_greater_than_int64(4, buf, &out_host);

    filter_env_clear();
    vulkan_host_ssbo_clear();
    yona_gpu_vulkan_device_shutdown();

    REQUIRE(ok_host);
    REQUIRE(out_host != nullptr);
    CHECK(out_default[0] == out_host[0]);
    for (int64_t i = 0; i < out_default[0]; i++)
        CHECK(out_default[1 + i] == out_host[1 + i]);

    yona_rt_rc_dec((void*)out_default);
    yona_rt_rc_dec((void*)out_host);
}

TEST_CASE("gpu vulkan filterGreaterThan: empty and full passes when gpu runs") {
    yona_gpu_vulkan_device_shutdown();
    filter_env_enable_small_arrays();

    int64_t buf[1 + 8];
    buf[0] = 8;
    for (int i = 0; i < 8; i++)
        buf[1 + i] = (int64_t)(i + 1);

    int64_t* none = nullptr;
    int ok_none = yona_gpu_vulkan_try_filter_greater_than_int64(100, buf, &none);
    int64_t* all = nullptr;
    int ok_all = yona_gpu_vulkan_try_filter_greater_than_int64(0, buf, &all);

    filter_env_clear();
    yona_gpu_vulkan_device_shutdown();

    if (!ok_none || !ok_all) {
        MESSAGE("Vulkan filterGreaterThan edge cases skipped: ",
                yona_gpu_vulkan_device_last_note());
        return;
    }

    REQUIRE(none != nullptr);
    CHECK(none[0] == 0);
    REQUIRE(all != nullptr);
    CHECK(all[0] == 8);
    for (int i = 0; i < 8; i++)
        CHECK(all[1 + i] == (int64_t)(i + 1));

    yona_rt_rc_dec((void*)none);
    yona_rt_rc_dec((void*)all);
}

#endif /* YONA_COMPILE_GPU_VULKAN */
