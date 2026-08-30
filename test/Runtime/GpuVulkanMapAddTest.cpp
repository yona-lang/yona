#include "yona/Runtime/Gpu/VulkanDevice.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>

extern "C" void YonaRuntimeRelease(void *ptr);

#if YONA_GPU_VULKAN_ENABLED

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

static void apple_require_gpu_ok(int ok) {
#if defined(__APPLE__)
  if (YonaRuntimeGpuVulkanLoaderAvailable()) {
    REQUIRE_MESSAGE(ok, std::string(YonaRuntimeGpuVulkanDeviceLastNote()));
  }
#else
  (void)ok;
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
#else
  (void)unsetenv("YONA_GPU_VULKAN_FILTER");
  (void)unsetenv("YONA_GPU_VULKAN_FILTER_MIN_LEN");
#endif
}

static void force_i32_set(void) {
#ifdef _WIN32
  (void)_putenv_s("YONA_GPU_VULKAN_FORCE_I32", "1");
#else
  (void)setenv("YONA_GPU_VULKAN_FORCE_I32", "1", 1);
#endif
}

static void force_i32_clear(void) {
#ifdef _WIN32
  (void)_putenv_s("YONA_GPU_VULKAN_FORCE_I32", "");
#else
  (void)unsetenv("YONA_GPU_VULKAN_FORCE_I32");
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
  YonaRuntimeGpuVulkanDeviceShutdown();
  mapadd_env_enable_small_arrays();

  int64_t buf[1 + 8];
  buf[0] = 8;
  for (int i = 0; i < 8; i++)
    buf[1 + i] = (int64_t)i;

  int64_t *out = nullptr;
  int ok = YonaRuntimeGpuVulkanTryMapAddInt64(100, buf, &out);

  mapadd_env_clear();
  YonaRuntimeGpuVulkanDeviceShutdown();

  if (!ok) {
    apple_require_gpu_ok(ok);
    MESSAGE("Vulkan mapAdd skipped: ", YonaRuntimeGpuVulkanDeviceLastNote());
    return;
  }

  REQUIRE(out != nullptr);
  CHECK(out[0] == 8);
  for (int i = 0; i < 8; i++)
    CHECK(out[1 + i] == (int64_t)i + 100);

  YonaRuntimeRelease((void *)out);
}

TEST_CASE("gpu vulkan mapadd: i32 path skips values outside int32") {
  YonaRuntimeGpuVulkanDeviceShutdown();
  int init_rc = YonaRuntimeGpuVulkanDeviceTryInitialize();
  int has_i64 = YonaRuntimeGpuVulkanDeviceHasShaderInt64();
  YonaRuntimeGpuVulkanDeviceShutdown();
  if (init_rc != 0) {
    MESSAGE("Vulkan mapAdd i32-skip probe skipped: ",
            YonaRuntimeGpuVulkanDeviceLastNote());
    return;
  }

  mapadd_env_enable_small_arrays();

  int64_t buf[1 + 2];
  buf[0] = 2;
  buf[1] = (int64_t)std::numeric_limits<int32_t>::max() + 1;
  buf[2] = 1;

  int64_t *out = nullptr;
  int ok = YonaRuntimeGpuVulkanTryMapAddInt64(1, buf, &out);

  std::string note = YonaRuntimeGpuVulkanDeviceLastNote();
  mapadd_env_clear();
  YonaRuntimeGpuVulkanDeviceShutdown();

  if (has_i64) {
    REQUIRE(ok);
    REQUIRE(out != nullptr);
    CHECK(out[1] == (int64_t)std::numeric_limits<int32_t>::max() + 2);
    YonaRuntimeRelease((void *)out);
    return;
  }
  CHECK(!ok);
  CHECK(note.find("int32") != std::string::npos);
}

