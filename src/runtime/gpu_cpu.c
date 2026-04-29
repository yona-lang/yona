/* ===== Std\GPU CPU backend =====
 *
 * Portable columnar accelerator fallback. The API deliberately copies on
 * upload/materialize so the CPU path preserves device-style ownership
 * boundaries while remaining available on every host.
 */

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
#include <emmintrin.h>
#define YONA_GPU_CPU_HAS_SSE2 1
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define YONA_GPU_CPU_HAS_NEON 1
#endif

#include "yona/runtime/gpu_build_config.h"
#include "yona/runtime/gpu_vulkan_device.h"

#include <stdlib.h>
#include <string.h>

#if YONA_GPU_VULKAN_ENABLED
static int yona_gpu_has_gpu_cached = -1;
#endif

void yona_gpu_vulkan_invalidate_capability_cache(void) {
#if YONA_GPU_VULKAN_ENABLED
    yona_gpu_has_gpu_cached = -1;
#endif
}

static char* yona_gpu_string(const char* s) {
    size_t len = strlen(s);
    char* out = (char*)rc_alloc(RC_TYPE_STRING, len + 1);
    memcpy(out, s, len + 1);
    return out;
}

static int yona_gpu_cpu_has_simd(void) {
#if defined(YONA_GPU_CPU_HAS_SSE2) || defined(YONA_GPU_CPU_HAS_NEON)
    return 1;
#else
    return 0;
#endif
}

static int64_t* yona_gpu_copy_int_array(int64_t* arr) {
    int64_t len = arr[0];
    int64_t* out = yona_rt_int_array_alloc(len);
    memcpy(out + 1, arr + 1, (size_t)len * sizeof(int64_t));
    return out;
}

static int64_t* yona_gpu_make_buffer(int64_t* values) {
    int64_t* buffer = (int64_t*)rc_alloc(RC_TYPE_ADT, 4 * sizeof(int64_t));
    buffer[0] = 0; /* Buffer */
    buffer[1] = 1;
    buffer[2] = 1; /* field 0 is heap-owned */
    buffer[3] = (int64_t)(intptr_t)values;
    return buffer;
}

static int64_t* yona_gpu_buffer_values(int64_t buffer) {
    int64_t* adt = (int64_t*)(intptr_t)buffer;
    return (int64_t*)(intptr_t)adt[3];
}

const char* yona_Std_GPU_raw__backendName(int64_t unused) {
    (void)unused;
    return yona_gpu_string(yona_gpu_cpu_has_simd() ? "cpu-simd" : "cpu-scalar");
}

const char* yona_Std_GPU_raw__vulkanStatus(int64_t unused) {
    (void)unused;
    return yona_gpu_string(yona_gpu_vulkan_status_name());
}

const char* yona_Std_GPU_raw__vulkanLastNote(int64_t unused) {
    (void)unused;
#if YONA_GPU_VULKAN_ENABLED
    return yona_gpu_string(yona_gpu_vulkan_device_last_note());
#else
    return yona_gpu_string("");
#endif
}

int64_t yona_Std_GPU_raw__hasGpu(int64_t unused) {
    (void)unused;
#if YONA_GPU_VULKAN_ENABLED
    if (yona_gpu_has_gpu_cached >= 0) return (int64_t)yona_gpu_has_gpu_cached;
    const char* dis = getenv("YONA_GPU_DISABLE_VULKAN");
    if (dis && dis[0] && strcmp(dis, "0") != 0) {
        yona_gpu_has_gpu_cached = 0;
        return 0;
    }
    int ti = yona_gpu_vulkan_device_try_init();
    if (ti != 0) {
        yona_gpu_has_gpu_cached = 0;
        return 0;
    }
    yona_gpu_has_gpu_cached = yona_gpu_vulkan_device_shader_int64() ? 1 : 0;
    return (int64_t)yona_gpu_has_gpu_cached;
#else
    return 0;
#endif
}

int64_t yona_Std_GPU_raw__vulkanAvailable(int64_t unused) {
    (void)unused;
    return yona_gpu_vulkan_loader_available() ? 1 : 0;
}

int64_t yona_Std_GPU_raw__hasSimd(int64_t unused) {
    (void)unused;
    return yona_gpu_cpu_has_simd() ? 1 : 0;
}

int64_t* yona_Std_GPU_raw__upload(int64_t* arr) {
    return yona_gpu_copy_int_array(arr);
}

int64_t* yona_Std_GPU_raw__materialize(int64_t* arr) {
    return yona_gpu_copy_int_array(arr);
}

int64_t yona_Std_GPU_raw__length(int64_t* arr) {
    return yona_rt_int_array_length(arr);
}

int64_t* yona_Std_GPU_raw__mapAdd(int64_t delta, int64_t* arr) {
#if YONA_GPU_VULKAN_ENABLED
    int64_t* gpu_out = NULL;
    if (yona_gpu_vulkan_try_map_add_int64(delta, arr, &gpu_out)) return gpu_out;
#endif
    int64_t len = arr[0];
    int64_t* out = yona_rt_int_array_alloc(len);
    for (int64_t i = 0; i < len; i++)
        out[1 + i] = arr[1 + i] + delta;
    return out;
}

