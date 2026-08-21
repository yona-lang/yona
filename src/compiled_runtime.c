/*
 * Yona Runtime Library
 *
 * Linked with compiled Yona programs. Provides runtime support
 * for operations that can't be expressed as pure LLVM IR:
 * printing, string operations, memory management.
 */

#if defined(_WIN32)
#ifndef _CRT_DECLARE_NONSTDC_NAMES
#define _CRT_DECLARE_NONSTDC_NAMES 1
#endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <math.h>
#include <ctype.h>
#if !defined(_WIN32)
#include <pthread.h>
#endif
#include <setjmp.h>
#include <time.h>
#if defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#endif
#if defined(__has_include)
#  if __has_include("version.h")
#    include "version.h"
#  endif
#endif
#ifndef YONA_VERSION_STRING
#define YONA_VERSION_STRING "unknown"
#endif
#if defined(__linux__) || defined(__APPLE__)
#include <execinfo.h>
#define YONA_HAS_BACKTRACE 1
#elif defined(_WIN32)
#include <windows.h>
#include <dbghelp.h>
#define YONA_HAS_BACKTRACE 1
#endif

/* ===== Reference Counting Memory Management ===== */
/*
 * RC Header Layout (hidden before the returned payload pointer):
 *   [refcount: int64_t][type_tag: int64_t][...payload...]
 *                                         ^-- returned pointer
 *
 * refcount is at ptr[-2], type_tag is at ptr[-1].
 */

#define RC_TYPE_SEQ     1
#define RC_TYPE_SET     2
#define RC_TYPE_DICT    3
#define RC_TYPE_ADT     4
/* HAMT print/RC aux bits live in the type_tag word (see hamt.c). */
#ifndef HAMT_FLAG_KEY_HEAP
#define HAMT_FLAG_KEY_HEAP (1LL << 16)
#define HAMT_FLAG_VAL_HEAP (1LL << 17)
#define HAMT_FLAG_IS_SET   (1LL << 18)
#endif
void yona_rt_hamt_destroy_children(void* node);
void yona_rt_hamt_stamp_aux_flags(void* node, int64_t flags);
#define RC_TYPE_CLOSURE 5
#define RC_TYPE_STRING  6
#define RC_TYPE_INT_ARRAY   18
#define RC_TYPE_FLOAT_ARRAY 19

/* ===== Size-class pool allocator ===== */
/* Free-list pools for common allocation sizes. Avoids malloc/free overhead
 * for frequently allocated objects (closures, seq chunks, small ADTs).
 * Each pool is a linked list of freed blocks, reused on next alloc. */

#define POOL_CLASSES 5
static const size_t pool_sizes[POOL_CLASSES] = {
    32,   /* small closures (5-slot header) */
    64,   /* small ADTs, tuples, flat seqs */
    128,  /* medium closures with captures */
    296,  /* RBT trie nodes, leaves, head chunks (272-296 bytes with RC header) */
    608,  /* rbt_t root struct (592 bytes payload + 16 RC header) */
};

typedef struct pool_block {
    struct pool_block* next;
} pool_block_t;

static _Thread_local pool_block_t* pool_freelist[POOL_CLASSES] = {0};

static int pool_class_for(size_t total_bytes) {
    for (int i = 0; i < POOL_CLASSES; i++) {
        if (total_bytes <= pool_sizes[i]) return i;
    }
    return -1; /* too large for pools */
}

/* Slab allocator: instead of individual malloc per block, allocate slabs
 * of many blocks at once. Blocks within a slab never go through glibc's
 * free/tcache, eliminating the tcache corruption issue. */
#define SLAB_BLOCKS 256  /* blocks per slab */

typedef struct slab {
    struct slab* next;
    char data[];  /* flexible array of SLAB_BLOCKS * block_size bytes */
} slab_t;

static _Thread_local slab_t* pool_slabs[POOL_CLASSES] = {0};

/* Allocation statistics — enabled when YONA_ALLOC_STATS env var is set at
 * program start. The atomic increments are cheap; the report destructor
 * prints to stderr only if the env var is set. Per-tag breakdown helps
 * localize leaks (see bench/core/queens.yona investigation that uncovered
 * the seq_tail copy-path leak). */
#include <stdatomic.h>
static _Atomic long long yona_alloc_n = 0;
static _Atomic long long yona_free_n = 0;
static _Atomic long long yona_slab_bytes = 0;
#define YONA_NUM_TAGS 32
static _Atomic long long yona_alloc_by_tag[YONA_NUM_TAGS];
static _Atomic long long yona_free_by_tag[YONA_NUM_TAGS];
static const char* yona_tag_name(int tag) {
    switch (tag) {
        case 1:  return "SEQ";
        case 2:  return "SET";
        case 3:  return "DICT";
        case 4:  return "ADT";
        case 5:  return "CLOSURE";
        case 6:  return "STRING";
        case 7:  return "TUPLE";
        case 8:  return "BYTEARR";
        case 11: return "CHUNKED";
        case 12: return "RBT";
        case 13: return "RBT_NODE";
        case 14: return "RBT_LEAF";
        case 15: return "RBT_CHUNK";
        case 16: return "REGEX";
        case 17: return "PROCESS";
        case 18: return "INTARR";
        case 19: return "FLOATARR";
        case 20: return "CHANNEL";
        default: return "other";
    }
}
/* Register via atexit so we run before the C runtime tears down stdio.
 * On Windows, __attribute__((destructor)) executes after CRT stream
 * shutdown and crashed when this function tried to fprintf(stderr, …). */
static void yona_alloc_report(void) {
    if (!getenv("YONA_ALLOC_STATS")) return;
    fprintf(stderr, "[alloc-stats] allocs=%lld frees=%lld slab_bytes=%lld\n",
            atomic_load(&yona_alloc_n), atomic_load(&yona_free_n),
            atomic_load(&yona_slab_bytes));
    for (int t = 0; t < YONA_NUM_TAGS; t++) {
        long long a = atomic_load(&yona_alloc_by_tag[t]);
        long long f = atomic_load(&yona_free_by_tag[t]);
        if (a == 0 && f == 0) continue;
        fprintf(stderr, "[alloc-stats]   tag=%s allocs=%lld frees=%lld leaked=%lld\n",
                yona_tag_name(t), a, f, a - f);
    }
    fflush(stderr);
}
static _Atomic int yona_alloc_report_registered = 0;
static inline void yona_alloc_report_maybe_register(void) {
    int expected = 0;
    if (atomic_compare_exchange_strong_explicit(
            &yona_alloc_report_registered, &expected, 1,
            memory_order_acq_rel, memory_order_acquire)) {
        atexit(yona_alloc_report);
    }
}
#define YONA_ALLOC_INC_TAG(tag) do { \
    atomic_fetch_add_explicit(&yona_alloc_n, 1, memory_order_relaxed); \
    if ((tag) >= 0 && (tag) < YONA_NUM_TAGS) \
        atomic_fetch_add_explicit(&yona_alloc_by_tag[(tag)], 1, memory_order_relaxed); \
} while(0)
#define YONA_FREE_INC_TAG(tag) do { \
    atomic_fetch_add_explicit(&yona_free_n, 1, memory_order_relaxed); \
    if ((tag) >= 0 && (tag) < YONA_NUM_TAGS) \
        atomic_fetch_add_explicit(&yona_free_by_tag[(tag)], 1, memory_order_relaxed); \
} while(0)
#define YONA_SLAB_ADD(n)       atomic_fetch_add_explicit(&yona_slab_bytes, (long long)(n), memory_order_relaxed)

static void pool_grow(int cls) {
    size_t block_size = pool_sizes[cls];
    size_t slab_size = sizeof(slab_t) + SLAB_BLOCKS * block_size;
    slab_t* slab = (slab_t*)calloc(1, slab_size);
    slab->next = pool_slabs[cls];
    pool_slabs[cls] = slab;
    YONA_SLAB_ADD(slab_size);
    if (getenv("YONA_POOL_TRACE"))
        fprintf(stderr, "pool_grow cls=%d slab=%p data=%p-%p\n",
                cls, (void*)slab, (void*)slab->data,
                (void*)(slab->data + SLAB_BLOCKS * block_size));
    /* Link all blocks in the slab into the free list */
    for (int i = 0; i < SLAB_BLOCKS; i++) {
        pool_block_t* block = (pool_block_t*)(slab->data + i * block_size);
        block->next = pool_freelist[cls];
        pool_freelist[cls] = block;
    }
}

static void* pool_alloc(size_t total_bytes) {
    int cls = pool_class_for(total_bytes);
    if (cls < 0) return malloc(total_bytes);
    if (!pool_freelist[cls]) pool_grow(cls);
    pool_block_t* block = pool_freelist[cls];
    if (((uintptr_t)block & 7) != 0) {
        fprintf(stderr, "POOL_ALLOC: UNALIGNED head block=%p cls=%d\n", (void*)block, cls);
        fflush(stderr);
        abort();
    }
    // Check that next pointer (about to become new head) is also aligned
    pool_block_t* next = block->next;
    if (next && ((uintptr_t)next & 7) != 0) {
        fprintf(stderr, "POOL_ALLOC: popping block=%p with UNALIGNED next=%p cls=%d\n",
                (void*)block, (void*)next, cls);
        fflush(stderr);
        abort();
    }
    pool_freelist[cls] = next;
    return (void*)block;
}

static void pool_free(void* ptr, size_t total_bytes) {
    if (!ptr) return;
    int cls = pool_class_for(total_bytes);
    if (cls < 0) { free(ptr); return; }
    if (cls == 3 && getenv("YONA_POOL_TRACE"))
        fprintf(stderr, "pool_free cls=3 block=%p\n", ptr);
    pool_block_t* block = (pool_block_t*)ptr;
    block->next = pool_freelist[cls];
    pool_freelist[cls] = block;
}

/* Internal: allocate with RC header, returns pointer to payload.
 * Header: [refcount, type_tag_and_pool_class, ...payload...]
 *                                              ^-- returned pointer
 * Pool class is encoded in the upper bits of the type_tag word:
 *   bits 0-7:  type tag (RC_TYPE_SEQ, etc.)
 *   bits 8-15: pool class index + 1 (0 = not pooled, 1-4 = class 0-3)
 * This avoids a 3rd header word while supporting pool_free. */
#define RC_HEADER_SIZE 2

#define ENCODE_TAG(tag, cls) ((tag) | (((int64_t)(cls) + 1) << 8))
#define ENCODE_TAG_LEN(tag, cls, len) ((tag) | (((int64_t)(cls) + 1) << 8) | ((int64_t)(len) << 16))
#define DECODE_TAG(encoded) ((encoded) & 0xFF)
#define DECODE_POOL_CLASS(encoded) ((int)(((encoded) >> 8) & 0xFF) - 1)
#define DECODE_STRING_LEN(encoded) ((size_t)((encoded) >> 16))

void* rc_alloc(int64_t type_tag, size_t payload_bytes) {
    yona_alloc_report_maybe_register();
    size_t total = RC_HEADER_SIZE * sizeof(int64_t) + payload_bytes;
    int cls = pool_class_for(total);
    int64_t* raw = (int64_t*)pool_alloc(total);
    raw[0] = 1;         /* refcount = 1 */
    raw[1] = ENCODE_TAG(type_tag, cls);  /* type tag + pool class */
    YONA_ALLOC_INC_TAG((int)type_tag);
    return (void*)(raw + RC_HEADER_SIZE);
}

/* Public: increment refcount (atomic for thread safety) */
void yona_rt_rc_inc(void* ptr) {
    if (__builtin_expect(!ptr, 0)) return;
    int64_t* header = ((int64_t*)ptr) - RC_HEADER_SIZE;
    int64_t rc = __atomic_load_n(&header[0], __ATOMIC_RELAXED);
    if (__builtin_expect(rc == INT64_MAX, 0)) return;  /* arena/static sentinel */
    __atomic_fetch_add(&header[0], 1, __ATOMIC_RELAXED);
}

/* Sentinel refcount for arena-allocated objects. rc_dec skips these. */
#define RC_ARENA_SENTINEL INT64_MAX

/* Forward declarations for recursive RC types */
#define RC_TYPE_CHUNKED_FWD 11  /* legacy chunked seq (unused, kept for tag safety) */
#define RC_TYPE_RBT_FWD      12 /* RBT seq root struct */
#define RC_TYPE_RBT_NODE_FWD 13 /* RBT internal node (32 child pointers) */
#define RC_TYPE_RBT_LEAF_FWD 14 /* RBT leaf node (heap_flag + 32 elements) */

/* Public: decrement refcount; free when it reaches 0.
 * Recursively rc_dec pointer-typed children for known container types. */
void yona_rt_rc_dec(void* ptr) {
    if (__builtin_expect(!ptr, 0)) return;
    int64_t* header = ((int64_t*)ptr) - RC_HEADER_SIZE;
    int64_t rc = __atomic_load_n(&header[0], __ATOMIC_RELAXED);
    if (__builtin_expect(rc == RC_ARENA_SENTINEL, 0)) return;
    int64_t old = __atomic_fetch_sub(&header[0], 1, __ATOMIC_ACQ_REL);
    if (__builtin_expect(old <= 1, 0)) {
        int64_t encoded_tag = header[1];
        int64_t type_tag = DECODE_TAG(encoded_tag);
        int pool_cls = DECODE_POOL_CLASS(encoded_tag);
        int64_t* payload = (int64_t*)ptr;

        if (type_tag == RC_TYPE_SEQ) {
            /* Flat seq: rc_dec elements if heap_flag set.
             * Layout: [count, flags, elem0, ...]
             * flags: bits 0-31 = heap_flag, bits 32-63 = offset */
            int64_t count = payload[0];
            int64_t flags = payload[1];
            int heap_flag = (int)(flags & 0xFFFFFFFF);
            int offset = (int)((uint64_t)flags >> 32);
            if (heap_flag) {
                for (int64_t i = 0; i < count; i++) {
                    int64_t val = payload[2 + offset + i];
                    if (val) yona_rt_rc_dec((void*)(intptr_t)val);
                }
            }
        } else if (type_tag == RC_TYPE_CHUNKED_FWD) {
            /* Legacy chunked seq: rc_dec next chunk pointer */
            typedef struct { int64_t tl, off, cnt; int64_t e[32]; void* next; } chunk_layout_t;
            chunk_layout_t* chunk = (chunk_layout_t*)ptr;
            if (chunk->next) yona_rt_rc_dec(chunk->next);
        } else if (type_tag == RC_TYPE_RBT_FWD) {
            /* RBT seq root: rc_dec elements in head/tail buffers, head chain, trie.
             * Layout matches rbt_t in seq.c:
             *   [0] length, [1] heap_flag, [2] head_off, [3] head_cnt,
             *   [4..35] head_buf[32],
             *   [36] head_next (ptr), [37] head_chain_len,
             *   [38] tail_cnt, [39..70] tail_buf[32],
             *   [71] back_shift, [72] back_root (ptr),
             *   [73] back_size, [74] back_off */
            int64_t hf = payload[1];
            int64_t head_off = payload[2];
            int64_t head_cnt = payload[3];
            int64_t tail_cnt = payload[38];
            if (hf) {
                for (int64_t i = 0; i < head_cnt; i++) {
                    int64_t v = payload[4 + head_off + i];
                    if (v) yona_rt_rc_dec((void*)(intptr_t)v);
                }
                for (int64_t i = 0; i < tail_cnt; i++) {
                    int64_t v = payload[39 + i];
                    if (v) yona_rt_rc_dec((void*)(intptr_t)v);
                }
            }
            /* Walk head chain and rc_dec elements if heap-typed */
            void* head_next = *(void**)&payload[36];
            if (hf && head_next) {
                void* cur = head_next;
                while (cur) {
                    int64_t* cp = (int64_t*)cur;
                    int64_t coff = cp[0], ccnt = cp[1];
                    for (int64_t i = 0; i < ccnt; i++) {
                        int64_t v = cp[2 + coff + i];
                        if (v) yona_rt_rc_dec((void*)(intptr_t)v);
                    }
                    cur = *(void**)&cp[2 + 32];
                }
            }
            if (head_next) yona_rt_rc_dec(head_next);
            void* back_root = *(void**)&payload[72];
            if (back_root) yona_rt_rc_dec(back_root);
        } else if (type_tag == RC_TYPE_RBT_NODE_FWD) {
            /* RBT internal node: rc_dec all non-null children. */
            for (int i = 0; i < 32; i++) {
                int64_t child = payload[i];
                if (child) yona_rt_rc_dec((void*)(intptr_t)child);
            }
        } else if (type_tag == 15 /* RC_TYPE_RBT_CHUNK */) {
            /* RBT head chain chunk: [offset, count, elems[32], next_ptr]
             * rc_dec next chunk. Elements are rc_dec'd by the rbt_t destructor
             * when walking the chain (it knows the heap_flag). */
            /* Note: for simplicity, don't rc_dec elements here. The rbt_t
             * destructor walks the head chain and handles element cleanup.
             * But if a chunk is freed independently (e.g. after tail detaches
             * it), we must handle elements. We use a conservative approach:
             * always rc_dec the next pointer. */
            void* next = *(void**)&payload[2 + 32]; /* after offset, count, elems[32] */
            if (next) yona_rt_rc_dec(next);
        } else if (type_tag == RC_TYPE_RBT_LEAF_FWD) {
            /* RBT leaf node: rc_dec elements if heap_flag set.
             * Layout: [heap_flag, elem0, ..., elem31] */
            int64_t hf = payload[0];
            if (hf) {
                for (int i = 0; i < 32; i++) {
                    int64_t v = payload[1 + i];
                    if (v) yona_rt_rc_dec((void*)(intptr_t)v);
                }
            }
        } else if (type_tag == RC_TYPE_CLOSURE) {
            /* Closure: rc_dec heap-typed captures using the heap_mask.
             * Layout: [fn_ptr, ret_type, arity, num_captures, heap_mask, cap0, ...] */
            int64_t num_caps = payload[3];
            int64_t heap_mask = payload[4];
            for (int64_t ci = 0; ci < num_caps && ci < 64; ci++) {
                if (heap_mask & ((int64_t)1 << ci)) {
                    int64_t cap_val = payload[5 + ci];
                    if (cap_val) yona_rt_rc_dec((void*)(intptr_t)cap_val);
                }
            }
        } else if (type_tag == RC_TYPE_ADT) {
            /* ADT: rc_dec heap-typed fields using heap_mask.
             * Layout: [tag, num_fields, heap_mask, field0, ...] */
            int64_t num_fields = payload[1];
            int64_t heap_mask = payload[2];
            for (int64_t fi = 0; fi < num_fields && fi < 64; fi++) {
                if (heap_mask & ((int64_t)1 << fi)) {
                    int64_t field_val = payload[3 + fi];
                    if (field_val) yona_rt_rc_dec((void*)(intptr_t)field_val);
                }
            }
        } else if (type_tag == RC_TYPE_DICT) {
            /* HAMT: rc_dec heap keys/values (aux flags) and child sub-nodes. */
            yona_rt_hamt_destroy_children(ptr);
        } else if (type_tag == RC_TYPE_SET) {
            /* Set: rc_dec all elements if heap_flag is set.
             * Layout: [count, heap_flag, elem0, ...] */
            int64_t count = payload[0];
            int64_t heap_flag = payload[1];
            if (heap_flag) {
                for (int64_t i = 0; i < count; i++) {
                    int64_t val = payload[2 + i];
                    if (val) yona_rt_rc_dec((void*)(intptr_t)val);
                }
            }
        } else if (type_tag == 9 /* RC_TYPE_TUPLE */) {
            /* Tuple: rc_dec heap-typed elements using heap_mask.
             * Layout: [num_elements, heap_mask, elem0, ...] */
            int64_t num_elems = payload[0];
            int64_t heap_mask = payload[1];
            for (int64_t ei = 0; ei < num_elems && ei < 64; ei++) {
                if (heap_mask & ((int64_t)1 << ei)) {
                    int64_t elem_val = payload[2 + ei];
                    if (elem_val) yona_rt_rc_dec((void*)(intptr_t)elem_val);
                }
            }
        } else if (type_tag == 17 /* RC_TYPE_PROCESS */) {
            /* Process handle: close pipe fds, reap zombie.
             * yona_process_destroy is defined in os_linux.c. */
            extern void yona_process_destroy(void* proc) __attribute__((weak));
            if (yona_process_destroy) yona_process_destroy(ptr);
        } else if (type_tag == 16 /* RC_TYPE_REGEX */) {
            /* Regex handle: free the PCRE2 compiled pattern.
             * Layout: [pcre2_code* code]
             * yona_regex_free_code is a weak symbol — if regex.c isn't
             * linked (PCRE2 unavailable), this is a no-op. */
            void* code = *(void**)payload;
            if (code) {
                extern void yona_regex_free_code(void* code) __attribute__((weak));
                if (yona_regex_free_code)
                    yona_regex_free_code(code);
            }
        } else if (type_tag == 20 /* RC_TYPE_CHANNEL */) {
            /* Channel: signal waiters, destroy mutex/condvars, free buffer. */
            extern void yona_rt_channel_destroy(void* ch);
            yona_rt_channel_destroy(ptr);
        }
        YONA_FREE_INC_TAG((int)type_tag);
        if (pool_cls >= 0)
            pool_free(header, pool_sizes[pool_cls]);
        else
            free(header);
    }
}