TEST_CASE("gpu vulkan mapmul: optional gpu roundtrip") {
  YonaRuntimeGpuVulkanDeviceShutdown();
  mapmul_env_enable_small_arrays();

  int64_t buf[1 + 8];
  buf[0] = 8;
  for (int i = 0; i < 8; i++)
    buf[1 + i] = (int64_t)(i + 1);

  int64_t *out = nullptr;
  int ok = YonaRuntimeGpuVulkanTryMapMulInt64(10, buf, &out);

  mapmul_env_clear();
  YonaRuntimeGpuVulkanDeviceShutdown();

  if (!ok) {
    apple_require_gpu_ok(ok);
    MESSAGE("Vulkan mapMul skipped: ", YonaRuntimeGpuVulkanDeviceLastNote());
    return;
  }

  REQUIRE(out != nullptr);
  CHECK(out[0] == 8);
  for (int i = 0; i < 8; i++)
    CHECK(out[1 + i] == ((int64_t)(i + 1)) * 10);

  YonaRuntimeRelease((void *)out);
}

TEST_CASE("gpu vulkan reduce: optional gpu roundtrip") {
  YonaRuntimeGpuVulkanDeviceShutdown();
  reduce_env_enable_small_arrays();

  int64_t buf[1 + 8];
  buf[0] = 8;
  for (int i = 0; i < 8; i++)
    buf[1 + i] = (int64_t)(i + 1);

  int64_t sum = 0;
  int ok = YonaRuntimeGpuVulkanTryReduceSumInt64(buf, &sum);

  reduce_env_clear();
  YonaRuntimeGpuVulkanDeviceShutdown();

  if (!ok) {
    apple_require_gpu_ok(ok);
    MESSAGE("Vulkan reduceSum skipped: ", YonaRuntimeGpuVulkanDeviceLastNote());
    return;
  }

  CHECK(sum == 36);
}

TEST_CASE("gpu vulkan reduce: host_ssbo matches default path when gpu runs") {
  YonaRuntimeGpuVulkanDeviceShutdown();
  reduce_env_enable_small_arrays();

  int64_t buf[1 + 8];
  buf[0] = 8;
  for (int i = 0; i < 8; i++)
    buf[1 + i] = (int64_t)(i + 1);

  int64_t sum_default = 0;
  int ok_default = YonaRuntimeGpuVulkanTryReduceSumInt64(buf, &sum_default);
  reduce_env_clear();
  YonaRuntimeGpuVulkanDeviceShutdown();

  if (!ok_default) {
    MESSAGE("Vulkan reduceSum skipped: ", YonaRuntimeGpuVulkanDeviceLastNote());
    return;
  }

  YonaRuntimeGpuVulkanDeviceShutdown();
  vulkan_host_ssbo_set();
  reduce_env_enable_small_arrays();

  int64_t sum_host = 0;
  int ok_host = YonaRuntimeGpuVulkanTryReduceSumInt64(buf, &sum_host);

  reduce_env_clear();
  vulkan_host_ssbo_clear();
  YonaRuntimeGpuVulkanDeviceShutdown();

  REQUIRE(ok_host);
  CHECK(sum_host == sum_default);
}

TEST_CASE("gpu vulkan mapadd: host_ssbo matches default path when gpu runs") {
  YonaRuntimeGpuVulkanDeviceShutdown();
  mapadd_env_enable_small_arrays();

  int64_t buf[1 + 8];
  buf[0] = 8;
  for (int i = 0; i < 8; i++)
    buf[1 + i] = (int64_t)i;

  int64_t *out_default = nullptr;
  int ok_default = YonaRuntimeGpuVulkanTryMapAddInt64(100, buf, &out_default);

  mapadd_env_clear();
  YonaRuntimeGpuVulkanDeviceShutdown();

  if (!ok_default) {
    MESSAGE("Vulkan mapAdd skipped: ", YonaRuntimeGpuVulkanDeviceLastNote());
    return;
  }

  YonaRuntimeGpuVulkanDeviceShutdown();
  vulkan_host_ssbo_set();
  mapadd_env_enable_small_arrays();

  int64_t *out_host = nullptr;
  int ok_host = YonaRuntimeGpuVulkanTryMapAddInt64(100, buf, &out_host);

  mapadd_env_clear();
  vulkan_host_ssbo_clear();
  YonaRuntimeGpuVulkanDeviceShutdown();

  REQUIRE(ok_host);
  REQUIRE(out_host != nullptr);
  CHECK(out_default[0] == out_host[0]);
  for (int i = 0; i < 8; i++)
    CHECK(out_default[1 + i] == out_host[1 + i]);

  YonaRuntimeRelease((void *)out_default);
  YonaRuntimeRelease((void *)out_host);
}

