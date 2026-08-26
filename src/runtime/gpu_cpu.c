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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Defined by the shared runtime after this implementation is included in
 * compiled_runtime.c.  Keep all ADT consumers on the canonical accessor API;
 * the declarations are needed because gpu_cpu.c is included earlier. */
int64_t yona_rt_adt_get_tag(void* node);
int64_t yona_rt_adt_get_field(void* node, int64_t index);

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

int64_t yona_Std_GPU_raw__vulkanLastIssueKind(int64_t unused) {
    (void)unused;
#if YONA_GPU_VULKAN_ENABLED
    return (int64_t)yona_gpu_vulkan_device_last_issue_kind();
#else
    return 0;
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
    /* Device ready is enough: i64 kernels when shaderInt64, else i32 when values fit. */
    yona_gpu_has_gpu_cached = 1;
    return (int64_t)yona_gpu_has_gpu_cached;
#else
    return 0;
#endif
}

int64_t yona_Std_GPU_raw__vulkanAvailable(int64_t unused) {
    (void)unused;
    return yona_gpu_vulkan_loader_available() ? 1 : 0;
}

int64_t yona_Std_GPU_raw__vulkanTimelineSemaphore(int64_t unused) {
    (void)unused;
#if YONA_GPU_VULKAN_ENABLED
    if (yona_gpu_vulkan_device_try_init() != 0) return 0;
    return yona_gpu_vulkan_device_timeline_semaphore() ? 1 : 0;
#else
    return 0;
#endif
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
#if YONA_GPU_VULKAN_ENABLED
    int64_t* gpu_out = NULL;
    if (yona_gpu_vulkan_try_filter_greater_than_int64(threshold, arr, &gpu_out)) return gpu_out;
#endif
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

int64_t* yona_Std_GPU_raw__mapSquare(int64_t* arr) {
#if YONA_GPU_VULKAN_ENABLED
    int64_t* gpu_out = NULL;
    if (yona_gpu_vulkan_try_map_square_int64(arr, &gpu_out)) return gpu_out;
#endif
    int64_t len = arr[0];
    int64_t* out = yona_rt_int_array_alloc(len);
    for (int64_t i = 0; i < len; i++)
        out[1 + i] = arr[1 + i] * arr[1 + i];
    return out;
}

int64_t* yona_Std_GPU_raw__filterLessThan(int64_t threshold, int64_t* arr) {
#if YONA_GPU_VULKAN_ENABLED
    int64_t* gpu_out = NULL;
    if (yona_gpu_vulkan_try_filter_less_than_int64(threshold, arr, &gpu_out)) return gpu_out;
#endif
    int64_t len = arr[0];
    int64_t count = 0;
    for (int64_t i = 0; i < len; i++)
        if (arr[1 + i] < threshold) count++;

    int64_t* out = yona_rt_int_array_alloc(count);
    int64_t j = 0;
    for (int64_t i = 0; i < len; i++) {
        int64_t v = arr[1 + i];
        if (v < threshold) out[1 + j++] = v;
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

int64_t yona_Std_GPU__vulkanLastIssueKind(void) {
    return yona_Std_GPU_raw__vulkanLastIssueKind(0);
}

int64_t yona_Std_GPU__vulkanAvailable(void) {
    return yona_Std_GPU_raw__vulkanAvailable(0);
}

int64_t yona_Std_GPU__vulkanTimelineSemaphore(void) {
    return yona_Std_GPU_raw__vulkanTimelineSemaphore(0);
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

int64_t yona_Std_GPU__filterLessThan(int64_t threshold, int64_t buffer) {
    int64_t* values = yona_Std_GPU_raw__filterLessThan(threshold, yona_gpu_buffer_values(buffer));
    return (int64_t)(intptr_t)yona_gpu_make_buffer(values);
}

int64_t yona_Std_GPU__mapSquare(int64_t buffer) {
    int64_t* values = yona_Std_GPU_raw__mapSquare(yona_gpu_buffer_values(buffer));
    return (int64_t)(intptr_t)yona_gpu_make_buffer(values);
}

int64_t yona_Std_GPU__reduceSum(int64_t buffer) {
    return yona_Std_GPU_raw__reduceSum(yona_gpu_buffer_values(buffer));
}

double* yona_Std_GPU_raw__mapFloatOp(int64_t* op, double* arr);
double yona_Std_GPU_raw__reduceFloatGPU(double* arr);
int64_t yona_Std_GPU_raw__mapReduceGraph(int64_t* stages, int64_t* arr);
int64_t yona_Std_GPU_raw__allocPinnedFloats(int64_t n);
int64_t yona_Std_GPU_raw__closePinnedFloats(int64_t handle);
int64_t yona_Std_GPU_raw__pinnedLength(int64_t handle);
double yona_Std_GPU_raw__pinnedGet(int64_t handle, int64_t i);
int64_t yona_Std_GPU_raw__pinnedSet(int64_t handle, int64_t i, double v);
double* yona_Std_GPU_raw__pinnedToFloatArray(int64_t handle);
int64_t yona_Std_GPU_raw__copyFloatArrayToPinned(double* arr, int64_t handle);
const char* yona_Std_GPU_raw__pinnedBackend(int64_t handle);
int64_t yona_Std_GPU_raw__mapFloatPinnedScale(double scale, int64_t handle);
int64_t yona_Std_GPU_raw__mapFloatPinnedMul2(int64_t handle);
int64_t yona_Std_GPU_raw__mapFloatPinnedOp(int64_t* op, int64_t handle);

int64_t yona_Std_GPU__mapGPU(int64_t op, int64_t buffer) {
    int64_t* adt = (int64_t*)(intptr_t)op;
    int64_t tag = adt[0];
    if (tag == 2) return yona_Std_GPU__mapSquare(buffer);
    int64_t arg = adt[3];
    if (tag == 0) return yona_Std_GPU__mapAdd(arg, buffer);
    return yona_Std_GPU__mapMul(arg, buffer);
}

int64_t yona_Std_GPU__reduceGPU(int64_t op, int64_t buffer) {
    (void)op;
    return yona_Std_GPU__reduceSum(buffer);
}

double* yona_Std_GPU__mapFloatGPU(int64_t op, double* arr) {
    return yona_Std_GPU_raw__mapFloatOp((int64_t*)(intptr_t)op, arr);
}

double yona_Std_GPU__reduceFloatGPU(int64_t op, double* arr) {
    (void)op;
    return yona_Std_GPU_raw__reduceFloatGPU(arr);
}

int64_t yona_Std_GPU__mapReduceGraphGPU(int64_t stages, int64_t buffer) {
    int64_t* seq = (int64_t*)(intptr_t)stages;
    int64_t n = yona_rt_seq_length(seq);
    if (n < 0) n = 0;
    int64_t* enc = yona_rt_int_array_alloc(n * 2);
    for (int64_t i = 0; i < n; i++) {
        void* op = (void*)(intptr_t)yona_rt_seq_get(seq, i);
        int64_t tag = yona_rt_adt_get_tag(op);
        enc[1 + i * 2] = tag;
        enc[1 + i * 2 + 1] =
            (tag == 2) ? 0 : yona_rt_adt_get_field(op, 0);
    }
    int64_t sum = yona_Std_GPU_raw__mapReduceGraph(enc, yona_gpu_buffer_values(buffer));
    yona_rt_rc_dec(enc);
    return sum;
}

int64_t yona_Std_GPU__allocPinnedFloats(int64_t n) {
    int64_t h = yona_Std_GPU_raw__allocPinnedFloats(n);
    int64_t* adt = (int64_t*)rc_alloc(RC_TYPE_ADT, 4 * sizeof(int64_t));
    adt[0] = 0;
    adt[1] = 1;
    adt[2] = 0;
    adt[3] = h;
    return (int64_t)(intptr_t)adt;
}

static int64_t yona_gpu_pinned_handle(int64_t pf) {
    int64_t* adt = (int64_t*)(intptr_t)pf;
    return adt[3];
}

int64_t yona_Std_GPU__closePinnedFloats(int64_t pf) {
    return yona_Std_GPU_raw__closePinnedFloats(yona_gpu_pinned_handle(pf));
}

int64_t yona_Std_GPU__pinnedLength(int64_t pf) {
    return yona_Std_GPU_raw__pinnedLength(yona_gpu_pinned_handle(pf));
}

double yona_Std_GPU__pinnedGet(int64_t pf, int64_t i) {
    return yona_Std_GPU_raw__pinnedGet(yona_gpu_pinned_handle(pf), i);
}

int64_t yona_Std_GPU__pinnedSet(int64_t pf, int64_t i, double v) {
    return yona_Std_GPU_raw__pinnedSet(yona_gpu_pinned_handle(pf), i, v);
}

double* yona_Std_GPU__pinnedToFloatArray(int64_t pf) {
    return yona_Std_GPU_raw__pinnedToFloatArray(yona_gpu_pinned_handle(pf));
}

int64_t yona_Std_GPU__copyFloatArrayToPinned(double* arr, int64_t pf) {
    return yona_Std_GPU_raw__copyFloatArrayToPinned(arr, yona_gpu_pinned_handle(pf));
}

const char* yona_Std_GPU__pinnedBackend(int64_t pf) {
    return yona_Std_GPU_raw__pinnedBackend(yona_gpu_pinned_handle(pf));
}

int64_t yona_Std_GPU__mapFloatPinnedGPU(int64_t op, int64_t pf) {
    int64_t* adt = (int64_t*)(intptr_t)op;
    int64_t h = yona_gpu_pinned_handle(pf);
    if (!adt) return -1;
    if (adt[0] == 0) {
        int64_t bits = adt[3];
        double scale;
        memcpy(&scale, &bits, sizeof(double));
        return yona_Std_GPU_raw__mapFloatPinnedScale(scale, h);
    }
    return yona_Std_GPU_raw__mapFloatPinnedMul2(h);
}

extern double* yona_rt_float_array_alloc(int64_t count);
extern int64_t yona_rt_float_array_length(double* arr);
extern void yona_rt_rc_dec(void* ptr);
extern int64_t yona_rt_seq_length(int64_t* seq);
extern int64_t yona_rt_seq_get(int64_t* seq, int64_t index);
extern int yona_gpu_vulkan_ctx_init(void);
extern int yona_gpu_vulkan_float64_buffer_scale_inplace(double* elements, uint32_t count, double scale);
extern int yona_gpu_vulkan_float64_buffer_reduce_sum(const double* elements, uint32_t count, double* out_sum);
extern int yona_gpu_vulkan_alloc_pinned_floats(int64_t count, double** out_host, void** out_opaque);
extern void yona_gpu_vulkan_free_pinned_floats(void* opaque);
#if YONA_GPU_VULKAN_ENABLED
extern int yona_gpu_vulkan_try_map_reduce_graph_int64(int64_t* stages, int64_t* arr, int64_t* out_sum);
#endif

typedef struct yona_gpu_pinned_floats {
    double* host;
    int64_t count;
    int owns_malloc; /* 1 = free(host); 0 when Vulkan-mapped (vk opaque owns) */
    void* vk_opaque; /* from yona_gpu_vulkan_alloc_pinned_floats, or NULL */
} yona_gpu_pinned_floats_t;

static double* yona_gpu_float_map_scale(double scale, double* arr) {
    int64_t len = yona_rt_float_array_length(arr);
    double* out = yona_rt_float_array_alloc(len);
    if (!out) return arr;
    memcpy(out, arr, (size_t)len * sizeof(double));
    if (len > 0 && len <= (int64_t)0xffffffffu) {
        if (yona_gpu_vulkan_ctx_init() == 0) {
            if (yona_gpu_vulkan_float64_buffer_scale_inplace(out, (uint32_t)len, scale) == 0)
                return out;
        }
    }
    for (int64_t i = 0; i < len; i++)
        out[i] *= scale;
    return out;
}

double* yona_Std_GPU_raw__mapFloatScale(double scale, double* arr) {
    return yona_gpu_float_map_scale(scale, arr);
}

double* yona_Std_GPU_raw__mapFloatMul2(double* arr) {
    return yona_gpu_float_map_scale(2.0, arr);
}

double* yona_Std_GPU_raw__mapFloatOp(int64_t* op, double* arr) {
    if (!op || !arr) return arr;
    int64_t tag = op[0];
    if (tag == 0) {
        int64_t bits = op[3];
        double scale;
        memcpy(&scale, &bits, sizeof(double));
        return yona_gpu_float_map_scale(scale, arr);
    }
    return yona_gpu_float_map_scale(2.0, arr);
}

double yona_Std_GPU_raw__reduceFloatGPU(double* arr) {
    int64_t len = yona_rt_float_array_length(arr);
    if (len > 0 && len <= (int64_t)0xffffffffu) {
        if (yona_gpu_vulkan_ctx_init() == 0) {
            double gpu_sum = 0.0;
            if (yona_gpu_vulkan_float64_buffer_reduce_sum(arr, (uint32_t)len, &gpu_sum) == 0)
                return gpu_sum;
        }
    }
    double sum = 0.0;
    for (int64_t i = 0; i < len; i++)
        sum += arr[i];
    return sum;
}

static int64_t yona_gpu_map_reduce_graph_cpu(int64_t* stages, int64_t* arr) {
    int64_t nflat = stages[0];
    if (nflat < 0 || (nflat % 2) != 0) return yona_Std_GPU_raw__reduceSum(arr);
    int64_t* cur = yona_gpu_copy_int_array(arr);
    for (int64_t i = 0; i + 1 < nflat; i += 2) {
        int64_t op = stages[1 + i];
        int64_t arg = stages[1 + i + 1];
        int64_t* next = cur;
        if (op == 0)
            next = yona_Std_GPU_raw__mapAdd(arg, cur);
        else if (op == 1)
            next = yona_Std_GPU_raw__mapMul(arg, cur);
        else if (op == 2)
            next = yona_Std_GPU_raw__mapSquare(cur);
        if (next != cur) {
            yona_rt_rc_dec(cur);
            cur = next;
        }
    }
    int64_t sum = yona_Std_GPU_raw__reduceSum(cur);
    yona_rt_rc_dec(cur);
    return sum;
}

int64_t yona_Std_GPU_raw__mapReduceGraph(int64_t* stages, int64_t* arr) {
#if YONA_GPU_VULKAN_ENABLED
    int64_t gpu_sum = 0;
    if (yona_gpu_vulkan_try_map_reduce_graph_int64(stages, arr, &gpu_sum)) return gpu_sum;
#endif
    return yona_gpu_map_reduce_graph_cpu(stages, arr);
}

int64_t yona_Std_GPU_raw__allocPinnedFloats(int64_t n) {
    if (n < 0) n = 0;
    yona_gpu_pinned_floats_t* p = (yona_gpu_pinned_floats_t*)calloc(1, sizeof(*p));
    if (!p) return 0;
    p->count = n;
    p->owns_malloc = 1;
    p->vk_opaque = NULL;
    p->host = NULL;

    const char* force_malloc = getenv("YONA_GPU_PINNED_HOST_MALLOC");
    const int skip_vk = (force_malloc && force_malloc[0] == '1' && force_malloc[1] == 0);

    if (!skip_vk) {
        double* mapped = NULL;
        void* opaque = NULL;
        if (yona_gpu_vulkan_alloc_pinned_floats(n, &mapped, &opaque) == 0 && opaque) {
            p->host = mapped;
            p->vk_opaque = opaque;
            p->owns_malloc = 0;
            return (int64_t)(intptr_t)p;
        }
    }

    if (n > 0) {
        p->host = (double*)calloc((size_t)n, sizeof(double));
        if (!p->host) {
            free(p);
            return 0;
        }
    }
    return (int64_t)(intptr_t)p;
}

int64_t yona_Std_GPU_raw__closePinnedFloats(int64_t handle) {
    yona_gpu_pinned_floats_t* p = (yona_gpu_pinned_floats_t*)(intptr_t)handle;
    if (!p) return 0;
    if (p->vk_opaque) {
        yona_gpu_vulkan_free_pinned_floats(p->vk_opaque);
        p->vk_opaque = NULL;
        p->host = NULL;
    } else if (p->owns_malloc && p->host) {
        free(p->host);
    }
    free(p);
    return 0;
}

int64_t yona_Std_GPU_raw__pinnedLength(int64_t handle) {
    yona_gpu_pinned_floats_t* p = (yona_gpu_pinned_floats_t*)(intptr_t)handle;
    return p ? p->count : 0;
}

double yona_Std_GPU_raw__pinnedGet(int64_t handle, int64_t i) {
    yona_gpu_pinned_floats_t* p = (yona_gpu_pinned_floats_t*)(intptr_t)handle;
    if (!p || !p->host || i < 0 || i >= p->count) return 0.0;
    return p->host[i];
}

int64_t yona_Std_GPU_raw__pinnedSet(int64_t handle, int64_t i, double v) {
    yona_gpu_pinned_floats_t* p = (yona_gpu_pinned_floats_t*)(intptr_t)handle;
    if (!p || !p->host || i < 0 || i >= p->count) return -1;
    p->host[i] = v;
    return 0;
}

double* yona_Std_GPU_raw__pinnedToFloatArray(int64_t handle) {
    yona_gpu_pinned_floats_t* p = (yona_gpu_pinned_floats_t*)(intptr_t)handle;
    if (!p) return yona_rt_float_array_alloc(0);
    double* out = yona_rt_float_array_alloc(p->count);
    if (!out) return NULL;
    if (p->count > 0 && p->host)
        memcpy(out, p->host, (size_t)p->count * sizeof(double));
    return out;
}

int64_t yona_Std_GPU_raw__copyFloatArrayToPinned(double* arr, int64_t handle) {
    yona_gpu_pinned_floats_t* p = (yona_gpu_pinned_floats_t*)(intptr_t)handle;
    if (!p || !arr) return -1;
    int64_t len = yona_rt_float_array_length(arr);
    if (len != p->count || !p->host) return -2;
    memcpy(p->host, arr, (size_t)len * sizeof(double));
    return 0;
}

const char* yona_Std_GPU_raw__pinnedBackend(int64_t handle) {
    yona_gpu_pinned_floats_t* p = (yona_gpu_pinned_floats_t*)(intptr_t)handle;
    if (!p)
        return yona_gpu_string("invalid");
    if (p->vk_opaque)
        return yona_gpu_string("vulkan-mapped");
    return yona_gpu_string("host-malloc");
}

int64_t yona_Std_GPU_raw__mapFloatPinnedScale(double scale, int64_t handle) {
    yona_gpu_pinned_floats_t* p = (yona_gpu_pinned_floats_t*)(intptr_t)handle;
    if (!p || !p->host) return -1;
    int64_t len = p->count;
    if (len <= 0) return 0;
    if (len <= (int64_t)0xffffffffu) {
        if (yona_gpu_vulkan_ctx_init() == 0) {
            if (yona_gpu_vulkan_float64_buffer_scale_inplace(p->host, (uint32_t)len, scale) == 0)
                return 0;
        }
    }
    for (int64_t i = 0; i < len; i++)
        p->host[i] *= scale;
    return 0;
}

int64_t yona_Std_GPU_raw__mapFloatPinnedMul2(int64_t handle) {
    return yona_Std_GPU_raw__mapFloatPinnedScale(2.0, handle);
}

int64_t yona_Std_GPU_raw__mapFloatPinnedOp(int64_t* op, int64_t handle) {
    if (!op) return -1;
    int64_t tag = op[0];
    if (tag == 0) {
        int64_t bits = op[3];
        double scale;
        memcpy(&scale, &bits, sizeof(double));
        return yona_Std_GPU_raw__mapFloatPinnedScale(scale, handle);
    }
    return yona_Std_GPU_raw__mapFloatPinnedMul2(handle);
}