/* ===== Arena Allocator ===== */
/*
 * Bump-allocated memory block freed in bulk. Non-escaping values use
 * this instead of malloc+RC for zero-overhead deallocation.
 *
 * Layout: [yona_arena_t header][...bump-allocated payloads...]
 * Each payload has the standard RC header but with refcount=SENTINEL.
 */

#define YONA_ARENA_DEFAULT_SIZE 4096

typedef struct yona_arena {
    char* base;
    char* cursor;
    char* end;
    struct yona_arena* next;  /* overflow chain */
} yona_arena_t;

void* yona_rt_arena_create(int64_t size) {
    yona_alloc_report_maybe_register();
    if (size <= 0) size = YONA_ARENA_DEFAULT_SIZE;
    yona_arena_t* arena = (yona_arena_t*)malloc(sizeof(yona_arena_t) + size);
    arena->base = (char*)(arena + 1);
    arena->cursor = arena->base;
    arena->end = arena->base + size;
    arena->next = NULL;
    return arena;
}

void* yona_rt_arena_alloc(void* arena_ptr, int64_t type_tag, int64_t payload_bytes) {
    yona_arena_t* arena = (yona_arena_t*)arena_ptr;
    size_t total = RC_HEADER_SIZE * sizeof(int64_t) + (size_t)payload_bytes;
    /* Align to 8 bytes */
    total = (total + 7) & ~7;

    /* Find an arena block with enough space */
    while (arena->cursor + total > arena->end) {
        if (!arena->next) {
            /* Allocate overflow block (at least total or default size) */
            int64_t new_size = total > YONA_ARENA_DEFAULT_SIZE ? (int64_t)total * 2 : YONA_ARENA_DEFAULT_SIZE;
            arena->next = (yona_arena_t*)yona_rt_arena_create(new_size);
        }
        arena = arena->next;
    }

    int64_t* raw = (int64_t*)arena->cursor;
    arena->cursor += total;
    raw[0] = RC_ARENA_SENTINEL;  /* sentinel: rc_dec will skip */
    raw[1] = type_tag;
    return (void*)(raw + RC_HEADER_SIZE);  /* return pointer past header */
}

void yona_rt_arena_destroy(void* arena_ptr) {
    yona_arena_t* arena = (yona_arena_t*)arena_ptr;
    while (arena) {
        yona_arena_t* next = arena->next;
        free(arena);
        arena = next;
    }
}

#define RC_TYPE_BOX     7
#define RC_TYPE_BYTE_ARRAY   8
/* ===== Persistent Seq ===== */
#include "runtime/seq.c"

/* ===== Bytes — length-prefixed byte buffer ===== */
/*
 * Layout: [rc_header][length: i64][byte0, byte1, ...]
 *                                  ^-- returned pointer
 * Unlike strings, Bytes can contain \0 and arbitrary binary data.
 * Length is stored at ptr[-1] (the i64 before the data pointer... no,
 * actually we store length at index 0 of the payload, same as SEQ).
 *
 * Actual layout: rc_alloc returns payload pointer.
 *   payload[0] = length (as i64)
 *   payload[1..] = bytes (packed as uint8_t, but stored after the i64 length)
 *
 * We use the same pattern as sequences: first i64 is length, then data.
 */

/* Allocate a Bytes buffer of the given size (uninitialized) */
void* yona_rt_byte_array_alloc(int64_t size) {
    int64_t* buf = (int64_t*)rc_alloc(RC_TYPE_BYTE_ARRAY, sizeof(int64_t) + (size_t)size);
    buf[0] = size;
    return buf;
}

/* Get length of a Bytes buffer */
int64_t yona_rt_byte_array_length(void* bytes) {
    return ((int64_t*)bytes)[0];
}

/* Get byte at index (returns 0-255) */
int64_t yona_rt_byte_array_get(void* bytes, int64_t index) {
    int64_t len = ((int64_t*)bytes)[0];
    if (index < 0 || index >= len) return 0;
    uint8_t* data = (uint8_t*)((int64_t*)bytes + 1);
    return (int64_t)data[index];
}

/* Set byte at index */
void yona_rt_byte_array_set(void* bytes, int64_t index, int64_t value) {
    int64_t len = ((int64_t*)bytes)[0];
    if (index < 0 || index >= len) return;
    uint8_t* data = (uint8_t*)((int64_t*)bytes + 1);
    data[index] = (uint8_t)(value & 0xFF);
}

/* Concatenate two Bytes buffers */
void* yona_rt_byte_array_concat(void* a, void* b) {
    int64_t len_a = ((int64_t*)a)[0];
    int64_t len_b = ((int64_t*)b)[0];
    int64_t* result = (int64_t*)rc_alloc(RC_TYPE_BYTE_ARRAY, sizeof(int64_t) + (size_t)(len_a + len_b));
    result[0] = len_a + len_b;
    uint8_t* dest = (uint8_t*)(result + 1);
    memcpy(dest, (uint8_t*)((int64_t*)a + 1), (size_t)len_a);
    memcpy(dest + len_a, (uint8_t*)((int64_t*)b + 1), (size_t)len_b);
    return result;
}

/* Slice: bytes[start..start+len] */
void* yona_rt_byte_array_slice(void* bytes, int64_t start, int64_t len) {
    int64_t total = ((int64_t*)bytes)[0];
    if (start < 0) start = 0;
    if (start + len > total) len = total - start;
    if (len <= 0) return yona_rt_byte_array_alloc(0);
    int64_t* result = (int64_t*)rc_alloc(RC_TYPE_BYTE_ARRAY, sizeof(int64_t) + (size_t)len);
    result[0] = len;
    uint8_t* src = (uint8_t*)((int64_t*)bytes + 1) + start;
    memcpy((uint8_t*)(result + 1), src, (size_t)len);
    return result;
}

/* Convert String to Bytes (copies, no null terminator in output) */
void* yona_rt_byte_array_from_string(const char* s) {
    size_t len = strlen(s);
    int64_t* buf = (int64_t*)rc_alloc(RC_TYPE_BYTE_ARRAY, sizeof(int64_t) + len);
    buf[0] = (int64_t)len;
    memcpy((uint8_t*)(buf + 1), s, len);
    return buf;
}

/* Convert Bytes to String (adds null terminator) */
const char* yona_rt_byte_array_to_string(void* bytes) {
    int64_t len = ((int64_t*)bytes)[0];
    char* s = (char*)rc_alloc(RC_TYPE_STRING, (size_t)len + 1);
    memcpy(s, (uint8_t*)((int64_t*)bytes + 1), (size_t)len);
    s[len] = '\0';
    return s;
}

/* Create Bytes from a list of integers (each 0-255) */
void* yona_rt_byte_array_from_seq(int64_t* seq) {
    int64_t len = seq[0];
    int64_t* buf = (int64_t*)rc_alloc(RC_TYPE_BYTE_ARRAY, sizeof(int64_t) + (size_t)len);
    buf[0] = len;
    uint8_t* data = (uint8_t*)(buf + 1);
    for (int64_t i = 0; i < len; i++)
        data[i] = (uint8_t)(seq[i + 1] & 0xFF);
    return buf;
}

/* Convert Bytes to a list of integers (each 0-255) */
int64_t* yona_rt_byte_array_to_seq(void* bytes) {
    int64_t len = ((int64_t*)bytes)[0];
    int64_t* seq = yona_rt_seq_alloc(len);
    uint8_t* data = (uint8_t*)((int64_t*)bytes + 1);
    for (int64_t i = 0; i < len; i++)
        seq[i + 1] = (int64_t)data[i];
    return seq;
}

/* Print Bytes as hex for debugging */
void yona_rt_print_byte_array(void* bytes) {
    int64_t len = ((int64_t*)bytes)[0];
    uint8_t* data = (uint8_t*)((int64_t*)bytes + 1);
    printf("<<");
    for (int64_t i = 0; i < len; i++) {
        if (i > 0) printf(", ");
        printf("%d", data[i]);
    }
    printf(">>");
}

/* Std\Bytes module aliases */
void* yona_Std_ByteArray__alloc(int64_t s)          { return yona_rt_byte_array_alloc(s); }
int64_t yona_Std_ByteArray__length(void* b)         { return yona_rt_byte_array_length(b); }
int64_t yona_Std_ByteArray__get(void* b, int64_t i) { return yona_rt_byte_array_get(b, i); }
void yona_Std_ByteArray__set(void* b, int64_t i, int64_t v) { yona_rt_byte_array_set(b, i, v); }
void* yona_Std_ByteArray__concat(void* a, void* b)  { return yona_rt_byte_array_concat(a, b); }
void* yona_Std_ByteArray__slice(void* b, int64_t s, int64_t l) { return yona_rt_byte_array_slice(b, s, l); }
void* yona_Std_ByteArray__fromString(const char* s) { return yona_rt_byte_array_from_string(s); }
const char* yona_Std_ByteArray__toString(void* b)   { return yona_rt_byte_array_to_string(b); }
void* yona_Std_ByteArray__fromSeq(int64_t* s)       { return yona_rt_byte_array_from_seq(s); }
int64_t* yona_Std_ByteArray__toSeq(void* b)         { return yona_rt_byte_array_to_seq(b); }
int64_t yona_Std_ByteArray__head(void* b)            { return yona_rt_byte_array_get(b, 0); }
void* yona_Std_ByteArray__tail(void* b) {
    int64_t len = yona_rt_byte_array_length(b);
    return (len <= 1) ? yona_rt_byte_array_alloc(0) : yona_rt_byte_array_slice(b, 1, len - 1);
}
void* yona_Std_ByteArray__join(void* a, void* b)    { return yona_rt_byte_array_concat(a, b); }
int64_t yona_Std_ByteArray__foldl(int64_t* fn, int64_t acc, void* b) {
    int64_t len = yona_rt_byte_array_length(b);
    typedef int64_t (*fold_fn_t)(int64_t*, int64_t, int64_t);
    fold_fn_t f = (fold_fn_t)(intptr_t)fn[0];
    uint8_t* data = (uint8_t*)((int64_t*)b + 1);
    for (int64_t i = 0; i < len; i++)
        acc = f(fn, acc, (int64_t)data[i]);
    return acc;
}
void* yona_Std_ByteArray__map(int64_t* fn, void* b) {
    int64_t len = yona_rt_byte_array_length(b);
    void* result = yona_rt_byte_array_alloc(len);
    typedef int64_t (*map_fn_t)(int64_t*, int64_t);
    map_fn_t f = (map_fn_t)(intptr_t)fn[0];
    uint8_t* src = (uint8_t*)((int64_t*)b + 1);
    uint8_t* dst = (uint8_t*)((int64_t*)result + 1);
    for (int64_t i = 0; i < len; i++)
        dst[i] = (uint8_t)f(fn, (int64_t)src[i]);
    return result;
}

/* ===== IntArray — contiguous unboxed int64_t[] ===== */
/* Layout: [count: i64][elem0, elem1, ...] — no per-element RC. */

int64_t* yona_rt_int_array_alloc(int64_t count) {
    int64_t* buf = (int64_t*)rc_alloc(RC_TYPE_INT_ARRAY,
        sizeof(int64_t) + (size_t)count * sizeof(int64_t));
    buf[0] = count;
    return buf;
}

int64_t yona_rt_int_array_length(int64_t* arr) { return arr[0]; }
int64_t yona_rt_int_array_get(int64_t* arr, int64_t i) { return arr[1 + i]; }

void yona_rt_int_array_set(int64_t* arr, int64_t i, int64_t v) {
    arr[1 + i] = v;
}

int64_t yona_rt_int_array_head(int64_t* arr) { return arr[1]; }

/* tail: returns a slice view (new array with offset data). O(n) copy for now. */
int64_t* yona_rt_int_array_tail(int64_t* arr) {
    int64_t len = arr[0];
    if (len <= 1) return yona_rt_int_array_alloc(0);
    int64_t new_len = len - 1;
    int64_t* result = yona_rt_int_array_alloc(new_len);
    memcpy(result + 1, arr + 2, (size_t)new_len * sizeof(int64_t));
    return result;
}

int64_t* yona_rt_int_array_cons(int64_t elem, int64_t* arr) {
    int64_t len = arr[0];
    int64_t* result = yona_rt_int_array_alloc(len + 1);
    result[1] = elem;
    memcpy(result + 2, arr + 1, (size_t)len * sizeof(int64_t));
    return result;
}

int64_t* yona_rt_int_array_join(int64_t* a, int64_t* b) {
    int64_t la = a[0], lb = b[0];
    int64_t* result = yona_rt_int_array_alloc(la + lb);
    memcpy(result + 1, a + 1, (size_t)la * sizeof(int64_t));
    memcpy(result + 1 + la, b + 1, (size_t)lb * sizeof(int64_t));
    return result;
}

int64_t* yona_rt_int_array_slice(int64_t* arr, int64_t start, int64_t len) {
    int64_t* result = yona_rt_int_array_alloc(len);
    memcpy(result + 1, arr + 1 + start, (size_t)len * sizeof(int64_t));
    return result;
}

int64_t* yona_rt_int_array_map(int64_t* fn, int64_t* arr) {
    int64_t len = arr[0];
    int64_t* result = yona_rt_int_array_alloc(len);
    typedef int64_t (*map_fn_t)(int64_t*, int64_t);
    map_fn_t f = (map_fn_t)(intptr_t)fn[0];
    for (int64_t i = 0; i < len; i++)
        result[1 + i] = f(fn, arr[1 + i]);
    return result;
}

int64_t yona_rt_int_array_foldl(int64_t* fn, int64_t acc, int64_t* arr) {
    int64_t len = arr[0];
    typedef int64_t (*fold_fn_t)(int64_t*, int64_t, int64_t);
    fold_fn_t f = (fold_fn_t)(intptr_t)fn[0];
    for (int64_t i = 0; i < len; i++)
        acc = f(fn, acc, arr[1 + i]);
    return acc;
}

int64_t* yona_rt_int_array_filter(int64_t* fn, int64_t* arr) {
    int64_t len = arr[0];
    typedef int64_t (*pred_fn_t)(int64_t*, int64_t);
    pred_fn_t p = (pred_fn_t)(intptr_t)fn[0];
    /* Two-pass: count matches, then fill */
    int64_t count = 0;
    for (int64_t i = 0; i < len; i++)
        if (p(fn, arr[1 + i])) count++;
    int64_t* result = yona_rt_int_array_alloc(count);
    int64_t j = 0;
    for (int64_t i = 0; i < len; i++)
        if (p(fn, arr[1 + i])) result[1 + j++] = arr[1 + i];
    return result;
}

