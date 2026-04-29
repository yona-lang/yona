/* ===== Channel Runtime =====
 *
 * Bounded MPMC channel for inter-task communication.
 * Layout: [count, head, tail, cap, ring buffer of cap i64s].
 *
 * send blocks the calling worker thread when the buffer is full.
 * recv blocks when the buffer is empty.
 * close wakes all waiters; subsequent recv returns None when drained.
 *
 * Designed for use BETWEEN tasks. The async runtime tracks blocked channel
 * waiters and raises :Deadlock when no runnable task can make progress.
 */

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>

#define RC_TYPE_CHANNEL 20

extern void* rc_alloc(int64_t type_tag, size_t payload_bytes);
extern void yona_rt_rc_inc(void* p);
extern void yona_rt_rc_dec(void* p);
extern void yona_rt_raise(int64_t symbol, const char* message);
extern int yona_rt_channel_wait_begin(void* channel, int op, int64_t count,
                                      int64_t cap, int closed,
                                      int opposite_waiters);
extern void yona_rt_channel_wait_end(void);

/* Forward declaration of task group from async_posix.c */
struct yona_task_group;
typedef struct yona_task_group yona_task_group_t;
extern int yona_rt_group_is_cancelled(yona_task_group_t* g);

/* Symbol IDs for channel errors. These are conceptual — at runtime, sym 0 is
 * the generic error symbol. The compile-time intern_symbol gives different
 * IDs for these names but the runtime can't easily reference them without
 * compile-time knowledge. For now use sym 0 with descriptive messages. */
#define SYM_CANCELLED      0
#define SYM_DEADLOCK       0
#define SYM_CHANNEL_CLOSED 0

/* Channel struct.
 * Note: this struct is heap-allocated via rc_alloc, so the returned pointer
 * is offset by RC_HEADER_SIZE (2 i64s) from the actual allocation. */
typedef struct yona_channel {
    int64_t cap;
    int64_t count;
    int64_t head;
    int64_t tail;
    int64_t* buf;
    pthread_mutex_t mutex;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
    int closed;
    int waiters;             /* number of blocked send/recv */
    int send_waiters;
    int recv_waiters;
    yona_task_group_t* group; /* owning task group, for cancellation */
} yona_channel_t;

/* Allocate Option ADT for recv return.
 * Layout: [tag, num_fields, heap_mask, fields...] (recursive ADT layout) */
static int64_t* chan_make_some(int64_t value) {
    int64_t* adt = (int64_t*)rc_alloc(4 /* RC_TYPE_ADT */, 4 * sizeof(int64_t));
    adt[0] = 0; adt[1] = 1; adt[2] = 0;
    adt[3] = value;
    return adt;
}

static int64_t* chan_make_none(void) {
    int64_t* adt = (int64_t*)rc_alloc(4 /* RC_TYPE_ADT */, 3 * sizeof(int64_t));
    adt[0] = 1; adt[1] = 0; adt[2] = 0;
    return adt;
}

yona_channel_t* yona_rt_channel_new(int64_t cap) {
    if (cap < 1) cap = 1;
    yona_channel_t* ch = (yona_channel_t*)rc_alloc(RC_TYPE_CHANNEL, sizeof(yona_channel_t));
    ch->cap = cap;
    ch->count = 0;
    ch->head = 0;
    ch->tail = 0;
    ch->buf = (int64_t*)calloc((size_t)cap, sizeof(int64_t));
    pthread_mutex_init(&ch->mutex, NULL);
    pthread_cond_init(&ch->not_full, NULL);
    pthread_cond_init(&ch->not_empty, NULL);
    ch->closed = 0;
    ch->waiters = 0;
    ch->group = NULL;  /* TODO: track current task group for cancellation */
    return ch;
}

