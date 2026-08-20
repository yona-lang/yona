#include <cstdlib>
#include <doctest/doctest.h>
#include <string>
#include "runtime/async_runtime.h"
#include "runtime/gpu_stub.h"

TEST_CASE("GPU: available is a boolean (0 without Vulkan probe, 1 when compute queue exists)") {
    const int64_t a = yona_Std_GPU__available(0);
    CHECK((a == 0 || a == 1));
}

TEST_CASE("GPU: apiVersion is 1") {
    CHECK(yona_Std_GPU__apiVersion(0) == 1);
}

TEST_CASE("GPU: physicalDeviceCount is non-negative") {
    CHECK(yona_Std_GPU__physicalDeviceCount(0) >= 0);
}

TEST_CASE("GPU: vulkan ctx init when discovery says compute is available") {
    if (yona_Std_GPU__available(0) != 1) {
        return;
    }
    CHECK(yona_gpu_vulkan_ctx_init() == 0);
    yona_gpu_vulkan_ctx_shutdown();
}

/* Set YONA_GPU_TEST_DISPATCH=1 to exercise submit + fence (driver-sensitive; default off). */
TEST_CASE("GPU: optional nop dispatch when YONA_GPU_TEST_DISPATCH is set") {
    const char* flag = std::getenv("YONA_GPU_TEST_DISPATCH");
    if (flag == nullptr || flag[0] == '\0') {
        return;
    }
    if (yona_Std_GPU__available(0) != 1) {
        return;
    }
    CHECK(yona_gpu_vulkan_ctx_init() == 0);
    CHECK(yona_gpu_vulkan_dispatch_nop_once() == 0);
    yona_gpu_vulkan_ctx_shutdown();
}

TEST_CASE("GPU: float mul2 uses f64 or f32 when compute is available") {
    if (yona_Std_GPU__available(0) != 1) {
        return;
    }
    CHECK(yona_gpu_vulkan_ctx_init() == 0);
    double data[4] = {1.0, 2.0, 3.0, 4.0};
    const int r = yona_gpu_vulkan_float64_buffer_mul2_inplace(data, 4);
    REQUIRE(r == 0);
    CHECK(data[0] == doctest::Approx(2.0));
    CHECK(data[1] == doctest::Approx(4.0));
    CHECK(data[2] == doctest::Approx(6.0));
    CHECK(data[3] == doctest::Approx(8.0));
    yona_gpu_vulkan_ctx_shutdown();
}

TEST_CASE("GPU: float reduce sum uses f64 or f32 when compute is available") {
    if (yona_Std_GPU__available(0) != 1) {
        return;
    }
    CHECK(yona_gpu_vulkan_ctx_init() == 0);
    double data[4] = {1.0, 2.0, 3.0, 4.0};
    double sum = 0.0;
    const int r = yona_gpu_vulkan_float64_buffer_reduce_sum(data, 4, &sum);
    REQUIRE(r == 0);
    CHECK(sum == doctest::Approx(10.0));
    yona_gpu_vulkan_ctx_shutdown();
}

/* Set YONA_GPU_TEST_F64_MUL2=1 to run GPU double×2 on a stack buffer (f64 or f32 fallback). */
TEST_CASE("GPU: optional f64 mul2 when YONA_GPU_TEST_F64_MUL2 is set") {
    const char* flag = std::getenv("YONA_GPU_TEST_F64_MUL2");
    if (flag == nullptr || flag[0] == '\0') {
        return;
    }
    if (yona_Std_GPU__available(0) != 1) {
        return;
    }
    CHECK(yona_gpu_vulkan_ctx_init() == 0);
    double data[4] = {1.0, 2.0, 3.0, 4.0};
    const int r = yona_gpu_vulkan_float64_buffer_mul2_inplace(data, 4);
    if (r == -20) {
        yona_gpu_vulkan_ctx_shutdown();
        return;
    }
    REQUIRE(r == 0);
    CHECK(data[0] == doctest::Approx(2.0));
    CHECK(data[1] == doctest::Approx(4.0));
    CHECK(data[2] == doctest::Approx(6.0));
    CHECK(data[3] == doctest::Approx(8.0));
    yona_gpu_vulkan_ctx_shutdown();
}