int64_t* yona_rt_int_array_from_seq(int64_t* seq) {
    int64_t len = seq[0];
    int64_t* result = yona_rt_int_array_alloc(len);
    if (!is_rbt(seq)) {
        /* Flat seq: single memcpy from the data region. */
        memcpy(result + 1, seq + SEQ_HDR_SIZE + FLAT_OFF(seq),
               (size_t)len * sizeof(int64_t));
    } else {
        /* RBT: fall back to per-element access. */
        for (int64_t i = 0; i < len; i++)
            result[1 + i] = yona_rt_seq_get(seq, i);
    }
    return result;
}

int64_t* yona_rt_int_array_to_seq(int64_t* arr) {
    extern int64_t* yona_rt_seq_alloc(int64_t count);
    int64_t len = arr[0];
    int64_t* seq = yona_rt_seq_alloc(len);
    memcpy(seq + SEQ_HDR_SIZE, arr + 1, (size_t)len * sizeof(int64_t));
    return seq;
}

void yona_rt_print_int_array(int64_t* arr) {
    int64_t len = arr[0];
    printf("IntArray[");
    for (int64_t i = 0; i < len; i++) {
        if (i > 0) printf(", ");
        printf("%" PRId64, arr[1 + i]);
    }
    printf("]");
}

/* Std\IntArray wrappers */
int64_t* yona_Std_IntArray__alloc(int64_t n) { return yona_rt_int_array_alloc(n); }
int64_t* yona_Std_IntArray__fill(int64_t n, int64_t v) {
    int64_t* arr = yona_rt_int_array_alloc(n);
    for (int64_t i = 0; i < n; i++) arr[1 + i] = v;
    return arr;
}
int64_t  yona_Std_IntArray__length(int64_t* a) { return yona_rt_int_array_length(a); }
int64_t  yona_Std_IntArray__get(int64_t* a, int64_t i) { return yona_rt_int_array_get(a, i); }
int64_t* yona_Std_IntArray__set(int64_t* a, int64_t i, int64_t v) {
    /* Persistent: copy-on-write */
    int64_t len = a[0];
    int64_t* result = yona_rt_int_array_alloc(len);
    memcpy(result + 1, a + 1, (size_t)len * sizeof(int64_t));
    result[1 + i] = v;
    return result;
}
int64_t  yona_Std_IntArray__head(int64_t* a) { return yona_rt_int_array_head(a); }
int64_t* yona_Std_IntArray__tail(int64_t* a) { return yona_rt_int_array_tail(a); }
int64_t* yona_Std_IntArray__cons(int64_t e, int64_t* a) { return yona_rt_int_array_cons(e, a); }
int64_t* yona_Std_IntArray__join(int64_t* a, int64_t* b) { return yona_rt_int_array_join(a, b); }
int64_t* yona_Std_IntArray__slice(int64_t* a, int64_t s, int64_t l) { return yona_rt_int_array_slice(a, s, l); }
int64_t* yona_Std_IntArray__map(int64_t* fn, int64_t* a) { return yona_rt_int_array_map(fn, a); }
int64_t  yona_Std_IntArray__foldl(int64_t* fn, int64_t acc, int64_t* a) { return yona_rt_int_array_foldl(fn, acc, a); }
int64_t* yona_Std_IntArray__filter(int64_t* fn, int64_t* a) { return yona_rt_int_array_filter(fn, a); }
int64_t* yona_Std_IntArray__fromSeq(int64_t* s) { return yona_rt_int_array_from_seq(s); }
int64_t* yona_Std_IntArray__toSeq(int64_t* a) { return yona_rt_int_array_to_seq(a); }

/* Accelerated columnar backends used by Std\GPU. */
#include "runtime/gpu_vulkan.c"
#include "runtime/gpu_cpu.c"
/* gpu_stub uses linked Vulkan entry points (vk* prototypes). gpu_vulkan_device.c
 * pulls vulkan.h with VK_NO_PROTOTYPES for dynamic loading; include stub first
 * so Khronos include guards do not swallow the prototype pass. */
#include "runtime/gpu_stub.c"
#include "runtime/gpu_vulkan_device.c"

/* ===== FloatArray — contiguous unboxed double[] ===== */
/* Layout: [count: i64][double0, double1, ...] */

double* yona_rt_float_array_alloc(int64_t count) {
    /* Store count as i64 before the double data */
    int64_t* buf = (int64_t*)rc_alloc(RC_TYPE_FLOAT_ARRAY,
        sizeof(int64_t) + (size_t)count * sizeof(double));
    buf[0] = count;
    return (double*)(buf + 1); /* return pointer to first double */
}

/* Access count from the i64 before the double data */
static int64_t* float_array_header(double* arr) { return ((int64_t*)arr) - 1; }

int64_t yona_rt_float_array_length(double* arr) { return float_array_header(arr)[0]; }
double  yona_rt_float_array_get(double* arr, int64_t i) { return arr[i]; }
void    yona_rt_float_array_set(double* arr, int64_t i, double v) { arr[i] = v; }
double  yona_rt_float_array_head(double* arr) { return arr[0]; }

double* yona_rt_float_array_tail(double* arr) {
    int64_t len = yona_rt_float_array_length(arr);
    if (len <= 1) return yona_rt_float_array_alloc(0);
    int64_t new_len = len - 1;
    double* result = yona_rt_float_array_alloc(new_len);
    memcpy(result, arr + 1, (size_t)new_len * sizeof(double));
    return result;
}

double* yona_rt_float_array_cons(double elem, double* arr) {
    int64_t len = yona_rt_float_array_length(arr);
    double* result = yona_rt_float_array_alloc(len + 1);
    result[0] = elem;
    memcpy(result + 1, arr, (size_t)len * sizeof(double));
    return result;
}

double* yona_rt_float_array_join(double* a, double* b) {
    int64_t la = yona_rt_float_array_length(a);
    int64_t lb = yona_rt_float_array_length(b);
    double* result = yona_rt_float_array_alloc(la + lb);
    memcpy(result, a, (size_t)la * sizeof(double));
    memcpy(result + la, b, (size_t)lb * sizeof(double));
    return result;
}

double* yona_rt_float_array_map(int64_t* fn, double* arr) {
    int64_t len = yona_rt_float_array_length(arr);
    double* result = yona_rt_float_array_alloc(len);
    typedef double (*map_fn_t)(int64_t*, double);
    map_fn_t f = (map_fn_t)(intptr_t)fn[0];
    for (int64_t i = 0; i < len; i++)
        result[i] = f(fn, arr[i]);
    return result;
}

double yona_rt_float_array_foldl(int64_t* fn, double acc, double* arr) {
    int64_t len = yona_rt_float_array_length(arr);
    typedef double (*fold_fn_t)(int64_t*, double, double);
    fold_fn_t f = (fold_fn_t)(intptr_t)fn[0];
    for (int64_t i = 0; i < len; i++)
        acc = f(fn, acc, arr[i]);
    return acc;
}

void yona_rt_print_float_array(double* arr) {
    int64_t len = yona_rt_float_array_length(arr);
    printf("FloatArray[");
    for (int64_t i = 0; i < len; i++) {
        if (i > 0) printf(", ");
        printf("%g", arr[i]);
    }
    printf("]");
}

/* Std\FloatArray wrappers */
double* yona_Std_FloatArray__alloc(int64_t n) { return yona_rt_float_array_alloc(n); }
double* yona_Std_FloatArray__fill(int64_t n, double v) {
    double* arr = yona_rt_float_array_alloc(n);
    for (int64_t i = 0; i < n; i++) arr[i] = v;
    return arr;
}
int64_t yona_Std_FloatArray__length(double* a) { return yona_rt_float_array_length(a); }
double  yona_Std_FloatArray__get(double* a, int64_t i) { return yona_rt_float_array_get(a, i); }
double* yona_Std_FloatArray__set(double* a, int64_t i, double v) {
    int64_t len = yona_rt_float_array_length(a);
    double* result = yona_rt_float_array_alloc(len);
    memcpy(result, a, (size_t)len * sizeof(double));
    result[i] = v;
    return result;
}
double  yona_Std_FloatArray__head(double* a) { return yona_rt_float_array_head(a); }
double* yona_Std_FloatArray__tail(double* a) { return yona_rt_float_array_tail(a); }
double* yona_Std_FloatArray__cons(double e, double* a) { return yona_rt_float_array_cons(e, a); }
double* yona_Std_FloatArray__join(double* a, double* b) { return yona_rt_float_array_join(a, b); }
double* yona_Std_FloatArray__map(int64_t* fn, double* a) { return yona_rt_float_array_map(fn, a); }
double  yona_Std_FloatArray__foldl(int64_t* fn, double acc, double* a) { return yona_rt_float_array_foldl(fn, acc, a); }

/* Box: heap-allocate arbitrary data (for tuples in collections) */
void* yona_rt_box(const void* data, int64_t size) {
    void* box = rc_alloc(RC_TYPE_BOX, (size_t)size);
    memcpy(box, data, (size_t)size);
    return box;
}

/* Tuple: heap-allocate i64 array with element metadata.
 * Layout: [num_elements, heap_mask, elem0, elem1, ...]
 * Uses RC_TYPE_TUPLE (9) for recursive destruction. */
#define RC_TYPE_TUPLE 9

void* yona_rt_tuple_alloc(int64_t num_elements) {
    int64_t* tuple = (int64_t*)rc_alloc(RC_TYPE_TUPLE, (2 + num_elements) * sizeof(int64_t));
    tuple[0] = num_elements;
    tuple[1] = 0; /* heap_mask */
    return tuple;
}

void yona_rt_tuple_set_heap_mask(void* tuple, int64_t mask) {
    ((int64_t*)tuple)[1] = mask;
}

void yona_rt_tuple_set(void* tuple, int64_t index, int64_t value) {
    ((int64_t*)tuple)[2 + index] = value;
}

int64_t yona_rt_tuple_get(void* tuple, int64_t index) {
    return ((int64_t*)tuple)[2 + index];
}

/* Unbox: just returns the pointer (data is at the payload position) */
/* No runtime function needed — codegen does inttoptr + load directly */

/* Public: allocate an RC-managed string buffer (for platform layer) */
void* yona_rt_rc_alloc_string(size_t bytes) {
    return rc_alloc(RC_TYPE_STRING, bytes);
}

/* Public: allocate an RC-managed string with known length.
 * Length is encoded in bits 16-63 of the type_tag word for O(1) retrieval. */
void* yona_rt_rc_alloc_string_len(size_t bytes, size_t str_len) {
    size_t total = RC_HEADER_SIZE * sizeof(int64_t) + bytes;
    int cls = pool_class_for(total);
    int64_t* raw = (int64_t*)pool_alloc(total);
    raw[0] = 1;
    raw[1] = ENCODE_TAG_LEN(RC_TYPE_STRING, cls, str_len);
    return (void*)(raw + RC_HEADER_SIZE);
}

/* O(1) string length: reads stored length from RC header, falls back to strlen. */
int64_t yona_rt_string_length_fast(const char* str) {
    if (__builtin_expect(!str, 0)) return 0;
    int64_t* header = ((int64_t*)str) - RC_HEADER_SIZE;
    /* Check if this is an RC-managed string (refcount > 0 and reasonable) */
    int64_t rc = header[0];
    if (__builtin_expect(rc > 0 && rc < 1000000, 1)) {
        size_t len = DECODE_STRING_LEN(header[1]);
        if (__builtin_expect(len > 0, 1)) return (int64_t)len;
    }
    return (int64_t)strlen(str);
}


void yona_rt_print_int(int64_t value) {
    printf("%" PRId64, value);
}

void yona_rt_print_float(double value) {
    printf("%g", value);
}

void yona_rt_print_string(const char* value) {
    printf("%s", value);
}

void yona_rt_print_bool(int value) {
    printf("%s", value ? "true" : "false");
}

void yona_rt_print_newline(void) {
    printf("\n");
}

char* yona_rt_string_concat(const char* a, const char* b) {
    size_t len_a = (size_t)yona_rt_string_length_fast(a);
    size_t len_b = (size_t)yona_rt_string_length_fast(b);
    size_t total = len_a + len_b;
    char* result = (char*)yona_rt_rc_alloc_string_len(total + 1, total);
    memcpy(result, a, len_a);
    memcpy(result + len_a, b, len_b + 1);
    return result;
}

/* ===== Sequence (list) runtime ===== */

/* Seq functions are in runtime/seq.c (included above) */

/* ===== Symbol runtime ===== */
/* Symbols are interned to i64 IDs at compile time. Comparison is icmp eq. */
/* Print takes the string name (resolved by the compiler from the symbol table). */

void yona_rt_print_symbol(const char* name) {
    printf(":%s", name);
}

/* ===== Set runtime — legacy flat array ===== */
/* Flat set layout: [count, heap_flag, elem0, elem1, ...]
 * Used only during literal construction. HAMT-based operations below. */

int64_t* yona_rt_set_alloc(int64_t count) {
    int64_t* set = (int64_t*)rc_alloc(RC_TYPE_SET, (count + 2) * sizeof(int64_t));
    set[0] = count;
    set[1] = 0;
    return set;
}

void yona_rt_set_set_heap(int64_t* set, int64_t flag) {
    if (!set) return;
    int64_t* header = set - RC_HEADER_SIZE;
    int64_t tag = DECODE_TAG(header[1]);
    if (tag == RC_TYPE_DICT) {
        if (flag)
            yona_rt_hamt_stamp_aux_flags(set, HAMT_FLAG_KEY_HEAP | HAMT_FLAG_IS_SET);
        return;
    }
    set[1] = flag;
}

void yona_rt_set_put(int64_t* set, int64_t index, int64_t value) {
    set[index + 2] = value;
}

/* ===== Dict runtime (HAMT-based) ===== */
/* Persistent hash map using Hash Array Mapped Trie.
 * O(1) amortized lookup/insert, structural sharing for immutability. */
#include "runtime/hamt.c"

/* Legacy API wrappers — codegen uses these names.
 * dict_alloc creates empty, dict_set does persistent put (returns new dict). */

int64_t* yona_rt_dict_alloc(int64_t count) {
    (void)count;
    return (int64_t*)yona_rt_hamt_empty();
}

void yona_rt_dict_set_heap(int64_t* dict, int64_t key_heap, int64_t val_heap) {
    if (!dict) return;
    int64_t flags = 0;
    if (key_heap)
        flags |= HAMT_FLAG_KEY_HEAP;
    if (val_heap)
        flags |= HAMT_FLAG_VAL_HEAP;
    if (flags)
        yona_rt_hamt_stamp_aux_flags(dict, flags);
}

/* Persistent insert: returns NEW dict. Old dict is unchanged.
 * Handles empty set {} (RC_TYPE_SET) gracefully — creates fresh HAMT. */
/* Callee-owns — see yona_rt_set_insert for the rationale. */
int64_t* yona_rt_dict_put(int64_t* dict, int64_t key, int64_t value) {
    int64_t* orig = dict;
    int converted_empty = 0;
    /* Check if the input is actually a SET (empty {} parsed as SetExpr) */
    if (dict) {
        int64_t* header = dict - RC_HEADER_SIZE;
        int64_t tag = DECODE_TAG(header[1]);
        if (tag == RC_TYPE_SET) {
            /* Empty set — treat as empty dict */
            dict = (int64_t*)yona_rt_hamt_empty();
            converted_empty = 1;
        }
    }
    int64_t* result = (int64_t*)yona_rt_hamt_put((hamt_node_t*)dict, key, value);
    /* Consume: if the hamt_put path-copied, drop the intermediate HAMT
     * input. (In the converted_empty case, this is the temporary empty
     * HAMT we just created; in the direct HAMT case, it's the caller's
     * input that we now own.) */
    if (dict && result != dict)
        yona_rt_rc_dec(dict);
    /* Consume: if we converted an empty flat-set, drop the original. */
    if (converted_empty)
        yona_rt_rc_dec(orig);
    return result;
}

/* Lookup: returns value for key, or default_val if not found. */
int64_t yona_rt_dict_get(int64_t* dict, int64_t key, int64_t default_val) {
    return yona_rt_hamt_get((hamt_node_t*)dict, key, default_val);
}

int64_t yona_rt_dict_size(int64_t* dict) {
    return yona_rt_hamt_size((hamt_node_t*)dict);
}

int64_t yona_rt_dict_contains(int64_t* dict, int64_t key) {
    return yona_rt_hamt_contains((hamt_node_t*)dict, key);
}

int64_t* yona_rt_dict_keys(int64_t* dict) {
    return yona_rt_hamt_keys((hamt_node_t*)dict);
}

/* Legacy dict_set — for codegen compatibility. Mutates in place (only safe
 * during construction when refcount=1). NOT persistent. */
void yona_rt_dict_set(int64_t* dict, int64_t index, int64_t key, int64_t value) {
    /* For HAMT, we can't mutate. This is only called during dict literal
     * construction, so we use the legacy flat path. The codegen should be
     * updated to use dict_put for persistent inserts. For now, this is a
     * compatibility shim that puts entries into the HAMT. */
    (void)index; /* index not meaningful for HAMT */
    /* This doesn't work with HAMT — codegen must use dict_put instead. */
}

void yona_rt_print_dict(int64_t* dict) {
    yona_rt_hamt_print((hamt_node_t*)dict);
}

/* ===== Set runtime — HAMT-based persistent operations ===== */
/* Uses the same HAMT as dicts. Elements stored as keys with value=1. */

static hamt_node_t* set_ensure_hamt(int64_t* set) {
    if (!set) {
        hamt_node_t* empty = yona_rt_hamt_empty();
        hamt_or_aux_flags(empty, HAMT_FLAG_IS_SET);
        return empty;
    }
    int64_t* header = set - RC_HEADER_SIZE;
    int64_t tag = DECODE_TAG(header[1]);
    if (tag == RC_TYPE_DICT) return (hamt_node_t*)set;
    hamt_node_t* h = yona_rt_hamt_empty();
    int64_t flags = HAMT_FLAG_IS_SET;
    if (set[1]) flags |= HAMT_FLAG_KEY_HEAP;
    hamt_or_aux_flags(h, flags);
    int64_t count = set[0];
    for (int64_t i = 0; i < count; i++)
        h = yona_rt_hamt_put(h, set[i + 2], 1);
    return h;
}