TEST_CASE("gpu vulkan filterGreaterThan: optional gpu roundtrip") {
  YonaRuntimeGpuVulkanDeviceShutdown();
  filter_env_enable_small_arrays();

  int64_t buf[1 + 8];
  buf[0] = 8;
  for (int i = 0; i < 8; i++)
    buf[1 + i] = (int64_t)(i + 1);

  int64_t *out = nullptr;
  int ok = YonaRuntimeGpuVulkanTryFilterGreaterThanInt64(4, buf, &out);

  filter_env_clear();
  YonaRuntimeGpuVulkanDeviceShutdown();

  if (!ok) {
    apple_require_gpu_ok(ok);
    MESSAGE("Vulkan filterGreaterThan skipped: ",
            YonaRuntimeGpuVulkanDeviceLastNote());
    return;
  }

  REQUIRE(out != nullptr);
  CHECK(out[0] == 4);
  CHECK(out[1] == 5);
  CHECK(out[2] == 6);
  CHECK(out[3] == 7);
  CHECK(out[4] == 8);

  YonaRuntimeRelease((void *)out);
}

TEST_CASE("gpu vulkan filterGreaterThan: forced i32 path") {
  YonaRuntimeGpuVulkanDeviceShutdown();
  force_i32_set();
  filter_env_enable_small_arrays();

  int64_t buf[1 + 8];
  buf[0] = 8;
  for (int i = 0; i < 8; i++)
    buf[1 + i] = (int64_t)(i + 1);

  int64_t *out = nullptr;
  int ok = YonaRuntimeGpuVulkanTryFilterGreaterThanInt64(4, buf, &out);

  filter_env_clear();
  force_i32_clear();
  YonaRuntimeGpuVulkanDeviceShutdown();

  if (!ok) {
    apple_require_gpu_ok(ok);
    MESSAGE("Vulkan filterGreaterThan i32 skipped: ",
            YonaRuntimeGpuVulkanDeviceLastNote());
    return;
  }

  REQUIRE(out != nullptr);
  CHECK(out[0] == 4);
  CHECK(out[1] == 5);
  CHECK(out[2] == 6);
  CHECK(out[3] == 7);
  CHECK(out[4] == 8);

  YonaRuntimeRelease((void *)out);
}

TEST_CASE(
    "gpu vulkan filterGreaterThan: host_ssbo matches default when gpu runs") {
  YonaRuntimeGpuVulkanDeviceShutdown();
  filter_env_enable_small_arrays();

  int64_t buf[1 + 8];
  buf[0] = 8;
  for (int i = 0; i < 8; i++)
    buf[1 + i] = (int64_t)(i + 1);

  int64_t *out_default = nullptr;
  int ok_default =
      YonaRuntimeGpuVulkanTryFilterGreaterThanInt64(4, buf, &out_default);
  filter_env_clear();
  YonaRuntimeGpuVulkanDeviceShutdown();

  if (!ok_default) {
    MESSAGE("Vulkan filterGreaterThan skipped: ",
            YonaRuntimeGpuVulkanDeviceLastNote());
    return;
  }

  YonaRuntimeGpuVulkanDeviceShutdown();
  vulkan_host_ssbo_set();
  filter_env_enable_small_arrays();

  int64_t *out_host = nullptr;
  int ok_host =
      YonaRuntimeGpuVulkanTryFilterGreaterThanInt64(4, buf, &out_host);

  filter_env_clear();
  vulkan_host_ssbo_clear();
  YonaRuntimeGpuVulkanDeviceShutdown();

  REQUIRE(ok_host);
  REQUIRE(out_host != nullptr);
  CHECK(out_default[0] == out_host[0]);
  for (int64_t i = 0; i < out_default[0]; i++)
    CHECK(out_default[1 + i] == out_host[1 + i]);

  YonaRuntimeRelease((void *)out_default);
  YonaRuntimeRelease((void *)out_host);
}

