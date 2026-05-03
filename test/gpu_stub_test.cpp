#include <cstdlib>
#include <doctest/doctest.h>
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

/* Set YONA_GPU_TEST_F64_MUL2=1 to run GPU double×2 on a stack buffer (needs shaderFloat64). */
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