/* Callee-owns: consumes `set`. If path-copy produces a new HAMT, the old
 * input is rc_dec'd. If mutated in place, returns same pointer (caller
 * will drop via its own bookkeeping). The codegen marks Set/Dict args
 * passed to this function as transferred so it doesn't double-drop. */
int64_t* yona_rt_set_insert(int64_t* set, int64_t elem) {
    int64_t* hamt_in;
    int converted_flat = 0;
    if (set) {
        int64_t* header = set - RC_HEADER_SIZE;
        int64_t tag = DECODE_TAG(header[1]);
        if (tag == RC_TYPE_DICT) {
            hamt_in = set;
        } else {
            /* Flat → HAMT: rebuild, consume old flat set. */
            hamt_node_t* h = yona_rt_hamt_empty();
            int64_t flags = HAMT_FLAG_IS_SET;
            if (set[1]) flags |= HAMT_FLAG_KEY_HEAP;
            hamt_or_aux_flags(h, flags);
            int64_t count = set[0];
            for (int64_t i = 0; i < count; i++)
                h = yona_rt_hamt_put(h, set[i + 2], 1);
            hamt_in = (int64_t*)h;
            converted_flat = 1;
        }
    } else {
        hamt_in = NULL;
    }
    int64_t* result = (int64_t*)yona_rt_hamt_put((hamt_node_t*)hamt_in, elem, 1);
    hamt_or_aux_flags((hamt_node_t*)result, HAMT_FLAG_IS_SET);
    /* Consume input: if path-copy happened (result differs from hamt_in
     * which was the caller's input after ensure_hamt), drop the old. */
    if (hamt_in && result != hamt_in)
        yona_rt_rc_dec(hamt_in);
    /* Consume the original flat set if we converted it. */
    if (converted_flat)
        yona_rt_rc_dec(set);
    return result;
}

int64_t yona_rt_set_contains(int64_t* set, int64_t elem) {
    if (!set) return 0;
    int64_t* header = set - RC_HEADER_SIZE;
    int64_t tag = DECODE_TAG(header[1]);
    if (tag == RC_TYPE_DICT) return yona_rt_hamt_contains((hamt_node_t*)set, elem);
    int64_t count = set[0];
    for (int64_t i = 0; i < count; i++)
        if (set[i + 2] == elem) return 1;
    return 0;
}

int64_t yona_rt_set_size(int64_t* set) {
    if (!set) return 0;
    int64_t* header = set - RC_HEADER_SIZE;
    int64_t tag = DECODE_TAG(header[1]);
    if (tag == RC_TYPE_DICT) return yona_rt_hamt_size((hamt_node_t*)set);
    return set[0];
}

int64_t* yona_rt_set_elements(int64_t* set) {
    if (!set) return yona_rt_seq_alloc(0);
    int64_t* header = set - RC_HEADER_SIZE;
    int64_t tag = DECODE_TAG(header[1]);
    if (tag == RC_TYPE_DICT) return yona_rt_hamt_keys((hamt_node_t*)set);
    int64_t count = set[0];
    int64_t* seq = yona_rt_seq_alloc(count);
    for (int64_t i = 0; i < count; i++) seq[2 + i] = set[2 + i];
    return seq;
}

int64_t* yona_rt_set_union(int64_t* a, int64_t* b) {
    hamt_node_t* ha = set_ensure_hamt(a);
    int64_t* be = yona_rt_set_elements(b);
    for (int64_t i = 0; i < be[0]; i++)
        ha = yona_rt_hamt_put(ha, be[2 + i], 1);
    return (int64_t*)ha;
}

int64_t* yona_rt_set_intersection(int64_t* a, int64_t* b) {
    hamt_node_t* r = yona_rt_hamt_empty();
    hamt_or_aux_flags(r, HAMT_FLAG_IS_SET);
    int64_t* ae = yona_rt_set_elements(a);
    for (int64_t i = 0; i < ae[0]; i++) {
        int64_t e = ae[2 + i];
        if (yona_rt_set_contains(b, e)) r = yona_rt_hamt_put(r, e, 1);
    }
    return (int64_t*)r;
}

int64_t* yona_rt_set_difference(int64_t* a, int64_t* b) {
    hamt_node_t* r = yona_rt_hamt_empty();
    hamt_or_aux_flags(r, HAMT_FLAG_IS_SET);
    int64_t* ae = yona_rt_set_elements(a);
    for (int64_t i = 0; i < ae[0]; i++) {
        int64_t e = ae[2 + i];
        if (!yona_rt_set_contains(b, e)) r = yona_rt_hamt_put(r, e, 1);
    }
    return (int64_t*)r;
}

void yona_rt_print_heap_value(int64_t val);
void yona_rt_hamt_print_set(hamt_node_t* node);

void yona_rt_print_set(int64_t* set) {
    if (!set) { printf("{}"); return; }
    int64_t* header = set - RC_HEADER_SIZE;
    int64_t tag = DECODE_TAG(header[1]);
    if (tag == RC_TYPE_DICT) {
        yona_rt_hamt_print_set((hamt_node_t*)set);
        return;
    }
    int64_t len = set[0];
    int hf = (int)set[1];
    printf("{");
    for (int64_t i = 0; i < len; i++) {
        if (i > 0) printf(", ");
        int64_t elem = set[i + 2];
        if (hf)
            yona_rt_print_heap_value(elem);
        else
            printf("%" PRId64, elem);
    }
    printf("}");
}

/* Print a heap pointer stored as i64 in a seq/tuple slot. Consults the RC
 * type tag so nested Seq/String/Tuple/… print as values, not addresses. */
void yona_rt_print_heap_value(int64_t val) {
    if (!val) {
        printf("0");
        return;
    }
    int64_t* ptr = (int64_t*)(intptr_t)val;
    int64_t tag = DECODE_TAG(ptr[-1]);
    switch ((int)tag) {
        case RC_TYPE_SEQ:
        case RC_TYPE_RBT_FWD:
            yona_rt_print_seq(ptr);
            break;
        case RC_TYPE_STRING:
            yona_rt_print_string((const char*)ptr);
            break;
        case RC_TYPE_SET:
            yona_rt_print_set(ptr);
            break;
        case RC_TYPE_DICT:
            if (hamt_aux_flags((hamt_node_t*)ptr) & HAMT_FLAG_IS_SET)
                yona_rt_print_set(ptr);
            else
                yona_rt_print_dict(ptr);
            break;
        case RC_TYPE_TUPLE: {
            int64_t n = ptr[0];
            int64_t mask = ptr[1];
            printf("(");
            for (int64_t i = 0; i < n; i++) {
                if (i > 0) printf(", ");
                int64_t elem = ptr[2 + i];
                if (i < 64 && (mask & ((int64_t)1 << i)))
                    yona_rt_print_heap_value(elem);
                else
                    printf("%" PRId64, elem);
            }
            printf(")");
            break;
        }
        case RC_TYPE_BYTE_ARRAY:
            yona_rt_print_byte_array(ptr);
            break;
        case RC_TYPE_INT_ARRAY:
            yona_rt_print_int_array(ptr);
            break;
        case RC_TYPE_FLOAT_ARRAY:
            yona_rt_print_float_array((double*)ptr);
            break;
        case RC_TYPE_ADT:
            printf("<adt>");
            break;
        case RC_TYPE_CLOSURE:
            printf("<function>");
            break;
        default:
            printf("%" PRId64, val);
            break;
    }
}

/* Forward declarations for ADT helpers used by iterators */
static inline int64_t* make_none(void);
static inline int64_t* make_some(int64_t value, int is_heap);
static inline int64_t* make_iterator(int64_t* closure);

/* ===== Dict/Set Streaming Iterators ===== */
/* Stack-based HAMT trie traversal. Yields entries one at a time via Iterator. */

/* Max HAMT depth: 64-bit hash / 5 bits per level = 13 levels.
 * Each stack frame tracks position within a node's data and children. */
#define HAMT_ITER_MAX_DEPTH 14

typedef struct {
    hamt_node_t* node;
    int data_idx;   /* next data entry to yield */
    int child_idx;  /* next child node to descend into */
    int data_count;
    int node_count;
} hamt_stack_frame_t;

typedef struct {
    hamt_stack_frame_t stack[HAMT_ITER_MAX_DEPTH];
    int depth;      /* current stack depth (0 = done) */
    int mode;       /* 0 = entries (key,val tuples), 1 = keys only, 2 = values only */
    int64_t* root;  /* RC ref to root dict/set — kept alive during iteration */
} hamt_iter_state_t;

static void hamt_iter_push(hamt_iter_state_t* st, hamt_node_t* node) {
    if (!node || st->depth >= HAMT_ITER_MAX_DEPTH) return;
    hamt_stack_frame_t* f = &st->stack[st->depth++];
    f->node = node;
    f->data_idx = 0;
    f->child_idx = 0;
    f->data_count = __builtin_popcountll((uint64_t)node->datamap);
    f->node_count = __builtin_popcountll((uint64_t)node->nodemap);
}

/* Advance to next entry. Returns 1 if found (key/val set), 0 if exhausted. */
static int hamt_iter_advance(hamt_iter_state_t* st, int64_t* out_key, int64_t* out_val) {
    while (st->depth > 0) {
        hamt_stack_frame_t* f = &st->stack[st->depth - 1];
        /* Yield data entries first */
        if (f->data_idx < f->data_count) {
            *out_key = f->node->payload[f->data_idx * 2];
            *out_val = f->node->payload[f->data_idx * 2 + 1];
            f->data_idx++;
            return 1;
        }
        /* Descend into child nodes */
        if (f->child_idx < f->node_count) {
            int dc = f->data_count;
            hamt_node_t* child = (hamt_node_t*)(intptr_t)f->node->payload[dc * 2 + f->child_idx];
            f->child_idx++;
            hamt_iter_push(st, child);
            continue;
        }
        /* Node exhausted — pop */
        st->depth--;
    }
    return 0;
}

/* Dict entries iterator: yields (key, value) tuples */
static int64_t dict_entries_iter_next(int64_t* env) {
    hamt_iter_state_t* st = (hamt_iter_state_t*)(intptr_t)env[5];
    int64_t key, val;
    if (!hamt_iter_advance(st, &key, &val)) {
        return (int64_t)(intptr_t)make_none();
    }
    /* Build a 2-tuple: heap-allocated [key, val] */
    int64_t* tup = (int64_t*)rc_alloc(RC_TYPE_ADT, 2 * sizeof(int64_t));
    tup[0] = key;
    tup[1] = val;
    return (int64_t)(intptr_t)make_some((int64_t)(intptr_t)tup, 1);
}

/* Dict keys iterator: yields keys */
static int64_t dict_keys_iter_next(int64_t* env) {
    hamt_iter_state_t* st = (hamt_iter_state_t*)(intptr_t)env[5];
    int64_t key, val;
    if (!hamt_iter_advance(st, &key, &val))
        return (int64_t)(intptr_t)make_none();
    return (int64_t)(intptr_t)make_some(key, 0);
}

/* Dict values iterator: yields values */
static int64_t dict_values_iter_next(int64_t* env) {
    hamt_iter_state_t* st = (hamt_iter_state_t*)(intptr_t)env[5];
    int64_t key, val;
    if (!hamt_iter_advance(st, &key, &val))
        return (int64_t)(intptr_t)make_none();
    return (int64_t)(intptr_t)make_some(val, 0);
}

/* Set elements iterator: yields elements (keys with val=1) */
static int64_t set_elements_iter_next(int64_t* env) {
    hamt_iter_state_t* st = (hamt_iter_state_t*)(intptr_t)env[5];
    int64_t key, val;
    if (!hamt_iter_advance(st, &key, &val))
        return (int64_t)(intptr_t)make_none();
    return (int64_t)(intptr_t)make_some(key, 0);
}

static int64_t hamt_make_iterator(int64_t* collection, void* next_fn) {
    extern void* yona_rt_closure_create(void* fn, int64_t ret, int64_t arity, int64_t caps);
    extern void yona_rt_closure_set_cap(void* cl, int64_t idx, int64_t val);
    hamt_iter_state_t* st = (hamt_iter_state_t*)malloc(sizeof(hamt_iter_state_t));
    st->depth = 0;
    st->root = collection;
    /* Keep the root alive during iteration */
    if (collection) yona_rt_rc_inc(collection);
    hamt_iter_push(st, (hamt_node_t*)collection);
    int64_t* cl = (int64_t*)yona_rt_closure_create(next_fn, 0, 0, 1);
    yona_rt_closure_set_cap(cl, 0, (int64_t)(intptr_t)st);
    return (int64_t)(intptr_t)make_iterator(cl);
}

/* Dict\entries : Dict -> Iterator (Int, Int) */
int64_t yona_Std_Dict__entries(int64_t* dict) {
    return hamt_make_iterator(dict, (void*)dict_entries_iter_next);
}

/* Dict\keys_iter : Dict -> Iterator Int (streaming, unlike keys which collects) */
int64_t yona_Std_Dict__keysIter(int64_t* dict) {
    return hamt_make_iterator(dict, (void*)dict_keys_iter_next);
}

/* Dict\values : Dict -> Iterator Int */
int64_t yona_Std_Dict__values(int64_t* dict) {
    return hamt_make_iterator(dict, (void*)dict_values_iter_next);
}

/* Set\iterator : Set -> Iterator Int */
int64_t yona_Std_Set__iterator(int64_t* set) {
    hamt_node_t* hamt = set_ensure_hamt(set);
    return hamt_make_iterator((int64_t*)hamt, (void*)set_elements_iter_next);
}

/* Dict\forEach : (Int -> Int -> ()) -> Dict -> () */
int64_t yona_Std_Dict__forEach(int64_t* fn, int64_t* dict) {
    hamt_iter_state_t st;
    st.depth = 0;
    st.root = dict;
    hamt_iter_push(&st, (hamt_node_t*)dict);
    int64_t key, val;
    typedef int64_t (*callback_fn_t)(int64_t*, int64_t, int64_t);
    callback_fn_t cb = (callback_fn_t)(intptr_t)fn[0];
    while (hamt_iter_advance(&st, &key, &val))
        cb(fn, key, val);
    return 0; /* unit */
}

/* Set\forEach : (Int -> ()) -> Set -> () */
int64_t yona_Std_Set__forEach(int64_t* fn, int64_t* set) {
    hamt_node_t* hamt = set_ensure_hamt(set);
    hamt_iter_state_t st;
    st.depth = 0;
    st.root = (int64_t*)hamt;
    hamt_iter_push(&st, hamt);
    int64_t key, val;
    typedef int64_t (*callback_fn_t)(int64_t*, int64_t);
    callback_fn_t cb = (callback_fn_t)(intptr_t)fn[0];
    while (hamt_iter_advance(&st, &key, &val))
        cb(fn, key);
    return 0; /* unit */
}

/* ===== ADT runtime (recursive types) ===== */
/* Heap-allocated ADT nodes: [tag (i8), field0 (i64), field1 (i64), ...] */
/* Used for recursive types like List a = Cons a (List a) | Nil        */

/* ADT layout (recursive, heap-allocated):
 * [tag, num_fields, heap_mask, field0, field1, ...]
 * tag: constructor tag (from adt_constructors_)
 * num_fields: number of fields
 * heap_mask: bitmask of which fields are heap-typed (for recursive rc_dec)
 * Fields start at index 3.
 */
#define ADT_HDR_SIZE 3  /* tag, num_fields, heap_mask */

/* Helper: allocate a None Option ADT */
static inline int64_t* make_none(void) {
    int64_t* adt = (int64_t*)rc_alloc(RC_TYPE_ADT, ADT_HDR_SIZE * sizeof(int64_t));
    adt[0] = 1; adt[1] = 0; adt[2] = 0;
    return adt;
}

/* Helper: allocate a Some(value) Option ADT.
 * heap_mask is 0 — the element is NOT owned by the wrapper.
 * Callers must rc_dec the element separately. This allows extracting
 * the element and freeing the wrapper without a use-after-free. */
static inline int64_t* make_some(int64_t value, int is_heap) {
    (void)is_heap; /* element ownership is caller's responsibility */
    int64_t* adt = (int64_t*)rc_alloc(RC_TYPE_ADT, (ADT_HDR_SIZE + 1) * sizeof(int64_t));
    adt[0] = 0; adt[1] = 1; adt[2] = 0; adt[3] = value;
    return adt;
}

/* Helper: allocate an Iterator ADT wrapping a closure */
static inline int64_t* make_iterator(int64_t* closure) {
    int64_t* adt = (int64_t*)rc_alloc(RC_TYPE_ADT, (ADT_HDR_SIZE + 1) * sizeof(int64_t));
    adt[0] = 0; adt[1] = 1; adt[2] = 0; /* heap_mask=0: closure managed separately */
    adt[3] = (int64_t)(intptr_t)closure;
    return adt;
}

void* yona_rt_adt_alloc(int64_t tag, int64_t num_fields) {
    int64_t* node = (int64_t*)rc_alloc(RC_TYPE_ADT, (ADT_HDR_SIZE + num_fields) * sizeof(int64_t));
    node[0] = tag;
    node[1] = num_fields;
    node[2] = 0; /* heap_mask — set by codegen */
    return node;
}

void yona_rt_adt_set_heap_mask(void* node, int64_t mask) {
    ((int64_t*)node)[2] = mask;
}

int64_t yona_rt_adt_get_tag(void* node) {
    return ((int64_t*)node)[0];
}

int64_t yona_rt_adt_get_field(void* node, int64_t index) {
    return ((int64_t*)node)[ADT_HDR_SIZE + index];
}

void yona_rt_adt_set_field(void* node, int64_t index, int64_t value) {
    ((int64_t*)node)[ADT_HDR_SIZE + index] = value;
}

/* Exceptions: setjmp/longjmp-based try/catch/raise */
#include "runtime/exceptions.c"
/* ===== Native stdlib shims ===== */
/* Pure C implementations of common stdlib functions for compiled code. */
/* These use the same mangled names as compiled Yona modules. */