TEST_CASE("gpu vulkan filterGreaterThan: empty and full passes when gpu runs") {
  YonaRuntimeGpuVulkanDeviceShutdown();
  filter_env_enable_small_arrays();

  int64_t buf[1 + 8];
  buf[0] = 8;
  for (int i = 0; i < 8; i++)
    buf[1 + i] = (int64_t)(i + 1);

  int64_t *none = nullptr;
  int ok_none = YonaRuntimeGpuVulkanTryFilterGreaterThanInt64(100, buf, &none);
  int64_t *all = nullptr;
  int ok_all = YonaRuntimeGpuVulkanTryFilterGreaterThanInt64(0, buf, &all);

  filter_env_clear();
  YonaRuntimeGpuVulkanDeviceShutdown();

  if (!ok_none || !ok_all) {
    apple_require_gpu_ok(ok_none && ok_all);
    MESSAGE("Vulkan filterGreaterThan edge cases skipped: ",
            YonaRuntimeGpuVulkanDeviceLastNote());
    return;
  }

  REQUIRE(none != nullptr);
  CHECK(none[0] == 0);
  REQUIRE(all != nullptr);
  CHECK(all[0] == 8);
  for (int i = 0; i < 8; i++)
    CHECK(all[1 + i] == (int64_t)(i + 1));

  YonaRuntimeRelease((void *)none);
  YonaRuntimeRelease((void *)all);
}

static void graph_env_enable_small_arrays(void) {
#ifdef _WIN32
  (void)_putenv_s("YONA_GPU_VULKAN_GRAPH", "1");
  (void)_putenv_s("YONA_GPU_VULKAN_GRAPH_MIN_LEN", "1");
#else
  (void)setenv("YONA_GPU_VULKAN_GRAPH", "1", 1);
  (void)setenv("YONA_GPU_VULKAN_GRAPH_MIN_LEN", "1", 1);
#endif
}

static void graph_env_clear(void) {
#ifdef _WIN32
  (void)_putenv_s("YONA_GPU_VULKAN_GRAPH", "");
  (void)_putenv_s("YONA_GPU_VULKAN_GRAPH_MIN_LEN", "");
#else
  (void)unsetenv("YONA_GPU_VULKAN_GRAPH");
  (void)unsetenv("YONA_GPU_VULKAN_GRAPH_MIN_LEN");
#endif
}

TEST_CASE("gpu vulkan mapReduceGraph Add then Mul then reduceSum") {
  graph_env_enable_small_arrays();
  if (YonaRuntimeGpuVulkanDeviceTryInitialize() != 0) {
    graph_env_clear();
    MESSAGE("Vulkan graph skipped: ", YonaRuntimeGpuVulkanDeviceLastNote());
    return;
  }

  int64_t stages[1 + 4];
  stages[0] = 4;
  stages[1] = 0; /* Add */
  stages[2] = 1;
  stages[3] = 1; /* Mul */
  stages[4] = 2;

  int64_t buf[1 + 5];
  buf[0] = 5;
  for (int i = 0; i < 5; i++)
    buf[1 + i] = (int64_t)(i + 1);

  int64_t sum = 0;
  int ok = YonaRuntimeGpuVulkanTryMapReduceGraphInt64(stages, buf, &sum);
  graph_env_clear();
  YonaRuntimeGpuVulkanDeviceShutdown();

  if (!ok) {
    apple_require_gpu_ok(ok);
    MESSAGE("Vulkan mapReduceGraph skipped: ",
            YonaRuntimeGpuVulkanDeviceLastNote());
    return;
  }
  /* (1..5)+1 then *2 → 4+6+8+10+12 = 40 */
  CHECK(sum == 40);
}

