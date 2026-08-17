/*
 * Single io_uring instance + io_ctx table for all Linux platform TUs.
 */

#include "yona/runtime/uring.h"

#include <stdatomic.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

static inline int yona_uring_setup(unsigned entries, struct io_uring_params *p) {
    return (int)syscall(__NR_io_uring_setup, entries, p);
}

static inline int yona_uring_enter(int ring_fd, unsigned to_submit, unsigned min_complete,
                                   unsigned flags) {
    return (int)syscall(__NR_io_uring_enter, ring_fd, to_submit, min_complete, flags, NULL, 0);
}

typedef struct {
    int ring_fd;
    unsigned *sq_head, *sq_tail, *sq_ring_mask, *sq_ring_entries;
    unsigned *sq_array;
    struct io_uring_sqe *sqes;
    void *sq_ring_ptr;
    size_t sq_ring_sz;
    unsigned *cq_head, *cq_tail, *cq_ring_mask, *cq_ring_entries;
    struct io_uring_cqe *cqes;
    void *cq_ring_ptr;
    size_t cq_ring_sz;
    atomic_uint_fast64_t next_id;
    int initialized;
} yona_ring_t;

static yona_ring_t yona_ring = {0};
static pthread_mutex_t yona_ring_mutex = PTHREAD_MUTEX_INITIALIZER;

static int yona_ring_init(void) {
    if (yona_ring.initialized) return 0;

    struct io_uring_params params;
    memset(&params, 0, sizeof(params));

    int fd = yona_uring_setup(256, &params);
    if (fd < 0) return -1;

    yona_ring.ring_fd = fd;

    size_t sq_ring_sz = params.sq_off.array + params.sq_entries * sizeof(unsigned);
    void *sq_ptr = mmap(0, sq_ring_sz, PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQ_RING);
    if (sq_ptr == MAP_FAILED) { close(fd); return -1; }

    yona_ring.sq_ring_ptr = sq_ptr;
    yona_ring.sq_ring_sz = sq_ring_sz;
    yona_ring.sq_head = sq_ptr + params.sq_off.head;
    yona_ring.sq_tail = sq_ptr + params.sq_off.tail;
    yona_ring.sq_ring_mask = sq_ptr + params.sq_off.ring_mask;
    yona_ring.sq_ring_entries = sq_ptr + params.sq_off.ring_entries;
    yona_ring.sq_array = sq_ptr + params.sq_off.array;

    size_t sqes_sz = params.sq_entries * sizeof(struct io_uring_sqe);
    yona_ring.sqes = mmap(0, sqes_sz, PROT_READ | PROT_WRITE,
                          MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQES);
    if (yona_ring.sqes == MAP_FAILED) { munmap(sq_ptr, sq_ring_sz); close(fd); return -1; }

    size_t cq_ring_sz = params.cq_off.cqes + params.cq_entries * sizeof(struct io_uring_cqe);
    void *cq_ptr;
    if (params.features & IORING_FEAT_SINGLE_MMAP) {
        cq_ptr = sq_ptr;
    } else {
        cq_ptr = mmap(0, cq_ring_sz, PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_CQ_RING);
        if (cq_ptr == MAP_FAILED) { munmap(yona_ring.sqes, sqes_sz); munmap(sq_ptr, sq_ring_sz); close(fd); return -1; }
    }

    yona_ring.cq_ring_ptr = cq_ptr;
    yona_ring.cq_ring_sz = cq_ring_sz;
    yona_ring.cq_head = cq_ptr + params.cq_off.head;
    yona_ring.cq_tail = cq_ptr + params.cq_off.tail;
    yona_ring.cq_ring_mask = cq_ptr + params.cq_off.ring_mask;
    yona_ring.cq_ring_entries = cq_ptr + params.cq_off.ring_entries;
    yona_ring.cqes = cq_ptr + params.cq_off.cqes;

    atomic_init(&yona_ring.next_id, 1);
    yona_ring.initialized = 1;
    return 0;
}

uint64_t ring_submit_sqe(struct io_uring_sqe *sqe_template) {
    pthread_mutex_lock(&yona_ring_mutex);
    if (!yona_ring.initialized && yona_ring_init() != 0) {
        pthread_mutex_unlock(&yona_ring_mutex);
        return 0;
    }
    unsigned tail = *yona_ring.sq_tail;
    unsigned mask = *yona_ring.sq_ring_mask;
    unsigned idx = tail & mask;
    uint64_t id = atomic_fetch_add(&yona_ring.next_id, 1);
    struct io_uring_sqe *sqe = &yona_ring.sqes[idx];
    *sqe = *sqe_template;
    sqe->user_data = id;
    yona_ring.sq_array[idx] = idx;
    __atomic_store_n(yona_ring.sq_tail, tail + 1, __ATOMIC_RELEASE);
    yona_uring_enter(yona_ring.ring_fd, 1, 0, 0);
    pthread_mutex_unlock(&yona_ring_mutex);
    return id;
}

/* Completions for other in-flight ids must be stashed; otherwise awaiting
 * accept while connect finished first would skip/drop the connect CQE. */