/* Std\Math */
int64_t yona_Std_Math__abs(int64_t x) { return x < 0 ? -x : x; }
int64_t yona_Std_Math__max(int64_t a, int64_t b) { return a > b ? a : b; }
int64_t yona_Std_Math__min(int64_t a, int64_t b) { return a < b ? a : b; }
int64_t yona_Std_Math__factorial(int64_t n) {
    int64_t r = 1;
    for (int64_t i = 2; i <= n; i++) r *= i;
    return r;
}
double yona_Std_Math__sqrt(double x) { return sqrt(x); }
double yona_Std_Math__sin(double x) { return sin(x); }
double yona_Std_Math__cos(double x) { return cos(x); }

/* Std\String — pure string operations, no I/O */

int64_t yona_Std_String__length(const char* s) {
    return yona_rt_string_length_fast(s);
}

const char* yona_Std_String__toUpperCase(const char* s) {
    size_t len = strlen(s);
    char* r = (char*)rc_alloc(RC_TYPE_STRING, len + 1);
    for (size_t i = 0; i <= len; i++) r[i] = (char)toupper((unsigned char)s[i]);
    return r;
}

const char* yona_Std_String__toLowerCase(const char* s) {
    size_t len = strlen(s);
    char* r = (char*)rc_alloc(RC_TYPE_STRING, len + 1);
    for (size_t i = 0; i <= len; i++) r[i] = (char)tolower((unsigned char)s[i]);
    return r;
}

const char* yona_Std_String__trim(const char* s) {
    const char* start = s;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')) start++;
    const char* end = s + strlen(s) - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) end--;
    size_t len = end - start + 1;
    char* r = (char*)rc_alloc(RC_TYPE_STRING, len + 1);
    memcpy(r, start, len);
    r[len] = '\0';
    return r;
}

int64_t yona_Std_String__indexOf(const char* needle, const char* haystack) {
    const char* p = strstr(haystack, needle);
    return p ? (int64_t)(p - haystack) : -1;
}

int64_t yona_Std_String__contains(const char* needle, const char* haystack) {
    return strstr(haystack, needle) != NULL;
}

int64_t yona_Std_String__startsWith(const char* prefix, const char* s) {
    size_t plen = strlen(prefix);
    return strncmp(s, prefix, plen) == 0;
}

int64_t yona_Std_String__endsWith(const char* suffix, const char* s) {
    size_t slen = strlen(s);
    size_t xlen = strlen(suffix);
    if (xlen > slen) return 0;
    return strcmp(s + slen - xlen, suffix) == 0;
}

const char* yona_Std_String__substring(const char* s, int64_t start, int64_t len) {
    size_t slen = strlen(s);
    if (start < 0) start = 0;
    if ((size_t)start >= slen) { char* r = (char*)rc_alloc(RC_TYPE_STRING, 1); r[0] = '\0'; return r; }
    if (len < 0 || (size_t)(start + len) > slen) len = slen - start;
    char* r = (char*)rc_alloc(RC_TYPE_STRING, len + 1);
    memcpy(r, s + start, len);
    r[len] = '\0';
    return r;
}

const char* yona_Std_String__replace(const char* old, const char* new_s, const char* s) {
    size_t olen = strlen(old);
    size_t nlen = strlen(new_s);
    size_t slen = strlen(s);
    if (olen == 0) {
        size_t l = strlen(s);
        char* r = (char*)rc_alloc(RC_TYPE_STRING, l + 1);
        memcpy(r, s, l + 1);
        return r;
    }
    /* Count occurrences */
    size_t count = 0;
    const char* p = s;
    while ((p = strstr(p, old)) != NULL) { count++; p += olen; }
    /* Build result */
    size_t rlen = slen + count * (nlen - olen);
    char* r = (char*)rc_alloc(RC_TYPE_STRING, rlen + 1);
    char* w = r;
    p = s;
    while (*p) {
        if (strncmp(p, old, olen) == 0) {
            memcpy(w, new_s, nlen);
            w += nlen;
            p += olen;
        } else {
            *w++ = *p++;
        }
    }
    *w = '\0';
    return r;
}

/* split returns an Iterator that yields substrings on demand */
typedef struct { const char* str; const char* pos; const char* delim; size_t dlen; int done; } split_iter_state_t;

static int64_t split_iter_next(int64_t* env) {
    split_iter_state_t* st = (split_iter_state_t*)(intptr_t)env[5];
    if (st->done) return (int64_t)(intptr_t)make_none();
    const char* next = st->dlen > 0 ? strstr(st->pos, st->delim) : NULL;
    size_t len;
    if (st->dlen == 0) {
        if (*st->pos == '\0') {
            st->done = 1;
            return (int64_t)(intptr_t)make_none();
        }
        len = 1;
        next = NULL;
    } else {
        len = next ? (size_t)(next - st->pos) : strlen(st->pos);
    }
    extern void* yona_rt_rc_alloc_string_len(size_t bytes, size_t str_len);
    char* part = (char*)yona_rt_rc_alloc_string_len(len + 1, len);
    memcpy(part, st->pos, len); part[len] = '\0';
    if (next) st->pos = next + st->dlen;
    else if (st->dlen == 0) st->pos += 1;
    else st->done = 1;
    if (st->dlen == 0 && *st->pos == '\0') st->done = 1;
    return (int64_t)(intptr_t)make_some((int64_t)(intptr_t)part, 1);
}

int64_t yona_Std_String__split(const char* delim, const char* s) {
    split_iter_state_t* st = (split_iter_state_t*)malloc(sizeof(split_iter_state_t));
    st->str = s; st->pos = s; st->delim = delim;
    st->dlen = strlen(delim); st->done = 0;
    extern void* yona_rt_closure_create(void* fn, int64_t ret, int64_t arity, int64_t caps);
    extern void yona_rt_closure_set_cap(void* cl, int64_t idx, int64_t val);
    int64_t* cl = (int64_t*)yona_rt_closure_create((void*)split_iter_next, 0, 0, 1);
    yona_rt_closure_set_cap(cl, 0, (int64_t)(intptr_t)st);
    return (int64_t)(intptr_t)make_iterator(cl);
}

const char* yona_Std_String__join(const char* sep, int64_t* seq) {
    int64_t n = seq[0];
    if (n == 0) { char* r = (char*)rc_alloc(RC_TYPE_STRING, 1); r[0] = '\0'; return r; }
    size_t seplen = strlen(sep);
    /* Calculate total length */
    size_t total = 0;
    for (int64_t i = 0; i < n; i++) {
        const char* part = (const char*)seq[i + 1];
        total += strlen(part);
        if (i > 0) total += seplen;
    }
    char* r = (char*)rc_alloc(RC_TYPE_STRING, total + 1);
    char* w = r;
    for (int64_t i = 0; i < n; i++) {
        if (i > 0) { memcpy(w, sep, seplen); w += seplen; }
        const char* part = (const char*)seq[i + 1];
        size_t plen = strlen(part);
        memcpy(w, part, plen);
        w += plen;
    }
    *w = '\0';
    return r;
}

int64_t yona_Std_String__charAt(const char* s, int64_t idx) {
    size_t len = strlen(s);
    if (idx < 0 || (size_t)idx >= len) return 0;
    return (int64_t)(unsigned char)s[idx];
}

const char* yona_Std_String__padLeft(int64_t width, const char* pad, const char* s) {
    size_t slen = strlen(s);
    if ((int64_t)slen >= width) { char* r = (char*)rc_alloc(RC_TYPE_STRING, slen + 1); memcpy(r, s, slen + 1); return r; }
    size_t plen = strlen(pad);
    if (plen == 0) { char* r = (char*)rc_alloc(RC_TYPE_STRING, slen + 1); memcpy(r, s, slen + 1); return r; }
    char* r = (char*)rc_alloc(RC_TYPE_STRING, width + 1);
    size_t fill = width - slen;
    for (size_t i = 0; i < fill; i++) r[i] = pad[i % plen];
    memcpy(r + fill, s, slen + 1);
    return r;
}

const char* yona_Std_String__padRight(int64_t width, const char* pad, const char* s) {
    size_t slen = strlen(s);
    if ((int64_t)slen >= width) { char* r = (char*)rc_alloc(RC_TYPE_STRING, slen + 1); memcpy(r, s, slen + 1); return r; }
    size_t plen = strlen(pad);
    if (plen == 0) { char* r = (char*)rc_alloc(RC_TYPE_STRING, slen + 1); memcpy(r, s, slen + 1); return r; }
    char* r = (char*)rc_alloc(RC_TYPE_STRING, width + 1);
    memcpy(r, s, slen);
    size_t fill = width - slen;
    for (size_t i = 0; i < fill; i++) r[slen + i] = pad[i % plen];
    r[width] = '\0';
    return r;
}

const char* yona_Std_String__reverse(const char* s) {
    size_t len = strlen(s);
    char* r = (char*)rc_alloc(RC_TYPE_STRING, len + 1);
    for (size_t i = 0; i < len; i++) r[i] = s[len - 1 - i];
    r[len] = '\0';
    return r;
}

int64_t yona_Std_String__toInt(const char* s) { return (int64_t)atoll(s); }
double yona_Std_String__toFloat(const char* s) { return atof(s); }

int64_t yona_Std_String__isEmpty(const char* s) { return s[0] == '\0'; }

const char* yona_Std_String__repeat(int64_t n, const char* s) {
    size_t len = strlen(s);
    size_t total = len * (size_t)n;
    char* r = (char*)rc_alloc(RC_TYPE_STRING, total + 1);
    for (int64_t i = 0; i < n; i++) memcpy(r + i * len, s, len);
    r[total] = '\0';
    return r;
}

const char* yona_Std_String__take(int64_t n, const char* s) {
    size_t len = strlen(s);
    if ((size_t)n >= len) n = (int64_t)len;
    if (n < 0) n = 0;
    char* r = (char*)rc_alloc(RC_TYPE_STRING, (size_t)n + 1);
    memcpy(r, s, (size_t)n);
    r[n] = '\0';
    return r;
}

const char* yona_Std_String__drop(int64_t n, const char* s) {
    size_t len = strlen(s);
    if ((size_t)n >= len) return (const char*)rc_alloc(RC_TYPE_STRING, 1);
    if (n < 0) n = 0;
    size_t new_len = len - (size_t)n;
    char* r = (char*)rc_alloc(RC_TYPE_STRING, new_len + 1);
    memcpy(r, s + n, new_len + 1);
    return r;
}

int64_t yona_Std_String__count(const char* needle, const char* haystack) {
    if (needle[0] == '\0') return 0;
    int64_t count = 0;
    size_t nlen = strlen(needle);
    const char* p = haystack;
    while ((p = strstr(p, needle)) != NULL) { count++; p += nlen; }
    return count;
}

int64_t yona_Std_String__lines(const char* s) {
    /* Split by newline — returns Iterator */
    return yona_Std_String__split("\n", s);
}

const char* yona_Std_String__unlines(int64_t* seq) {
    return yona_Std_String__join("\n", seq);
}

/* chars returns an Iterator that yields each character as a single-char string */
typedef struct { const char* str; size_t pos; size_t len; } char_iter_state_t;

static int64_t char_iter_next(int64_t* env) {
    char_iter_state_t* st = (char_iter_state_t*)(intptr_t)env[5];
    if (st->pos >= st->len) return (int64_t)(intptr_t)make_none();
    int64_t ch = (int64_t)(unsigned char)st->str[st->pos++];
    return (int64_t)(intptr_t)make_some(ch, 0);
}

int64_t yona_Std_String__chars(const char* s) {
    size_t len = yona_rt_string_length_fast(s);
    char_iter_state_t* st = (char_iter_state_t*)malloc(sizeof(char_iter_state_t));
    st->str = s; st->pos = 0; st->len = len;
    extern void* yona_rt_closure_create(void* fn, int64_t ret, int64_t arity, int64_t caps);
    extern void yona_rt_closure_set_cap(void* cl, int64_t idx, int64_t val);
    int64_t* cl = (int64_t*)yona_rt_closure_create((void*)char_iter_next, 0, 0, 1);
    yona_rt_closure_set_cap(cl, 0, (int64_t)(intptr_t)st);
    return (int64_t)(intptr_t)make_iterator(cl);
}

const char* yona_Std_String__fromChars(int64_t* seq) {
    int64_t len = yona_rt_seq_length(seq);
    char* r = (char*)yona_rt_rc_alloc_string_len((size_t)len + 1, (size_t)len);
    for (int64_t i = 0; i < len; i++)
        r[i] = (char)yona_rt_seq_get(seq, i);
    r[len] = '\0';
    return r;
}

/* Std\Encoding — base64, hex, URL encoding */

static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

const char* yona_Std_Encoding__base64Encode(const char* s) {
    size_t len = strlen(s);
    size_t out_len = 4 * ((len + 2) / 3);
    char* r = (char*)rc_alloc(RC_TYPE_STRING, out_len + 1);
    size_t j = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t a = (uint8_t)s[i];
        uint32_t b = (i + 1 < len) ? (uint8_t)s[i + 1] : 0;
        uint32_t c = (i + 2 < len) ? (uint8_t)s[i + 2] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;
        r[j++] = b64_table[(triple >> 18) & 0x3F];
        r[j++] = b64_table[(triple >> 12) & 0x3F];
        r[j++] = (i + 1 < len) ? b64_table[(triple >> 6) & 0x3F] : '=';
        r[j++] = (i + 2 < len) ? b64_table[triple & 0x3F] : '=';
    }
    r[j] = '\0';
    return r;
}

static int b64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

const char* yona_Std_Encoding__base64Decode(const char* s) {
    size_t len = strlen(s);
    size_t out_len = 3 * len / 4;
    char* r = (char*)rc_alloc(RC_TYPE_STRING, out_len + 1);
    size_t j = 0;
    for (size_t i = 0; i < len; i += 4) {
        int a = b64_decode_char(s[i]);
        int b = (i + 1 < len) ? b64_decode_char(s[i + 1]) : 0;
        int c = (i + 2 < len) ? b64_decode_char(s[i + 2]) : 0;
        int d = (i + 3 < len) ? b64_decode_char(s[i + 3]) : 0;
        if (a < 0) a = 0; if (b < 0) b = 0;
        uint32_t triple = ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)c << 6) | (uint32_t)d;
        r[j++] = (triple >> 16) & 0xFF;
        if (i + 2 < len && s[i + 2] != '=') r[j++] = (triple >> 8) & 0xFF;
        if (i + 3 < len && s[i + 3] != '=') r[j++] = triple & 0xFF;
    }
    r[j] = '\0';
    return r;
}

static const char hex_chars[] = "0123456789abcdef";

const char* yona_Std_Encoding__hexEncode(const char* s) {
    size_t len = strlen(s);
    char* r = (char*)rc_alloc(RC_TYPE_STRING, len * 2 + 1);
    for (size_t i = 0; i < len; i++) {
        r[i * 2] = hex_chars[((uint8_t)s[i] >> 4) & 0xF];
        r[i * 2 + 1] = hex_chars[(uint8_t)s[i] & 0xF];
    }
    r[len * 2] = '\0';
    return r;
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

const char* yona_Std_Encoding__hexDecode(const char* s) {
    size_t len = strlen(s);
    size_t out_len = len / 2;
    char* r = (char*)rc_alloc(RC_TYPE_STRING, out_len + 1);
    for (size_t i = 0; i < out_len; i++)
        r[i] = (char)((hex_val(s[i * 2]) << 4) | hex_val(s[i * 2 + 1]));
    r[out_len] = '\0';
    return r;
}

const char* yona_Std_Encoding__urlEncode(const char* s) {
    size_t len = strlen(s);
    /* Worst case: every char is encoded as %XX (3x) */
    char* r = (char*)rc_alloc(RC_TYPE_STRING, len * 3 + 1);
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            r[j++] = c;
        } else {
            r[j++] = '%';
            r[j++] = hex_chars[(c >> 4) & 0xF];
            r[j++] = hex_chars[c & 0xF];
        }
    }
    r[j] = '\0';
    return r;
}

const char* yona_Std_Encoding__urlDecode(const char* s) {
    size_t len = strlen(s);
    char* r = (char*)rc_alloc(RC_TYPE_STRING, len + 1);
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '%' && i + 2 < len) {
            r[j++] = (char)((hex_val(s[i + 1]) << 4) | hex_val(s[i + 2]));
            i += 2;
        } else if (s[i] == '+') {
            r[j++] = ' ';
        } else {
            r[j++] = s[i];
        }
    }
    r[j] = '\0';
    return r;
}

const char* yona_Std_Encoding__htmlEscape(const char* s) {
    size_t len = strlen(s);
    /* Worst case: every char becomes &amp; (5x) */
    char* r = (char*)rc_alloc(RC_TYPE_STRING, len * 6 + 1);
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        switch (s[i]) {
            case '&':  memcpy(r + j, "&amp;", 5);  j += 5; break;
            case '<':  memcpy(r + j, "&lt;", 4);   j += 4; break;
            case '>':  memcpy(r + j, "&gt;", 4);   j += 4; break;
            case '"':  memcpy(r + j, "&quot;", 6); j += 6; break;
            case '\'': memcpy(r + j, "&#39;", 5);  j += 5; break;
            default:   r[j++] = s[i]; break;
        }
    }
    r[j] = '\0';
    return r;
}

/* ===== Platform I/O wrappers ===== */
/* Platform-specific implementations are in src/runtime/platform/ (C sources)
 * with shared headers in include/yona/runtime/. See CMakeLists.txt. */
#include "yona/runtime/platform.h"

/* yona_rt_io_await: platform/file_linux.c (io_uring) on Linux;
 * platform/file_macos.c (kqueue) on macOS;
 * platform/file_windows.c (direct-result table) on Windows. */

/* Resource cleanup for `with` expression. Closes file descriptors,
 * sockets, or other resources based on the value type. */
void yona_rt_close(int64_t handle) {
    /* Only close valid-looking file descriptors (small positive integers).
     * Arbitrary i64 values (pointers, large ints) are not fds. */
    if (handle > 2 && handle < 65536)
        yona_platform_close_file_handle((int)handle);
}