int64_t* yona_Std_GPU_raw__mapMul(int64_t factor, int64_t* arr) {
#if YONA_GPU_VULKAN_ENABLED
    int64_t* gpu_out = NULL;
    if (yona_gpu_vulkan_try_map_mul_int64(factor, arr, &gpu_out)) return gpu_out;
#endif
    int64_t len = arr[0];
    int64_t* out = yona_rt_int_array_alloc(len);
    for (int64_t i = 0; i < len; i++)
        out[1 + i] = arr[1 + i] * factor;
    return out;
}

int64_t* yona_Std_GPU_raw__filterGreaterThan(int64_t threshold, int64_t* arr) {
    int64_t len = arr[0];
    int64_t count = 0;
    for (int64_t i = 0; i < len; i++)
        if (arr[1 + i] > threshold) count++;

    int64_t* out = yona_rt_int_array_alloc(count);
    int64_t j = 0;
    for (int64_t i = 0; i < len; i++) {
        int64_t v = arr[1 + i];
        if (v > threshold) out[1 + j++] = v;
    }
    return out;
}

int64_t yona_Std_GPU_raw__reduceSum(int64_t* arr) {
#if YONA_GPU_VULKAN_ENABLED
    int64_t gpu_sum = 0;
    if (yona_gpu_vulkan_try_reduce_sum_int64(arr, &gpu_sum)) return gpu_sum;
#endif
    int64_t len = arr[0];
    int64_t* data = arr + 1;
    int64_t i = 0;
    int64_t sum = 0;

#if defined(YONA_GPU_CPU_HAS_SSE2)
    __m128i acc = _mm_setzero_si128();
    for (; i + 1 < len; i += 2) {
        __m128i v = _mm_loadu_si128((const __m128i*)(const void*)(data + i));
        acc = _mm_add_epi64(acc, v);
    }
    int64_t lanes[2];
    _mm_storeu_si128((__m128i*)(void*)lanes, acc);
    sum = lanes[0] + lanes[1];
#elif defined(YONA_GPU_CPU_HAS_NEON)
    int64x2_t acc = vdupq_n_s64(0);
    for (; i + 1 < len; i += 2) {
        int64x2_t v = vld1q_s64(data + i);
        acc = vaddq_s64(acc, v);
    }
    int64_t lanes[2];
    vst1q_s64(lanes, acc);
    sum = lanes[0] + lanes[1];
#endif

    for (; i < len; i++)
        sum += data[i];
    return sum;
}

const char* yona_Std_GPU__backendName(void) {
    return yona_Std_GPU_raw__backendName(0);
}

const char* yona_Std_GPU__vulkanStatus(void) {
    return yona_Std_GPU_raw__vulkanStatus(0);
}

const char* yona_Std_GPU__vulkanLastNote(void) {
    return yona_Std_GPU_raw__vulkanLastNote(0);
}

int64_t yona_Std_GPU__hasGpu(void) {
    return yona_Std_GPU_raw__hasGpu(0);
}

int64_t yona_Std_GPU__vulkanAvailable(void) {
    return yona_Std_GPU_raw__vulkanAvailable(0);
}

int64_t yona_Std_GPU__hasSimd(void) {
    return yona_Std_GPU_raw__hasSimd(0);
}

int64_t yona_Std_GPU__upload(int64_t arr) {
    int64_t* values = yona_Std_GPU_raw__upload((int64_t*)(intptr_t)arr);
    return (int64_t)(intptr_t)yona_gpu_make_buffer(values);
}

int64_t yona_Std_GPU__materialize(int64_t buffer) {
    int64_t* values = yona_gpu_buffer_values(buffer);
    return (int64_t)(intptr_t)yona_Std_GPU_raw__materialize(values);
}

int64_t yona_Std_GPU__length(int64_t buffer) {
    return yona_Std_GPU_raw__length(yona_gpu_buffer_values(buffer));
}

int64_t yona_Std_GPU__mapAdd(int64_t delta, int64_t buffer) {
    int64_t* values = yona_Std_GPU_raw__mapAdd(delta, yona_gpu_buffer_values(buffer));
    return (int64_t)(intptr_t)yona_gpu_make_buffer(values);
}

int64_t yona_Std_GPU__mapMul(int64_t factor, int64_t buffer) {
    int64_t* values = yona_Std_GPU_raw__mapMul(factor, yona_gpu_buffer_values(buffer));
    return (int64_t)(intptr_t)yona_gpu_make_buffer(values);
}

int64_t yona_Std_GPU__filterGreaterThan(int64_t threshold, int64_t buffer) {
    int64_t* values = yona_Std_GPU_raw__filterGreaterThan(threshold, yona_gpu_buffer_values(buffer));
    return (int64_t)(intptr_t)yona_gpu_make_buffer(values);
}

int64_t yona_Std_GPU__reduceSum(int64_t buffer) {
    return yona_Std_GPU_raw__reduceSum(yona_gpu_buffer_values(buffer));
}