void yona_rt_channel_send(yona_channel_t* ch, int64_t value) {
    pthread_mutex_lock(&ch->mutex);
    while (ch->count == ch->cap && !ch->closed) {
        if (ch->group && yona_rt_group_is_cancelled(ch->group)) {
            pthread_mutex_unlock(&ch->mutex);
            yona_rt_raise(SYM_CANCELLED,
                          "task cancelled while waiting on channel send");
            return;
        }
        if (yona_rt_channel_wait_begin(ch, 1, ch->count, ch->cap, ch->closed,
                                       ch->recv_waiters)) {
            yona_rt_channel_wait_end();
            pthread_mutex_unlock(&ch->mutex);
            yona_rt_raise(SYM_DEADLOCK,
                          "channel deadlock: send waiting on full channel; no runnable tasks remain");
            return;
        }
        ch->waiters++;
        ch->send_waiters++;
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 100000000;  /* 100ms */
        if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
        pthread_cond_timedwait(&ch->not_full, &ch->mutex, &ts);
        ch->send_waiters--;
        ch->waiters--;
        yona_rt_channel_wait_end();
    }
    if (ch->closed) {
        pthread_mutex_unlock(&ch->mutex);
        yona_rt_raise(SYM_CHANNEL_CLOSED, "send on closed channel");
        return;
    }
    ch->buf[ch->tail] = value;
    ch->tail = (ch->tail + 1) % ch->cap;
    ch->count++;
    pthread_cond_signal(&ch->not_empty);
    pthread_mutex_unlock(&ch->mutex);
}

int64_t yona_rt_channel_recv(yona_channel_t* ch) {
    pthread_mutex_lock(&ch->mutex);
    while (ch->count == 0 && !ch->closed) {
        if (ch->group && yona_rt_group_is_cancelled(ch->group)) {
            pthread_mutex_unlock(&ch->mutex);
            yona_rt_raise(SYM_CANCELLED,
                          "task cancelled while waiting on channel recv");
            return 0;
        }
        if (yona_rt_channel_wait_begin(ch, 2, ch->count, ch->cap, ch->closed,
                                       ch->send_waiters)) {
            yona_rt_channel_wait_end();
            pthread_mutex_unlock(&ch->mutex);
            yona_rt_raise(SYM_DEADLOCK,
                          "channel deadlock: recv waiting on empty open channel; no runnable tasks remain");
            return 0;
        }
        ch->waiters++;
        ch->recv_waiters++;
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 100000000;  /* 100ms */
        if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
        pthread_cond_timedwait(&ch->not_empty, &ch->mutex, &ts);
        ch->recv_waiters--;
        ch->waiters--;
        yona_rt_channel_wait_end();
    }
    if (ch->count == 0 && ch->closed) {
        /* Channel closed and drained */
        pthread_mutex_unlock(&ch->mutex);
        return (int64_t)(intptr_t)chan_make_none();
    }
    int64_t value = ch->buf[ch->head];
    ch->head = (ch->head + 1) % ch->cap;
    ch->count--;
    pthread_cond_signal(&ch->not_full);
    pthread_mutex_unlock(&ch->mutex);
    return (int64_t)(intptr_t)chan_make_some(value);
}

int64_t yona_rt_channel_try_recv(yona_channel_t* ch) {
    pthread_mutex_lock(&ch->mutex);
    if (ch->count == 0) {
        pthread_mutex_unlock(&ch->mutex);
        return (int64_t)(intptr_t)chan_make_none();
    }
    int64_t value = ch->buf[ch->head];
    ch->head = (ch->head + 1) % ch->cap;
    ch->count--;
    pthread_cond_signal(&ch->not_full);
    pthread_mutex_unlock(&ch->mutex);
    return (int64_t)(intptr_t)chan_make_some(value);
}

void yona_rt_channel_close(yona_channel_t* ch) {
    pthread_mutex_lock(&ch->mutex);
    ch->closed = 1;
    pthread_cond_broadcast(&ch->not_full);
    pthread_cond_broadcast(&ch->not_empty);
    pthread_mutex_unlock(&ch->mutex);
}

int64_t yona_rt_channel_is_closed(yona_channel_t* ch) {
    pthread_mutex_lock(&ch->mutex);
    int closed = ch->closed;
    pthread_mutex_unlock(&ch->mutex);
    return closed ? 1 : 0;
}

int64_t yona_rt_channel_length(yona_channel_t* ch) {
    pthread_mutex_lock(&ch->mutex);
    int64_t n = ch->count;
    pthread_mutex_unlock(&ch->mutex);
    return n;
}

int64_t yona_rt_channel_capacity(yona_channel_t* ch) {
    return ch->cap;
}

/* Called from rc_dec when refcount hits 0. Takes void* to match dispatch. */
void yona_rt_channel_destroy(void* ptr) {
    yona_channel_t* ch = (yona_channel_t*)ptr;
    /* Wake any straggler waiters before destroying */
    pthread_mutex_lock(&ch->mutex);
    pthread_cond_broadcast(&ch->not_full);
    pthread_cond_broadcast(&ch->not_empty);
    pthread_mutex_unlock(&ch->mutex);
    pthread_mutex_destroy(&ch->mutex);
    pthread_cond_destroy(&ch->not_full);
    pthread_cond_destroy(&ch->not_empty);
    free(ch->buf);
    /* The channel struct itself is freed by rc_dec via the standard pool free */
}