/* Std\IO — non-blocking writes via io_uring, non-blocking line reads
 * via the thread pool. Every function that can block submits its work
 * off the calling task and returns a uring / async id; the codegen's
 * IO / AFN markers auto-await at the use site.
 *
 * The int64_t fd argument unpacks directly from the FileHandle ADT
 * wrapper (heap-boxed `{tag, fd}`); user code writes
 * `case h of FileHandle fd -> ...` to get the raw fd, and the stdin /
 * stdout / stderr CAFs in Std\IO return already-extracted fd ints. */

extern int64_t yona_platform_write_fd_str_submit(int fd, const char* s);
extern int64_t yona_platform_write_fd_strs_submit(int fd, const char* s1, const char* s2);
extern const char* yona_platform_read_line_fd(int fd);

/* Write a string to an fd (no trailing newline). Returns uring ID. */
int64_t yona_Std_IO__writeStr(int64_t fd, const char* s) {
    return yona_platform_write_fd_str_submit((int)fd, s);
}

/* Write a string followed by "\n". Returns uring ID. */
int64_t yona_Std_IO__writeLine(int64_t fd, const char* s) {
    return yona_platform_write_fd_strs_submit((int)fd, s, "\n");
}

int64_t yona_Std_IO__print(const char* s) {
    return yona_Std_IO__writeStr(1, s);
}

int64_t yona_Std_IO__println(const char* s) {
    return yona_Std_IO__writeLine(1, s);
}

int64_t yona_Std_IO__eprint(const char* s) {
    return yona_Std_IO__writeStr(2, s);
}

int64_t yona_Std_IO__eprintln(const char* s) {
    return yona_Std_IO__writeLine(2, s);
}

int64_t yona_Std_IO__putStr(int64_t fd, const char* s) {
    return yona_Std_IO__writeStr(fd, s);
}

int64_t yona_Std_IO__putStrLn(int64_t fd, const char* s) {
    return yona_Std_IO__writeLine(fd, s);
}

int64_t yona_Std_IO__write(int64_t fd, const char* s) {
    return yona_Std_IO__writeStr(fd, s);
}

/* Thread-pool async: read one line from fd. Returns Option String
 * (heap-allocated Some line / None at EOF). Called via AFN so the
 * read blocks on a worker thread, not the calling task. */
int64_t yona_Std_IO__readLineFd(int64_t fd) {
    const char* line = yona_platform_read_line_fd((int)fd);
    if (!line) {
        /* EOF → None */
        int64_t* adt = (int64_t*)rc_alloc(RC_TYPE_ADT, (ADT_HDR_SIZE + 0) * sizeof(int64_t));
        adt[0] = 1;  /* tag = None */
        adt[1] = 0;  /* num_fields */
        adt[2] = 0;  /* heap_mask */
        return (int64_t)(intptr_t)adt;
    }
    /* Some line — the string is heap-owned, so bit 0 of heap_mask is set. */
    int64_t* adt = (int64_t*)rc_alloc(RC_TYPE_ADT, (ADT_HDR_SIZE + 1) * sizeof(int64_t));
    adt[0] = 0;  /* tag = Some */
    adt[1] = 1;  /* num_fields */
    adt[2] = 1;  /* heap_mask — field 0 is heap */
    adt[3] = (int64_t)(intptr_t)line;
    return (int64_t)(intptr_t)adt;
}

/* Synchronous: isatty(fd). Cheap syscall, no async needed. */
int64_t yona_Std_IO__isTty(int64_t fd) {
    return isatty((int)fd) == 1 ? 1 : 0;
}

/* Synchronous: fsync(fd). Most io_uring writes are durable at
 * completion; this is a backstop for user-managed buffers. */
int64_t yona_Std_IO__flushFd(int64_t fd) {
    return yona_platform_flush_file_handle((int)fd);
}

/* Std\File — async ops return uring ID, sync ops return directly */
/* Register a direct result for io_await fallback when io_uring is unavailable */
extern int64_t yona_io_register_direct_result(void* result);
#define io_register_direct_result yona_io_register_direct_result

int64_t yona_Std_File__readFile(const char* path) {
    int64_t id = yona_platform_read_file_submit(path);
    if (id > 0) return id; /* uring ID = promise */
    /* io_uring unavailable: read synchronously, wrap result for io_await */
    char* data = yona_platform_read_file(path);
    return io_register_direct_result(data);
}
int64_t yona_Std_File__writeFile(const char* path, const char* content) {
    int64_t id = yona_platform_write_file_submit(path, content);
    if (id > 0) return id; /* uring ID = promise */
    int64_t ok = yona_platform_write_file(path, content) == 0 ? 1 : 0;
    return io_register_direct_result((void*)(intptr_t)ok);
}
int64_t yona_Std_File__appendFile(const char* path, const char* content) {
    return yona_platform_append_file(path, content) == 0 ? 1 : 0;
}
int64_t yona_Std_File__exists(const char* path) {
    return yona_platform_file_exists(path);
}
int64_t yona_Std_File__remove(const char* path) {
    return yona_platform_remove_file(path) == 0 ? 1 : 0;
}
int64_t yona_Std_File__size(const char* path) {
    return yona_platform_file_size(path);
}
int64_t* yona_Std_File__listDir(const char* path) {
    return yona_platform_list_dir(path);
}

/* readLines: read file, split by newlines, return seq of strings */
/* readLines now returns an Iterator String (streaming, O(64KB) memory).
 * The Iterator wraps a 64KB-buffered file reader that yields lines on demand. */
int64_t yona_Std_File__readLines(const char* path) {
    extern int64_t yona_rt_file_line_iterator(const char* path);
    return yona_rt_file_line_iterator(path);
}

/* ===== Std\Set — persistent hash set (HAMT-backed) ===== */
int64_t* yona_Std_Set__insert(int64_t* set, int64_t elem) {
    return yona_rt_set_insert(set, elem);
}
int64_t yona_Std_Set__contains(int64_t* set, int64_t elem) {
    return yona_rt_set_contains(set, elem);
}
int64_t yona_Std_Set__size(int64_t* set) {
    return yona_rt_set_size(set);
}
int64_t* yona_Std_Set__elements(int64_t* set) {
    return yona_rt_set_elements(set);
}
int64_t* yona_Std_Set__union(int64_t* a, int64_t* b) {
    return yona_rt_set_union(a, b);
}
int64_t* yona_Std_Set__intersection(int64_t* a, int64_t* b) {
    return yona_rt_set_intersection(a, b);
}
int64_t* yona_Std_Set__difference(int64_t* a, int64_t* b) {
    return yona_rt_set_difference(a, b);
}

/* ===== Std\Dict — persistent hash map (HAMT) ===== */
int64_t* yona_Std_Dict__put(int64_t* dict, int64_t key, int64_t value) {
    return yona_rt_dict_put(dict, key, value);
}
int64_t yona_Std_Dict__get(int64_t* dict, int64_t key, int64_t default_val) {
    return yona_rt_dict_get(dict, key, default_val);
}
int64_t yona_Std_Dict__contains(int64_t* dict, int64_t key) {
    return yona_rt_dict_contains(dict, key);
}
int64_t yona_Std_Dict__size(int64_t* dict) {
    return yona_rt_dict_size(dict);
}
int64_t* yona_Std_Dict__keys(int64_t* dict) {
    return yona_rt_dict_keys(dict);
}

/* Binary file I/O */
int64_t yona_Std_File__readFileBytes(const char* path) {
    extern int64_t yona_platform_read_file_bytes_submit(const char* path);
    int64_t id = yona_platform_read_file_bytes_submit(path);
    if (id > 0) return id;
    extern void* yona_rt_byte_array_from_string(const char* s);
    void* bytes = yona_rt_byte_array_from_string(yona_platform_read_file(path));
    return io_register_direct_result(bytes);
}

int64_t yona_Std_File__writeFileBytes(const char* path, void* bytes) {
    int64_t* b = (int64_t*)bytes;
    int64_t len = b[0];
    uint8_t* data = (uint8_t*)(b + 1);
    /* Sync write for binary data */
    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    size_t w = fwrite(data, 1, (size_t)len, f);
    fclose(f);
    return (w == (size_t)len) ? 1 : 0;
}

/* ===== Std\File — handle-based binary I/O ===== */

/* Helper: extract fd from FileHandle ADT (passed as i64 pointer-as-int) */
static int fh_fd(int64_t handle_i64) {
    int64_t* handle = (int64_t*)(intptr_t)handle_i64;
    return (int)handle[3]; /* ADT_HDR_SIZE=3, field 0 = fd */
}

/* openFile: open file and return FileHandle ADT wrapping the fd.
 * mode is a FileMode ADT: Read=0, Write=1, ReadWrite=2, Append=3.
 * Returns FileHandle ADT: [tag=0, num_fields=1, heap_mask=0, fd] */
int64_t yona_Std_File__openFile(const char* path, int64_t mode_i64) {
    int64_t mode_tag = mode_i64;
    if (mode_i64 > 16) {
        int64_t* mode_adt = (int64_t*)(intptr_t)mode_i64;
        mode_tag = mode_adt[0];  /* recursive ADT layout: [tag, num_fields, heap_mask, ...] */
    }
    int fd = (int)yona_platform_open_file_handle(path, mode_tag);
    if (fd < 0) {
        extern void yona_rt_raise(int64_t tag, const char* msg);
        yona_rt_raise(0, "cannot open file");
        return 0;
    }

    /* Wrap in FileHandle ADT: [tag=0, num_fields=1, heap_mask=0, fd] */
    int64_t* adt = (int64_t*)rc_alloc(RC_TYPE_ADT, 4 * sizeof(int64_t));
    adt[0] = 0;         /* tag = FileHandle */
    adt[1] = 1;         /* num_fields */
    adt[2] = 0;         /* heap_mask = 0 (fd is not heap) */
    adt[3] = (int64_t)fd;
    return (int64_t)(intptr_t)adt;
}

/* closeFileHandle: extract fd from FileHandle ADT and close it */
int64_t yona_Std_File__closeFileHandle(int64_t handle_i64) {
    int fd = fh_fd(handle_i64);
    yona_platform_close_file_handle(fd);
    return 0;
}

/* readBytes: read up to count bytes from FileHandle at current position.
 * Uses io_uring pread with userspace position tracking. */
int64_t yona_Std_File__readBytes(int64_t handle, int64_t count) {
    int fd = fh_fd(handle);
    int64_t pos = yona_platform_tell_file_handle(fd);
    if (pos < 0) pos = 0;
    extern int64_t yona_platform_read_fd_bytes_submit(int fd, int64_t count, int64_t offset);
    int64_t id = yona_platform_read_fd_bytes_submit(fd, count, pos);
    yona_platform_seek_file_handle(fd, pos + count, 0);
    return id;
}

/* writeBytes: write Bytes to FileHandle via io_uring pwrite. Returns bytes written. */
int64_t yona_Std_File__writeBytes(int64_t handle, int64_t bytes_i64) {
    int fd = fh_fd(handle);
    void* bytes = (void*)(intptr_t)bytes_i64;
    int64_t* b = (int64_t*)bytes;
    int64_t len = b[0];
    int64_t pos = yona_platform_tell_file_handle(fd);
    if (pos < 0) pos = 0;
    extern int64_t yona_platform_write_fd_bytes_submit(int fd, void* bytes, int64_t offset);
    int64_t id = yona_platform_write_fd_bytes_submit(fd, bytes, pos);
    yona_platform_seek_file_handle(fd, pos + len, 0);
    return id;
}

/* seek: set file position. whence is a Whence ADT: SeekSet=0, SeekCur=1, SeekEnd=2 */
int64_t yona_Std_File__seek(int64_t handle, int64_t offset, int64_t whence_i64) {
    int fd = fh_fd(handle);
    int64_t whence_tag = whence_i64;
    if (whence_i64 > 16) {
        int64_t* whence_adt = (int64_t*)(intptr_t)whence_i64;
        whence_tag = whence_adt[0];
    }
    return yona_platform_seek_file_handle(fd, offset, whence_tag);
}

/* tell: get current file position */
int64_t yona_Std_File__tell(int64_t handle) {
    int fd = fh_fd(handle);
    return yona_platform_tell_file_handle(fd);
}

/* flush: fsync */
int64_t yona_Std_File__flush(int64_t handle) {
    int fd = fh_fd(handle);
    return yona_platform_flush_file_handle(fd);
}

/* truncate: ftruncate */
int64_t yona_Std_File__truncate(int64_t handle, int64_t length) {
    int fd = fh_fd(handle);
    return yona_platform_truncate_file_handle(fd, length);
}

/* readChunks: create streaming binary chunk iterator from FileHandle */
int64_t yona_Std_File__readChunks(int64_t handle, int64_t chunk_size) {
    int fd = fh_fd(handle);
    extern int64_t yona_rt_file_chunk_iterator(int64_t fd, int64_t chunk_size);
    return yona_rt_file_chunk_iterator((int64_t)fd, chunk_size);
}

/* Std\Process */
const char* yona_Std_Process__getenv(const char* name) {
    return yona_platform_getenv(name);
}
const char* yona_Std_Process__getcwd(void) {
    return yona_platform_getcwd();
}
int64_t yona_Std_Process__exit(int64_t code) {
    return yona_platform_exit_process(code);
}
const char* yona_Std_Process__exec(const char* cmd) {
    return yona_platform_exec(cmd);
}
int64_t yona_Std_Process__execStatus(const char* cmd) {
    return yona_platform_exec_status(cmd);
}
int64_t yona_Std_Process__setenv(const char* name, const char* value) {
    return yona_platform_setenv(name, value);
}
const char* yona_Std_Process__hostname(void) {
    return yona_platform_hostname();
}

static int yona_rt_argc = 0;
static char** yona_rt_argv = NULL;

void yona_rt_set_process_args(int argc, char** argv) {
    yona_rt_argc = argc;
    yona_rt_argv = argv;
}

static char* yona_rt_copy_cstr(const char* src) {
    if (!src) src = "";
    size_t n = strlen(src);
    char* r = (char*)yona_rt_rc_alloc_string(n + 1);
    memcpy(r, src, n + 1);
    return r;
}

int64_t* yona_Std_Process__getArgs(void) {
    extern int64_t* yona_rt_seq_alloc(int64_t count);
    extern void yona_rt_seq_set(int64_t* seq, int64_t index, int64_t value);
    extern void yona_rt_seq_set_heap(int64_t* seq, int64_t flag);
    int64_t n = yona_rt_argc > 0 ? (int64_t)yona_rt_argc : 0;
    int64_t* seq = yona_rt_seq_alloc(n);
    for (int64_t i = 0; i < n; i++) {
        const char* a = (yona_rt_argv && yona_rt_argv[i]) ? yona_rt_argv[i] : "";
        yona_rt_seq_set(seq, i, (int64_t)(intptr_t)yona_rt_copy_cstr(a));
    }
    yona_rt_seq_set_heap(seq, 1);
    return seq;
}

const char* yona_Std_Process__yonaVersion(void) {
    return yona_rt_copy_cstr(YONA_VERSION_STRING);
}

char* yona_Std_IO__readStdin(void) {
    const size_t max_cap = 64u * 1024u * 1024u;
    size_t cap = 4096, len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) return yona_rt_copy_cstr("");
    for (;;) {
        if (len >= cap) {
            if (cap >= max_cap) break;
            size_t ncap = cap * 2;
            if (ncap > max_cap) ncap = max_cap;
            char* nb = (char*)realloc(buf, ncap);
            if (!nb) break;
            buf = nb;
            cap = ncap;
        }
#if defined(_WIN32)
        int n = (int)read(0, buf + len, (unsigned)(cap - len));
#else
        ssize_t n = read(0, buf + len, cap - len);
#endif
        if (n <= 0) break;
        len += (size_t)n;
    }
    char* r = (char*)yona_rt_rc_alloc_string(len + 1);
    memcpy(r, buf, len);
    r[len] = '\0';
    free(buf);
    return r;
}

/* readExact: stream read() loop — pipe/socket safe (not pread/seek).
 * `fd_or_handle` is a raw descriptor (stdin is 0) or a FileHandle ADT. */
#ifndef YONA_READ_EXACT_MAX
#define YONA_READ_EXACT_MAX (16u * 1024u * 1024u)
#endif

char* yona_Std_IO__readExactBytes(int64_t fd_or_handle, int64_t n) {
    if (n <= 0 || (uint64_t)n > YONA_READ_EXACT_MAX) {
        char* r = (char*)yona_rt_rc_alloc_string_len(1, 0);
        r[0] = '\0';
        return r;
    }
    int fd;
    if (fd_or_handle > 65536) {
        int64_t* handle = (int64_t*)(intptr_t)fd_or_handle;
        /* Linear FileHandle is [tag, 1, heap, inner]; FileHandle is [tag, 1, 0, fd]. */
        if (handle[1] == 1 && handle[2] != 0 && handle[3] > 65536) {
            int64_t* inner = (int64_t*)(intptr_t)handle[3];
            fd = (int)inner[3];
        } else {
            fd = (int)handle[3];
        }
    } else {
        fd = (int)fd_or_handle;
    }
    size_t want = (size_t)n;
    char* buf = (char*)malloc(want);
    if (!buf) {
        char* r = (char*)yona_rt_rc_alloc_string_len(1, 0);
        r[0] = '\0';
        return r;
    }
    size_t got = 0;
    while (got < want) {
#if defined(_WIN32)
        int k = (int)_read(fd, buf + got, (unsigned)(want - got));
#else
        ssize_t k = read(fd, buf + got, want - got);
#endif
        if (k <= 0)
            break;
        got += (size_t)k;
    }
    char* r = (char*)yona_rt_rc_alloc_string_len(got + 1, got);
    memcpy(r, buf, got);
    r[got] = '\0';
    free(buf);
    return r;
}