TEST_CASE("gpu vulkan mapReduceGraph Add then Square then reduceSum") {
  graph_env_enable_small_arrays();
  if (YonaRuntimeGpuVulkanDeviceTryInitialize() != 0) {
    graph_env_clear();
    MESSAGE("Vulkan graph skipped: ", YonaRuntimeGpuVulkanDeviceLastNote());
    return;
  }

  int64_t stages[1 + 4];
  stages[0] = 4;
  stages[1] = 0; /* Add */
  stages[2] = 1;
  stages[3] = 2; /* Square */
  stages[4] = 0;

  int64_t buf[1 + 5];
  buf[0] = 5;
  for (int i = 0; i < 5; i++)
    buf[1 + i] = (int64_t)(i + 1);

  int64_t sum = 0;
  int ok = YonaRuntimeGpuVulkanTryMapReduceGraphInt64(stages, buf, &sum);
  graph_env_clear();
  YonaRuntimeGpuVulkanDeviceShutdown();

  if (!ok) {
    apple_require_gpu_ok(ok);
    MESSAGE("Vulkan mapReduceGraph Square skipped: ",
            YonaRuntimeGpuVulkanDeviceLastNote());
    return;
  }
  /* (1..5)+1 then square → 4+9+16+25+36 = 90 */
  CHECK(sum == 90);
}

TEST_CASE("gpu vulkan filterLessThan: optional gpu roundtrip") {
  YonaRuntimeGpuVulkanDeviceShutdown();
  filter_env_enable_small_arrays();

  int64_t buf[1 + 8];
  buf[0] = 8;
  for (int i = 0; i < 8; i++)
    buf[1 + i] = (int64_t)(i + 1);

  int64_t *out = nullptr;
  int ok = YonaRuntimeGpuVulkanTryFilterLessThanInt64(4, buf, &out);

  filter_env_clear();
  YonaRuntimeGpuVulkanDeviceShutdown();

  if (!ok) {
    apple_require_gpu_ok(ok);
    MESSAGE("Vulkan filterLessThan skipped: ",
            YonaRuntimeGpuVulkanDeviceLastNote());
    return;
  }

  REQUIRE(out != nullptr);
  CHECK(out[0] == 3);
  CHECK(out[1] == 1);
  CHECK(out[2] == 2);
  CHECK(out[3] == 3);

  YonaRuntimeRelease((void *)out);
}

TEST_CASE("gpu vulkan mapSquare: optional gpu roundtrip") {
  YonaRuntimeGpuVulkanDeviceShutdown();
  mapmul_env_enable_small_arrays();

  int64_t buf[1 + 5];
  buf[0] = 5;
  for (int i = 0; i < 5; i++)
    buf[1 + i] = (int64_t)(i + 1);

  int64_t *out = nullptr;
  int ok = YonaRuntimeGpuVulkanTryMapSquareInt64(buf, &out);

  mapmul_env_clear();
  YonaRuntimeGpuVulkanDeviceShutdown();

  if (!ok) {
    apple_require_gpu_ok(ok);
    MESSAGE("Vulkan mapSquare skipped: ", YonaRuntimeGpuVulkanDeviceLastNote());
    return;
  }

  REQUIRE(out != nullptr);
  CHECK(out[0] == 5);
  CHECK(out[1] == 1);
  CHECK(out[2] == 4);
  CHECK(out[3] == 9);
  CHECK(out[4] == 16);
  CHECK(out[5] == 25);

  YonaRuntimeRelease((void *)out);
}

#endif /* YONA_GPU_VULKAN_ENABLED */