/* ===== Std\Channel wrappers (i64-based for codegen) ===== */

int64_t yona_Std_Channel__channel(int64_t cap) {
    int64_t raw = (int64_t)(intptr_t)yona_rt_channel_new(cap);
    void* left = yona_rt_adt_alloc(0, 1);
    void* right = yona_rt_adt_alloc(0, 1);
    yona_rt_rc_inc((void*)(intptr_t)raw);
    yona_rt_rc_inc((void*)(intptr_t)raw);
    yona_rt_adt_set_field(left, 0, raw);
    yona_rt_adt_set_field(right, 0, raw);
    yona_rt_adt_set_heap_mask(left, 1);
    yona_rt_adt_set_heap_mask(right, 1);
    void* tuple = yona_rt_tuple_alloc(2);
    yona_rt_tuple_set(tuple, 0, (int64_t)(intptr_t)left);
    yona_rt_tuple_set(tuple, 1, (int64_t)(intptr_t)right);
    yona_rt_tuple_set_heap_mask(tuple, 3);
    return (int64_t)(intptr_t)tuple;
}

int64_t yona_Std_Channel__raw_new(int64_t cap) {
    return (int64_t)(intptr_t)yona_rt_channel_new(cap);
}

int64_t yona_Std_Channel__send(int64_t ch_i64, int64_t value) {
    yona_rt_channel_send((yona_channel_t*)(intptr_t)ch_i64, value);
    return 0;
}

int64_t yona_Std_Channel__raw_send(int64_t ch_i64, int64_t value) {
    return yona_Std_Channel__send(ch_i64, value);
}

int64_t yona_Std_Channel__recv(int64_t ch_i64) {
    return yona_rt_channel_recv((yona_channel_t*)(intptr_t)ch_i64);
}

int64_t yona_Std_Channel__raw_recv(int64_t ch_i64) {
    return yona_Std_Channel__recv(ch_i64);
}

int64_t yona_Std_Channel__tryRecv(int64_t ch_i64) {
    return yona_rt_channel_try_recv((yona_channel_t*)(intptr_t)ch_i64);
}

int64_t yona_Std_Channel__raw_tryRecv(int64_t ch_i64) {
    return yona_Std_Channel__tryRecv(ch_i64);
}

int64_t yona_Std_Channel__close(int64_t ch_i64) {
    yona_rt_channel_close((yona_channel_t*)(intptr_t)ch_i64);
    return 0;
}

int64_t yona_Std_Channel__raw_close(int64_t ch_i64) {
    return yona_Std_Channel__close(ch_i64);
}

int64_t yona_Std_Channel__isClosed(int64_t ch_i64) {
    return yona_rt_channel_is_closed((yona_channel_t*)(intptr_t)ch_i64);
}

int64_t yona_Std_Channel__raw_isClosed(int64_t ch_i64) {
    return yona_Std_Channel__isClosed(ch_i64);
}

int64_t yona_Std_Channel__length(int64_t ch_i64) {
    return yona_rt_channel_length((yona_channel_t*)(intptr_t)ch_i64);
}

int64_t yona_Std_Channel__raw_length(int64_t ch_i64) {
    return yona_Std_Channel__length(ch_i64);
}

int64_t yona_Std_Channel__capacity(int64_t ch_i64) {
    return yona_rt_channel_capacity((yona_channel_t*)(intptr_t)ch_i64);
}

int64_t yona_Std_Channel__raw_capacity(int64_t ch_i64) {
    return yona_Std_Channel__capacity(ch_i64);
}

/* ===== Std\Task — task spawning ===== */

/* yona_rt_async_spawn_closure is defined in async_posix.c which is #included before
 * channel_posix.c, so the function is already in scope. We use it directly without
 * a forward declaration. */

/* spawn: takes a Yona closure (zero-arity thunk), returns a promise.
 * The codegen treats this as IO so the result is auto-awaited at use site.
 * The closure runs on a thread pool worker concurrently with the caller. */
int64_t yona_Std_Task__spawn(int64_t* closure) {
    return (int64_t)(intptr_t)yona_rt_async_spawn_closure(closure, NULL);
}