/* Result Json/String-style ADT: Ok tag 0 | Err tag 1, one heap field. */
static int64_t yona_io_result_ok_str(char* s) {
    int64_t* adt = (int64_t*)rc_alloc(RC_TYPE_ADT, (ADT_HDR_SIZE + 1) * sizeof(int64_t));
    adt[0] = 0;
    adt[1] = 1;
    adt[2] = 1;
    adt[3] = (int64_t)(intptr_t)s;
    return (int64_t)(intptr_t)adt;
}

static int64_t yona_io_result_err(const char* msg) {
    int64_t* adt = (int64_t*)rc_alloc(RC_TYPE_ADT, (ADT_HDR_SIZE + 1) * sizeof(int64_t));
    adt[0] = 1;
    adt[1] = 1;
    adt[2] = 1;
    adt[3] = (int64_t)(intptr_t)yona_rt_copy_cstr(msg);
    return (int64_t)(intptr_t)adt;
}

int64_t yona_Std_IO__readExact(int64_t fd_or_handle, int64_t n) {
    if (n < 0)
        return yona_io_result_err("negative count");
    if ((uint64_t)n > YONA_READ_EXACT_MAX)
        return yona_io_result_err("too large");
    char* s = yona_Std_IO__readExactBytes(fd_or_handle, n);
    if (yona_rt_string_length_fast(s) == n)
        return yona_io_result_ok_str(s);
    return yona_io_result_err("unexpected eof");
}

/* Synchronous write — no Promise. Safe for LSP Content-Length framing. */
void yona_Std_IO__writeBytes(int64_t fd_or_handle, const char* s) {
    if (!s)
        return;
    int fd;
    if (fd_or_handle > 65536) {
        int64_t* handle = (int64_t*)(intptr_t)fd_or_handle;
        if (handle[1] == 1 && handle[2] != 0 && handle[3] > 65536) {
            int64_t* inner = (int64_t*)(intptr_t)handle[3];
            fd = (int)inner[3];
        } else {
            fd = (int)handle[3];
        }
    } else {
        fd = (int)fd_or_handle;
    }
    size_t n = (size_t)yona_rt_string_length_fast(s);
    size_t off = 0;
    while (off < n) {
#if defined(_WIN32)
        int k = (int)_write(fd, s + off, (unsigned)(n - off));
#else
        ssize_t k = write(fd, s + off, n - off);
#endif
        if (k <= 0)
            break;
        off += (size_t)k;
    }
}

/* ===== Std\Http — HTTP client ===== */

/* Build an HTTP/1.1 request string */
const char* yona_Std_Http__buildRequest(const char* method, const char* host,
                                         const char* path, const char* body) {
    size_t body_len = body ? strlen(body) : 0;
    size_t host_len = strlen(host);
    size_t path_len = strlen(path);
    size_t method_len = strlen(method);

    /* Calculate total size */
    size_t total = method_len + 1 + path_len + 11 /* " HTTP/1.1\r\n" */
        + 6 + host_len + 2 /* "Host: ...\r\n" */
        + 24 /* "Connection: close\r\n" */
        + 19 /* "User-Agent: Yona\r\n" */;
    if (body_len > 0) {
        total += 32 /* "Content-Length: NNN\r\n" */
            + 48 /* "Content-Type: application/x-www-form-urlencoded\r\n" */
            + 2 + body_len;
    } else {
        total += 2; /* final \r\n */
    }

    char* r = (char*)rc_alloc(RC_TYPE_STRING, total + 64);
    int n;
    if (body_len > 0) {
        n = snprintf(r, total + 64,
            "%s %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: Yona\r\n"
            "Connection: close\r\n"
            "Content-Length: %zu\r\n"
            "Content-Type: application/x-www-form-urlencoded\r\n"
            "\r\n"
            "%s",
            method, path, host, body_len, body);
    } else {
        n = snprintf(r, total + 64,
            "%s %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: Yona\r\n"
            "Connection: close\r\n"
            "\r\n",
            method, path, host);
    }
    r[n] = '\0';
    return r;
}

/* Parse HTTP status code from response string */
int64_t yona_Std_Http__parseStatus(const char* response) {
    /* HTTP/1.1 200 OK\r\n... */
    if (strncmp(response, "HTTP/", 5) != 0) return 0;
    const char* sp = strchr(response, ' ');
    if (!sp) return 0;
    return (int64_t)atoi(sp + 1);
}

/* Extract HTTP response body (after \r\n\r\n) */
const char* yona_Std_Http__parseBody(const char* response) {
    const char* body = strstr(response, "\r\n\r\n");
    if (!body) {
        char* r = (char*)rc_alloc(RC_TYPE_STRING, 1);
        r[0] = '\0';
        return r;
    }
    body += 4;
    size_t len = strlen(body);
    char* r = (char*)rc_alloc(RC_TYPE_STRING, len + 1);
    memcpy(r, body, len + 1);
    return r;
}

/* Extract a specific HTTP header value */
const char* yona_Std_Http__getHeader(const char* name, const char* response) {
    size_t name_len = strlen(name);
    const char* p = response;
    while ((p = strstr(p, name)) != NULL) {
        /* Check it's at start of line */
        if (p == response || *(p - 1) == '\n') {
            p += name_len;
            if (*p == ':') {
                p++;
                while (*p == ' ') p++;
                const char* end = strstr(p, "\r\n");
                size_t len = end ? (size_t)(end - p) : strlen(p);
                char* r = (char*)rc_alloc(RC_TYPE_STRING, len + 1);
                memcpy(r, p, len);
                r[len] = '\0';
                return r;
            }
        }
        p++;
    }
    char* r = (char*)rc_alloc(RC_TYPE_STRING, 1);
    r[0] = '\0';
    return r;
}

/* Parse URL into (host, port, path) — returns seq of 3 elements [host, port, path] */
int64_t* yona_Std_Http__parseUrl(const char* url) {
    int64_t* result = yona_rt_seq_alloc(3);
    int port = 80;
    const char* host_start = url;
    const char* path_start = "/";

    /* Skip scheme */
    if (strncmp(url, "http://", 7) == 0) { host_start = url + 7; port = 80; }
    else if (strncmp(url, "https://", 8) == 0) { host_start = url + 8; port = 443; }

    /* Find path */
    const char* slash = strchr(host_start, '/');
    size_t host_len;
    if (slash) {
        host_len = (size_t)(slash - host_start);
        path_start = slash;
    } else {
        host_len = strlen(host_start);
    }

    /* Check for port in host */
    const char* colon = (const char*)memchr(host_start, ':', host_len);
    if (colon) {
        port = atoi(colon + 1);
        host_len = (size_t)(colon - host_start);
    }

    char* host = (char*)rc_alloc(RC_TYPE_STRING, host_len + 1);
    memcpy(host, host_start, host_len);
    host[host_len] = '\0';

    size_t path_len = strlen(path_start);
    char* path = (char*)rc_alloc(RC_TYPE_STRING, path_len + 1);
    memcpy(path, path_start, path_len + 1);

    result[1] = (int64_t)(intptr_t)host;
    result[2] = (int64_t)port;
    result[3] = (int64_t)(intptr_t)path;
    return result;
}

/* yona_Std_Http__httpGet: platform/net_linux.c (io_uring) or net_windows.c (Winsock). */

/* Std\Random — pseudo-random number generation */

static int yona_random_initialized = 0;

static void yona_random_init(void) {
    if (!yona_random_initialized) {
        srand((unsigned)time(NULL));
        yona_random_initialized = 1;
    }
}

int64_t yona_Std_Random__int(int64_t lo, int64_t hi) {
    yona_random_init();
    if (lo >= hi) return lo;
    return lo + (int64_t)(rand() % (hi - lo + 1));
}

double yona_Std_Random__float(void) {
    yona_random_init();
    return (double)rand() / (double)RAND_MAX;
}

int64_t yona_Std_Random__choice(int64_t* seq) {
    yona_random_init();
    int64_t len = seq[0];
    if (len <= 0) return 0;
    return seq[1 + (int64_t)(rand() % len)];
}

int64_t* yona_Std_Random__shuffle(int64_t* seq) {
    yona_random_init();
    int64_t len = seq[0];
    int64_t* result = yona_rt_seq_alloc(len);
    memcpy(result + 1, seq + 1, len * sizeof(int64_t));
    /* Fisher-Yates shuffle */
    for (int64_t i = len - 1; i > 0; i--) {
        int64_t j = (int64_t)(rand() % (i + 1));
        int64_t tmp = result[1 + i];
        result[1 + i] = result[1 + j];
        result[1 + j] = tmp;
    }
    return result;
}

/* ===== Std\Time — time measurement and utilities ===== */

#if defined(__linux__) || defined(__APPLE__)
#include <sys/time.h>
#endif

/* 100-ns intervals from 1601-01-01 to 1970-01-01 */
#if defined(_WIN32)
#define YONA_WIN_EPOCH_100NS 116444736000000000ULL
#endif

/* Current time as epoch milliseconds */
int64_t yona_Std_Time__now(void) {
#if defined(_WIN32)
	FILETIME ft;
	ULARGE_INTEGER u;
	GetSystemTimeAsFileTime(&ft);
	u.LowPart = ft.dwLowDateTime;
	u.HighPart = ft.dwHighDateTime;
	return (int64_t)((u.QuadPart - YONA_WIN_EPOCH_100NS) / 10000ULL);
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (int64_t)tv.tv_sec * 1000 + (int64_t)tv.tv_usec / 1000;
#endif
}

/* Current time as epoch microseconds */
int64_t yona_Std_Time__nowMicros(void) {
#if defined(_WIN32)
	FILETIME ft;
	ULARGE_INTEGER u;
	GetSystemTimeAsFileTime(&ft);
	u.LowPart = ft.dwLowDateTime;
	u.HighPart = ft.dwHighDateTime;
	return (int64_t)((u.QuadPart - YONA_WIN_EPOCH_100NS) / 10ULL);
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (int64_t)tv.tv_sec * 1000000 + (int64_t)tv.tv_usec;
#endif
}

/* Current time as epoch seconds */
int64_t yona_Std_Time__epoch(void) {
    return (int64_t)time(NULL);
}

/* Sleep for N milliseconds */
void yona_Std_Time__sleep(int64_t ms) {
#if defined(_WIN32)
	if (ms <= 0) return;
	while (ms > 0) {
		DWORD chunk = (ms > 0x7FFFFFFF) ? 0x7FFFFFFF : (DWORD)ms;
		Sleep(chunk);
		ms -= (int64_t)chunk;
	}
#else
	usleep((useconds_t)(ms * 1000));
#endif
}

/* Format epoch seconds as ISO 8601 string (YYYY-MM-DD HH:MM:SS) */
const char* yona_Std_Time__format(int64_t epoch_secs) {
    time_t t = (time_t)epoch_secs;
    struct tm* tm = localtime(&t);
    char* r = (char*)rc_alloc(RC_TYPE_STRING, 20);
    strftime(r, 20, "%Y-%m-%d %H:%M:%S", tm);
    return r;
}

/* Elapsed milliseconds between two now() timestamps */
int64_t yona_Std_Time__elapsed(int64_t start, int64_t end) {
    return end - start;
}

/* ===== Std\Path — file path manipulation ===== */

static int yona_path_is_sep(char c) {
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

const char* yona_Std_Path__join(const char* a, const char* b) {
    size_t la = strlen(a), lb = strlen(b);
    /* Skip trailing slash on a */
    if (la > 0 && yona_path_is_sep(a[la-1])) la--;
    /* Skip leading slash on b */
    const char* bs = b;
    if (lb > 0 && yona_path_is_sep(b[0])) { bs++; lb--; }
    char* r = (char*)rc_alloc(RC_TYPE_STRING, la + 1 + lb + 1);
    memcpy(r, a, la);
    r[la] = '/';
    memcpy(r + la + 1, bs, lb);
    r[la + 1 + lb] = '\0';
    return r;
}

const char* yona_Std_Path__dirname(const char* path) {
    size_t len = strlen(path);
    /* Find last slash */
    const char* last = NULL;
    for (size_t i = 0; i < len; i++) if (yona_path_is_sep(path[i])) last = path + i;
    if (!last) { char* r = (char*)rc_alloc(RC_TYPE_STRING, 2); r[0] = '.'; r[1] = '\0'; return r; }
    size_t dlen = (size_t)(last - path);
    if (dlen == 0) dlen = 1; /* root "/" */
    char* r = (char*)rc_alloc(RC_TYPE_STRING, dlen + 1);
    memcpy(r, path, dlen);
    r[dlen] = '\0';
    return r;
}

const char* yona_Std_Path__basename(const char* path) {
    size_t len = strlen(path);
    const char* last = path;
    for (size_t i = 0; i < len; i++) if (yona_path_is_sep(path[i])) last = path + i + 1;
    size_t blen = strlen(last);
    char* r = (char*)rc_alloc(RC_TYPE_STRING, blen + 1);
    memcpy(r, last, blen + 1);
    return r;
}

const char* yona_Std_Path__extension(const char* path) {
    const char* base = path;
    size_t len = strlen(path);
    for (size_t i = 0; i < len; i++) if (yona_path_is_sep(path[i])) base = path + i + 1;
    const char* dot = NULL;
    for (const char* p = base; *p; p++) if (*p == '.') dot = p;
    if (!dot || dot == base) { char* r = (char*)rc_alloc(RC_TYPE_STRING, 1); r[0] = '\0'; return r; }
    size_t elen = strlen(dot);
    char* r = (char*)rc_alloc(RC_TYPE_STRING, elen + 1);
    memcpy(r, dot, elen + 1);
    return r;
}

const char* yona_Std_Path__withExtension(const char* path, const char* ext) {
    /* Find last dot in basename */
    const char* base = path;
    size_t len = strlen(path);
    for (size_t i = 0; i < len; i++) if (yona_path_is_sep(path[i])) base = path + i + 1;
    const char* dot = NULL;
    for (const char* p = base; *p; p++) if (*p == '.') dot = p;
    size_t stem_len = dot ? (size_t)(dot - path) : len;
    size_t elen = strlen(ext);
    char* r = (char*)rc_alloc(RC_TYPE_STRING, stem_len + elen + 1);
    memcpy(r, path, stem_len);
    memcpy(r + stem_len, ext, elen + 1);
    return r;
}

int64_t yona_Std_Path__isAbsolute(const char* path) {
    if (!path || !path[0]) return 0;
    if (yona_path_is_sep(path[0])) return 1;
#ifdef _WIN32
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':')
        return 1;
#endif
    return 0;
}

/* ===== Std\FloatMath — floating-point math (wrappers for math.h) ===== */

double yona_Std_FloatMath__sqrt(double x)  { return sqrt(x); }
double yona_Std_FloatMath__sin(double x)   { return sin(x); }
double yona_Std_FloatMath__cos(double x)   { return cos(x); }
double yona_Std_FloatMath__tan(double x)   { return tan(x); }
double yona_Std_FloatMath__log(double x)   { return log(x); }
double yona_Std_FloatMath__exp(double x)   { return exp(x); }
double yona_Std_FloatMath__floor(double x) { return floor(x); }
double yona_Std_FloatMath__ceil(double x)  { return ceil(x); }
double yona_Std_FloatMath__round(double x) { return round(x); }
double yona_Std_FloatMath__pi(void)        { return 3.14159265358979323846; }

/* ===== Std\Format — string formatting ===== */

/* Simple placeholder format: replace {} with arguments in order.
 * Takes a format string and a sequence of string arguments. */
const char* yona_Std_Format__format(const char* fmt, int64_t* args) {
    int64_t argc = args[0];
    int64_t argi = 0;
    size_t flen = strlen(fmt);

    /* First pass: calculate output size */
    size_t out_size = 0;
    for (size_t i = 0; i < flen; i++) {
        if (fmt[i] == '{' && i + 1 < flen && fmt[i+1] == '}' && argi < argc) {
            const char* arg = (const char*)(intptr_t)args[argi + 1];
            out_size += strlen(arg);
            argi++;
            i++; /* skip } */
        } else {
            out_size++;
        }
    }

    /* Second pass: build output */
    char* r = (char*)rc_alloc(RC_TYPE_STRING, out_size + 1);
    argi = 0;
    size_t j = 0;
    for (size_t i = 0; i < flen; i++) {
        if (fmt[i] == '{' && i + 1 < flen && fmt[i+1] == '}' && argi < argc) {
            const char* arg = (const char*)(intptr_t)args[argi + 1];
            size_t alen = strlen(arg);
            memcpy(r + j, arg, alen);
            j += alen;
            argi++;
            i++;
        } else {
            r[j++] = fmt[i];
        }
    }
    r[j] = '\0';
    return r;
}

/* ===== Std\Json — recursive ADT parse/stringify (see runtime/json.c) ===== */
#include "runtime/json.c"

/* ===== Std\Crypto — hashing and random bytes ===== */

/* SHA-256 implementation (standalone, no openssl dependency) */
static const uint32_t sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define SHA256_ROTR(x,n) (((x) >> (n)) | ((x) << (32-(n))))
#define SHA256_CH(x,y,z)  (((x) & (y)) ^ (~(x) & (z)))
#define SHA256_MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHA256_EP0(x)  (SHA256_ROTR(x,2) ^ SHA256_ROTR(x,13) ^ SHA256_ROTR(x,22))
#define SHA256_EP1(x)  (SHA256_ROTR(x,6) ^ SHA256_ROTR(x,11) ^ SHA256_ROTR(x,25))
#define SHA256_SIG0(x) (SHA256_ROTR(x,7) ^ SHA256_ROTR(x,18) ^ ((x) >> 3))
#define SHA256_SIG1(x) (SHA256_ROTR(x,17) ^ SHA256_ROTR(x,19) ^ ((x) >> 10))