/* Set YONA_GPU_TEST_F64_MUL2_ASYNC=1 to await GPU mul2 via promise (dedicated fence thread). */
TEST_CASE("GPU: optional async f64 mul2 when YONA_GPU_TEST_F64_MUL2_ASYNC is set") {
    const char* flag = std::getenv("YONA_GPU_TEST_F64_MUL2_ASYNC");
    if (flag == nullptr || flag[0] == '\0') {
        return;
    }
    if (yona_Std_GPU__available(0) != 1) {
        return;
    }
    CHECK(yona_gpu_vulkan_ctx_init() == 0);
    double data[4] = {1.0, 2.0, 3.0, 4.0};
    yona_promise_t* pr = yona_gpu_vulkan_float64_buffer_mul2_async(data, 4, nullptr);
    REQUIRE(pr != nullptr);
    const int64_t r = yona_rt_async_await(pr);
    if (r == -20) {
        yona_gpu_vulkan_ctx_shutdown();
        return;
    }
    CHECK(r == 0);
    CHECK(data[0] == doctest::Approx(2.0));
    CHECK(data[1] == doctest::Approx(4.0));
    CHECK(data[2] == doctest::Approx(6.0));
    CHECK(data[3] == doctest::Approx(8.0));
    yona_gpu_vulkan_ctx_shutdown();
}

TEST_CASE("GPU: pinned floats alloc prefers vulkan-mapped or host-malloc") {
    double* host = nullptr;
    void* opaque = nullptr;
    const int r = yona_gpu_vulkan_alloc_pinned_floats(4, &host, &opaque);
    if (r != 0) {
        /* CPU-only build or no device: stub returns -1 */
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
    yona_gpu_vulkan_free_pinned_floats(opaque);
    yona_gpu_vulkan_ctx_shutdown();
}

extern "C" {
int64_t yona_Std_GPU_raw__allocPinnedFloats(int64_t n);
int64_t yona_Std_GPU_raw__closePinnedFloats(int64_t handle);
const char* yona_Std_GPU_raw__pinnedBackend(int64_t handle);
int64_t yona_Std_GPU_raw__pinnedSet(int64_t handle, int64_t i, double v);
double yona_Std_GPU_raw__pinnedGet(int64_t handle, int64_t i);
int64_t yona_Std_GPU_raw__mapFloatPinnedScale(double scale, int64_t handle);
}

TEST_CASE("GPU: Std\\GPU pinned malloc fallback and in-place scale") {
#if defined(_WIN32)
    (void)_putenv_s("YONA_GPU_PINNED_HOST_MALLOC", "1");
#else
    setenv("YONA_GPU_PINNED_HOST_MALLOC", "1", 1);
#endif
    int64_t h = yona_Std_GPU_raw__allocPinnedFloats(2);
    REQUIRE(h != 0);
    const char* backend = yona_Std_GPU_raw__pinnedBackend(h);
    REQUIRE(backend != nullptr);
    CHECK(std::string(backend) == "host-malloc");
    CHECK(yona_Std_GPU_raw__pinnedSet(h, 0, 3.0) == 0);
    CHECK(yona_Std_GPU_raw__pinnedSet(h, 1, 4.0) == 0);
    CHECK(yona_Std_GPU_raw__mapFloatPinnedScale(2.0, h) == 0);
    CHECK(yona_Std_GPU_raw__pinnedGet(h, 0) == doctest::Approx(6.0));
    CHECK(yona_Std_GPU_raw__pinnedGet(h, 1) == doctest::Approx(8.0));
    CHECK(yona_Std_GPU_raw__closePinnedFloats(h) == 0);
#if defined(_WIN32)
    (void)_putenv_s("YONA_GPU_PINNED_HOST_MALLOC", "");
#else
    unsetenv("YONA_GPU_PINNED_HOST_MALLOC");
#endif
}

extern "C" {
yona_task_group_t* yona_rt_group_begin(void);
void yona_rt_group_cancel(yona_task_group_t* g);
void yona_rt_group_end(void* g);
}

/* Set YONA_GPU_TEST_F64_GROUP_CANCEL=1: grouped mul2 promise completes -887 on cancel
 * (may finish before the GPU drain; host buffer write is discarded when cancelled). */
TEST_CASE("GPU: optional grouped f64 mul2 cancel") {
    const char* flag = std::getenv("YONA_GPU_TEST_F64_GROUP_CANCEL");
    if (flag == nullptr || flag[0] == '\0') {
        return;
    }
    if (yona_Std_GPU__available(0) != 1) {
        return;
    }
    CHECK(yona_gpu_vulkan_ctx_init() == 0);
    double data[4] = {1.0, 2.0, 3.0, 4.0};
    yona_task_group_t* g = yona_rt_group_begin();
    REQUIRE(g != nullptr);
    yona_promise_t* pr = yona_gpu_vulkan_float64_buffer_mul2_async(data, 4, g);
    REQUIRE(pr != nullptr);
    yona_rt_group_cancel(g);
    const int64_t r = yona_rt_async_await(pr);
    if (r == -20) {
        yona_rt_group_end(g);
        yona_gpu_vulkan_ctx_shutdown();
        return;
    }
    CHECK(r == -887);
    yona_rt_group_end(g);
    yona_gpu_vulkan_ctx_shutdown();
}