#define RING_PENDING_MAX 256
static struct {
    uint64_t id;
    int32_t res;
    int used;
} ring_pending[RING_PENDING_MAX];

static int pending_put(uint64_t id, int32_t res) {
    for (int i = 0; i < RING_PENDING_MAX; i++) {
        if (!ring_pending[i].used) {
            ring_pending[i].id = id;
            ring_pending[i].res = res;
            ring_pending[i].used = 1;
            return 0;
        }
    }
    return -1;
}

static int pending_take(uint64_t id, int32_t *out) {
    for (int i = 0; i < RING_PENDING_MAX; i++) {
        if (ring_pending[i].used && ring_pending[i].id == id) {
            *out = ring_pending[i].res;
            ring_pending[i].used = 0;
            return 1;
        }
    }
    return 0;
}

int32_t ring_await(uint64_t id) {
    for (;;) {
        pthread_mutex_lock(&yona_ring_mutex);
        if (!yona_ring.initialized) {
            pthread_mutex_unlock(&yona_ring_mutex);
            return -1;
        }
        int32_t stashed = 0;
        if (pending_take(id, &stashed)) {
            pthread_mutex_unlock(&yona_ring_mutex);
            return stashed;
        }
        unsigned head = __atomic_load_n(yona_ring.cq_head, __ATOMIC_ACQUIRE);
        unsigned tail = __atomic_load_n(yona_ring.cq_tail, __ATOMIC_ACQUIRE);
        unsigned consumed = 0;
        int32_t found_res = 0;
        int found = 0;
        while (head != tail) {
            unsigned mask = *yona_ring.cq_ring_mask;
            struct io_uring_cqe *cqe = &yona_ring.cqes[head & mask];
            if (cqe->user_data == id) {
                found_res = cqe->res;
                found = 1;
            } else {
                (void)pending_put(cqe->user_data, cqe->res);
            }
            head++;
            consumed++;
        }
        if (consumed)
            __atomic_store_n(yona_ring.cq_head,
                __atomic_load_n(yona_ring.cq_head, __ATOMIC_RELAXED) + consumed,
                __ATOMIC_RELEASE);
        pthread_mutex_unlock(&yona_ring_mutex);
        if (found)
            return found_res;
        yona_uring_enter(yona_ring.ring_fd, 0, 1, IORING_ENTER_GETEVENTS);
    }
}

void ring_cancel(uint64_t target_id) {
    if (!yona_ring.initialized) return;
    pthread_mutex_lock(&yona_ring_mutex);
    unsigned tail = *yona_ring.sq_tail;
    unsigned mask = *yona_ring.sq_ring_mask;
    unsigned idx = tail & mask;
    struct io_uring_sqe *sqe = &yona_ring.sqes[idx];
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_ASYNC_CANCEL;
    sqe->addr = target_id;
    sqe->user_data = atomic_fetch_add(&yona_ring.next_id, 1);
    yona_ring.sq_array[idx] = idx;
    __atomic_store_n(yona_ring.sq_tail, tail + 1, __ATOMIC_RELEASE);
    yona_uring_enter(yona_ring.ring_fd, 1, 0, 0);
    pthread_mutex_unlock(&yona_ring_mutex);
}

void ring_cancel_group_ios(uint64_t* io_ids, int count) {
    for (int i = 0; i < count; i++)
        ring_cancel(io_ids[i]);
}

static struct {
    uint64_t id;
    io_context_t* ctx;
} io_ctx_table[IO_CTX_TABLE_SIZE];

static pthread_mutex_t io_ctx_mutex = PTHREAD_MUTEX_INITIALIZER;

void io_ctx_put(uint64_t id, io_context_t* ctx) {
    pthread_mutex_lock(&io_ctx_mutex);
    unsigned idx = (unsigned)(id % IO_CTX_TABLE_SIZE);
    for (unsigned i = 0; i < IO_CTX_TABLE_SIZE; i++) {
        unsigned slot = (idx + i) % IO_CTX_TABLE_SIZE;
        if (io_ctx_table[slot].id == 0) {
            io_ctx_table[slot].id = id;
            io_ctx_table[slot].ctx = ctx;
            pthread_mutex_unlock(&io_ctx_mutex);
            return;
        }
    }
    pthread_mutex_unlock(&io_ctx_mutex);
}

io_context_t* io_ctx_take(uint64_t id) {
    pthread_mutex_lock(&io_ctx_mutex);
    unsigned idx = (unsigned)(id % IO_CTX_TABLE_SIZE);
    for (unsigned i = 0; i < IO_CTX_TABLE_SIZE; i++) {
        unsigned slot = (idx + i) % IO_CTX_TABLE_SIZE;
        if (io_ctx_table[slot].id == id) {
            io_context_t* ctx = io_ctx_table[slot].ctx;
            io_ctx_table[slot].id = 0;
            io_ctx_table[slot].ctx = NULL;
            pthread_mutex_unlock(&io_ctx_mutex);
            return ctx;
        }
        if (io_ctx_table[slot].id == 0) break;
    }
    pthread_mutex_unlock(&io_ctx_mutex);
    return NULL;
}