const char* yona_Std_Crypto__sha256(const char* input) {
    size_t len = strlen(input);
    /* Padding */
    size_t new_len = ((len + 8) / 64 + 1) * 64;
    uint8_t* msg = (uint8_t*)calloc(new_len, 1);
    memcpy(msg, input, len);
    msg[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) msg[new_len - 1 - i] = (uint8_t)(bits >> (i * 8));

    uint32_t h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    for (size_t chunk = 0; chunk < new_len; chunk += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)msg[chunk+i*4]<<24) | ((uint32_t)msg[chunk+i*4+1]<<16) |
                   ((uint32_t)msg[chunk+i*4+2]<<8) | (uint32_t)msg[chunk+i*4+3];
        for (int i = 16; i < 64; i++)
            w[i] = SHA256_SIG1(w[i-2]) + w[i-7] + SHA256_SIG0(w[i-15]) + w[i-16];

        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t t1 = hh + SHA256_EP1(e) + SHA256_CH(e,f,g) + sha256_k[i] + w[i];
            uint32_t t2 = SHA256_EP0(a) + SHA256_MAJ(a,b,c);
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    free(msg);

    char* r = (char*)rc_alloc(RC_TYPE_STRING, 65);
    for (int i = 0; i < 8; i++)
        snprintf(r + i*8, 9, "%08x", h[i]);
    return r;
}

const char* yona_Std_Crypto__randomBytes(int64_t n) {
    char* r = (char*)rc_alloc(RC_TYPE_STRING, (size_t)n + 1);
    FILE* f = fopen("/dev/urandom", "rb");
    if (f) { fread(r, 1, (size_t)n, f); fclose(f); }
    r[n] = '\0';
    return r;
}

const char* yona_Std_Crypto__randomHex(int64_t n) {
    char* bytes = (char*)malloc((size_t)n);
    FILE* f = fopen("/dev/urandom", "rb");
    if (f) { fread(bytes, 1, (size_t)n, f); fclose(f); }
    char* r = (char*)rc_alloc(RC_TYPE_STRING, (size_t)n * 2 + 1);
    static const char hex[] = "0123456789abcdef";
    for (int64_t i = 0; i < n; i++) {
        r[i*2]   = hex[((uint8_t)bytes[i] >> 4) & 0xF];
        r[i*2+1] = hex[(uint8_t)bytes[i] & 0xF];
    }
    r[n*2] = '\0';
    free(bytes);
    return r;
}

const char* yona_Std_Crypto__uuid4(void) {
    uint8_t bytes[16];
    FILE* f = fopen("/dev/urandom", "rb");
    if (f) { fread(bytes, 1, 16, f); fclose(f); }
    bytes[6] = (bytes[6] & 0x0f) | 0x40; /* version 4 */
    bytes[8] = (bytes[8] & 0x3f) | 0x80; /* variant 1 */
    char* r = (char*)rc_alloc(RC_TYPE_STRING, 37);
    snprintf(r, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0],bytes[1],bytes[2],bytes[3],bytes[4],bytes[5],bytes[6],bytes[7],
        bytes[8],bytes[9],bytes[10],bytes[11],bytes[12],bytes[13],bytes[14],bytes[15]);
    return r;
}

/* ===== Std\Log — structured logging ===== */

static int64_t yona_log_level = 1; /* 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR */
static const char* yona_log_level_names[] = {"DEBUG", "INFO", "WARN", "ERROR"};

void yona_Std_Log__setLevel(int64_t level) { yona_log_level = level; }
int64_t yona_Std_Log__getLevel(void) { return yona_log_level; }

static void yona_log_emit(int64_t level, const char* msg) {
    if (level < yona_log_level) return;
    time_t now = time(NULL);
    struct tm* tm = localtime(&now);
    char ts[20];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);
    fprintf(stderr, "[%s] %s: %s\n", ts, yona_log_level_names[level], msg);
    fflush(stderr);
}

void yona_Std_Log__debug(const char* msg) { yona_log_emit(0, msg); }
void yona_Std_Log__info(const char* msg)  { yona_log_emit(1, msg); }
void yona_Std_Log__warn(const char* msg)  { yona_log_emit(2, msg); }
void yona_Std_Log__error(const char* msg) { yona_log_emit(3, msg); }

/* Std\List */
int64_t yona_Std_List__length(int64_t* seq) { return yona_rt_seq_length(seq); }
int64_t yona_Std_List__head(int64_t* seq) { return yona_rt_seq_head(seq); }
int64_t* yona_Std_List__tail(int64_t* seq) { return yona_rt_seq_tail(seq); }
int64_t* yona_Std_List__reverse(int64_t* seq) {
    int64_t len = yona_rt_seq_length(seq);
    int64_t* r = yona_rt_seq_alloc(len);
    for (int64_t i = 0; i < len; i++) r[i + 1] = yona_rt_seq_get(seq, len - 1 - i);
    return r;
}

/* C loop-based foldl/foldr — no stack overflow on large sequences.
 * fn is a closure with arity 2: fn(acc, elem) -> new_acc
 * collection can be Seq (RC_TYPE_SEQ) or Iterator (RC_TYPE_ADT).
 * Detection via RC type tag in header[1] & 0xFF. */
typedef int64_t (*fold_fn_t)(int64_t* env, int64_t, int64_t);

/* Fold over an Iterator: call next() in a loop until None */
static int64_t foldl_iterator(fold_fn_t f, int64_t* fn, int64_t acc, int64_t* iter_adt) {
    /* Iterator ADT layout: [tag, num_fields, heap_mask, closure_ptr] */
    int64_t* closure = (int64_t*)(intptr_t)iter_adt[ADT_HDR_SIZE];
    typedef int64_t (*next_fn_t)(int64_t*);
    next_fn_t next = (next_fn_t)(intptr_t)closure[0];

    while (1) {
        int64_t option = next(closure);
        int64_t* opt_ptr = (int64_t*)(intptr_t)option;
        /* Option ADT layout: [tag, num_fields, heap_mask, field0] */
        if (opt_ptr[0] != 0) {
            yona_rt_rc_dec(opt_ptr);  /* free None */
            break;
        }
        int64_t elem = opt_ptr[ADT_HDR_SIZE];  /* field0 */
        yona_rt_rc_dec(opt_ptr);  /* free Some wrapper (heap_mask=0, doesn't touch elem) */
        acc = f(fn, acc, elem);
    }
    return acc;
}

/* Fold over a Seq: head/tail loop */
static int64_t foldl_seq(fold_fn_t f, int64_t* fn, int64_t acc, int64_t* seq) {
    while (yona_rt_seq_length(seq) > 0) {
        int64_t elem = yona_rt_seq_head(seq);
        acc = f(fn, acc, elem);
        seq = yona_rt_seq_tail(seq);
    }
    return acc;
}

/* Polymorphic foldl: works on Seq, Iterator, ByteArray, IntArray, FloatArray, String.
 * Detects collection type via RC type tag and dispatches to specialized foldl. */
int64_t yona_Std_List__foldl(int64_t* fn, int64_t acc, int64_t* collection) {
    fold_fn_t f = (fold_fn_t)(intptr_t)fn[0];
    /* Check RC type tag: header is at ptr - RC_HEADER_SIZE */
    int64_t* header = collection - RC_HEADER_SIZE;
    int64_t type_tag = header[1] & 0xFF;
    if (type_tag == RC_TYPE_ADT)
        return foldl_iterator(f, fn, acc, collection);
    if (type_tag == RC_TYPE_INT_ARRAY)
        return yona_rt_int_array_foldl(fn, acc, collection);
    if (type_tag == RC_TYPE_FLOAT_ARRAY) {
        /* FloatArray uses double; we need to convert through int64 representation.
         * Cast i64 acc to double via memcpy, fold, cast back. */
        double dacc;
        memcpy(&dacc, &acc, sizeof(double));
        double dresult = yona_rt_float_array_foldl(fn, dacc, (double*)collection);
        int64_t iresult;
        memcpy(&iresult, &dresult, sizeof(double));
        return iresult;
    }
    if (type_tag == RC_TYPE_BYTE_ARRAY) {
        int64_t len = yona_rt_byte_array_length(collection);
        uint8_t* data = (uint8_t*)(collection + 1);
        for (int64_t i = 0; i < len; i++)
            acc = f(fn, acc, (int64_t)data[i]);
        return acc;
    }
    if (type_tag == RC_TYPE_STRING) {
        const char* s = (const char*)collection;
        for (; *s; s++)
            acc = f(fn, acc, (int64_t)(unsigned char)*s);
        return acc;
    }
    return foldl_seq(f, fn, acc, collection);
}

int64_t yona_Std_List__fold(int64_t* fn, int64_t acc, int64_t* collection) {
    return yona_Std_List__foldl(fn, acc, collection);
}

int64_t yona_Std_List__foldr(int64_t* fn, int64_t acc, int64_t* seq) {
    fold_fn_t f = (fold_fn_t)(intptr_t)fn[0];
    int64_t len = yona_rt_seq_length(seq);
    for (int64_t i = len - 1; i >= 0; i--) {
        int64_t elem = yona_rt_seq_get(seq, i);
        acc = f(fn, elem, acc);
    }
    return acc;
}

/* C loop-based map and filter */
int64_t* yona_Std_List__map(int64_t* fn, int64_t* seq) {
    typedef int64_t (*map_fn_t)(int64_t* env, int64_t);
    map_fn_t f = (map_fn_t)(intptr_t)fn[0];
    int64_t len = yona_rt_seq_length(seq);
    int64_t* result = yona_rt_seq_alloc(len);
    for (int64_t i = 0; i < len; i++) {
        int64_t elem = yona_rt_seq_get(seq, i);
        result[i + 1] = f(fn, elem);
    }
    return result;
}

int64_t* yona_Std_List__filter(int64_t* fn, int64_t* seq) {
    typedef int64_t (*pred_fn_t)(int64_t* env, int64_t);
    pred_fn_t f = (pred_fn_t)(intptr_t)fn[0];
    int64_t len = yona_rt_seq_length(seq);
    int64_t* result = yona_rt_seq_alloc(len);  /* over-allocate */
    int64_t count = 0;
    for (int64_t i = 0; i < len; i++) {
        int64_t elem = yona_rt_seq_get(seq, i);
        if (f(fn, elem)) {
            result[count + 1] = elem;
            count++;
        }
    }
    result[0] = count;  /* actual count */
    return result;
}

int64_t yona_Std_List__sum(int64_t* seq) {
    int64_t total = 0;
    int64_t len = yona_rt_seq_length(seq);
    for (int64_t i = 0; i < len; i++) total += yona_rt_seq_get(seq, i);
    return total;
}

int64_t yona_Std_List__product(int64_t* seq) {
    int64_t total = 1;
    int64_t len = yona_rt_seq_length(seq);
    for (int64_t i = 0; i < len; i++) total *= yona_rt_seq_get(seq, i);
    return total;
}

/* Prelude aliases — always available without import */
int64_t yona_Prelude__foldl(int64_t* fn, int64_t acc, int64_t* seq) {
    return yona_Std_List__foldl(fn, acc, seq);
}
int64_t yona_Prelude__foldr(int64_t* fn, int64_t acc, int64_t* seq) {
    return yona_Std_List__foldr(fn, acc, seq);
}

/* Std\Types — type conversions */
int64_t yona_Std_Types__toInt(const char* s) { return (int64_t)atoll(s); }
double yona_Std_Types__toFloat(const char* s) { return atof(s); }
const char* yona_Std_Types__intToString(int64_t n) {
    char* r = (char*)rc_alloc(RC_TYPE_STRING, 32);
    snprintf(r, 32, "%" PRId64, n);
    return r;
}
const char* yona_Std_Types__floatToString(double f) {
    char* r = (char*)rc_alloc(RC_TYPE_STRING, 32);
    snprintf(r, 32, "%g", f);
    return r;
}
const char* yona_Std_Types__boolToString(int64_t b) {
    const char* src = b ? "true" : "false";
    size_t len = strlen(src);
    char* r = (char*)rc_alloc(RC_TYPE_STRING, len + 1);
    memcpy(r, src, len + 1);
    return r;
}

/* ===== Primitive trait instances (Show, Eq, Ord, Hash) ===== */

const char* yona_Prelude__Show_Int__show(int64_t n) {
    char* r = (char*)rc_alloc(RC_TYPE_STRING, 32);
    snprintf(r, 32, "%" PRId64, n);
    return r;
}

const char* yona_Prelude__Show_String__show(const char* s) {
    /* Show for strings wraps in quotes: "hello" -> "\"hello\"" */
    size_t len = strlen(s);
    char* r = (char*)rc_alloc(RC_TYPE_STRING, len + 3);
    r[0] = '"';
    memcpy(r + 1, s, len);
    r[len + 1] = '"';
    r[len + 2] = '\0';
    return r;
}

const char* yona_Prelude__Show_Bool__show(int64_t b) {
    const char* src = b ? "true" : "false";
    size_t len = strlen(src);
    char* r = (char*)rc_alloc(RC_TYPE_STRING, len + 1);
    memcpy(r, src, len + 1);
    return r;
}

const char* yona_Prelude__Show_Float__show(double f) {
    char* r = (char*)rc_alloc(RC_TYPE_STRING, 32);
    snprintf(r, 32, "%g", f);
    return r;
}

int64_t yona_Prelude__Eq_Int__eq(int64_t a, int64_t b) { return a == b ? 1 : 0; }

int64_t yona_Prelude__Eq_String__eq(const char* a, const char* b) {
    return strcmp(a, b) == 0 ? 1 : 0;
}

int64_t yona_Prelude__Eq_Bool__eq(int64_t a, int64_t b) { return a == b ? 1 : 0; }

int64_t yona_Prelude__Ord_Int__compare(int64_t a, int64_t b) {
    return (a < b) ? -1 : (a > b) ? 1 : 0;
}

int64_t yona_Prelude__Hash_Int__hash(int64_t x) {
    /* splitmix64 finalizer */
    uint64_t h = (uint64_t)x;
    h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ULL;
    h = (h ^ (h >> 27)) * 0x94d049bb133111ebULL;
    return (int64_t)(h ^ (h >> 31));
}

int64_t yona_Prelude__Hash_String__hash(const char* s) {
    /* FNV-1a */
    uint64_t h = 14695981039346656037ULL;
    for (; *s; s++)
        h = (h ^ (uint64_t)(unsigned char)*s) * 1099511628211ULL;
    return (int64_t)h;
}

/* Float instances */
int64_t yona_Prelude__Eq_Float__eq(double a, double b) { return a == b ? 1 : 0; }
int64_t yona_Prelude__Ord_Float__compare(double a, double b) {
    return (a < b) ? -1 : (a > b) ? 1 : 0;
}
int64_t yona_Prelude__Hash_Float__hash(double f) {
    uint64_t bits;
    memcpy(&bits, &f, sizeof(bits));
    bits = (bits ^ (bits >> 30)) * 0xbf58476d1ce4e5b9ULL;
    bits = (bits ^ (bits >> 27)) * 0x94d049bb133111ebULL;
    return (int64_t)(bits ^ (bits >> 31));
}

/* Bool instances */
int64_t yona_Prelude__Ord_Bool__compare(int64_t a, int64_t b) {
    return (a < b) ? -1 : (a > b) ? 1 : 0;
}
int64_t yona_Prelude__Hash_Bool__hash(int64_t b) { return b ? 1 : 0; }

/* String instances */
int64_t yona_Prelude__Ord_String__compare(const char* a, const char* b) {
    int r = strcmp(a, b);
    return (r < 0) ? -1 : (r > 0) ? 1 : 0;
}

/* Symbol instances */
const char* yona_Prelude__Show_Symbol__show(int64_t sym_id) {
    char* r = (char*)rc_alloc(RC_TYPE_STRING, 32);
    snprintf(r, 32, ":%" PRId64, sym_id);
    return r;
}
int64_t yona_Prelude__Eq_Symbol__eq(int64_t a, int64_t b) { return a == b ? 1 : 0; }
int64_t yona_Prelude__Hash_Symbol__hash(int64_t s) { return s; }

/* ===== Array trait instance wrappers ===== */

int64_t yona_Prelude__Array_ByteArray__length(int64_t arr) {
    return yona_rt_byte_array_length((void*)(intptr_t)arr);
}
int64_t yona_Prelude__Array_ByteArray__get(int64_t arr, int64_t i) {
    return yona_rt_byte_array_get((void*)(intptr_t)arr, i);
}
int64_t yona_Prelude__Array_IntArray__length(int64_t arr) {
    return yona_rt_int_array_length((int64_t*)(intptr_t)arr);
}
int64_t yona_Prelude__Array_IntArray__get(int64_t arr, int64_t i) {
    return yona_rt_int_array_get((int64_t*)(intptr_t)arr, i);
}
int64_t yona_Prelude__Array_FloatArray__length(int64_t arr) {
    return (int64_t)yona_rt_float_array_length((double*)(intptr_t)arr);
}
double yona_Prelude__Array_FloatArray__get(int64_t arr, int64_t i) {
    return yona_rt_float_array_get((double*)(intptr_t)arr, i);
}
int64_t yona_Prelude__Array_Seq__length(int64_t arr) {
    extern int64_t yona_rt_seq_length(int64_t* seq);
    return yona_rt_seq_length((int64_t*)(intptr_t)arr);
}
int64_t yona_Prelude__Array_Seq__get(int64_t arr, int64_t i) {
    extern int64_t yona_rt_seq_get(int64_t* seq, int64_t index);
    return yona_rt_seq_get((int64_t*)(intptr_t)arr, i);
}
int64_t yona_Prelude__Array_String__length(int64_t arr) {
    extern int64_t yona_Std_String__length(const char* s);
    return yona_Std_String__length((const char*)(intptr_t)arr);
}
int64_t yona_Prelude__Array_String__get(int64_t arr, int64_t i) {
    const char* s = (const char*)(intptr_t)arr;
    return (int64_t)(unsigned char)s[i];
}

/* seq_head and seq_tail are in runtime/seq.c */

/* Async runtime: thread pool, promises, await */
#if defined(_WIN32)
#include "runtime/platform/async_win32.c"
#else
#include "runtime/platform/async_posix.c"
#endif

/* Channels: bounded MPMC for inter-task communication */
#if defined(_WIN32)
#include "runtime/platform/channel_win32.c"
#else
#include "runtime/platform/channel_posix.c"
#endif

/* Closures: partial application, env-passing convention */
#include "runtime/closures.c"

/* UTF-8 ↔ LSP UTF-16 positions (documented C ABI + Std\Utf16) */
#include "runtime/utf16.c"
