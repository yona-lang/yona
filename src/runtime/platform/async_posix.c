/* ===== Async Runtime =====
 *
 * Fixed-size thread pool with work queue. Async functions submit tasks
 * to the pool and return a promise handle immediately (non-blocking).
 * Promises are awaited lazily at use sites via yona_rt_async_await.
 *
 * Structured concurrency: task groups track child promises. If one child
 * fails, siblings are cancelled (thread pool: skip execution; io_uring:
 * IORING_OP_ASYNC_CANCEL). Error propagated to parent via group_await_all.
 */

#include "yona/runtime/sjlj.h"
#include <pthread.h>
#include <unistd.h>

void yona_rt_arena_destroy(void* arena_ptr);

#define YONA_POOL_SIZE 8
#define YONA_POOL_MAX_THREADS 32
#define YONA_GROUP_INITIAL_CAP 8

/* Forward declarations for exception handling (exceptions.c) */
void* yona_rt_try_push(void);
void yona_rt_try_end(void);
void yona_rt_raise(int64_t symbol, const char* message);
int64_t yona_rt_get_exception_symbol(void);
const char* yona_rt_get_exception_message(void);

struct yona_promise {
    int64_t result;
    int completed;             /* accessed via __atomic builtins */
    int error;                 /* 1 if completed with error */
    pthread_mutex_t mutex;
    pthread_cond_t cond;
};
typedef struct yona_promise yona_promise_t;

typedef int64_t (*yona_async_fn_t)(int64_t);
typedef int64_t (*yona_thunk_fn_t)(void);

/* ===== Task Groups (Structured Concurrency) ===== */

typedef struct yona_task_group {
    int cancelled;       /* accessed via __atomic builtins */
    int pending_count;   /* accessed via __atomic builtins */
    /* Thread pool children */
    yona_promise_t** children;
    int child_count, child_cap;
    /* io_uring children */
    uint64_t* io_children;
    int io_child_count, io_child_cap;
    /* Error from first failing child */
    int64_t first_error_symbol;
    const char* first_error_msg;
    int has_error;
    /* Bump arena for structured-concurrency scope (parent thread only) */
    void* arena;
    /* Synchronization */
    pthread_mutex_t mutex;
    pthread_cond_t done_cond;
} yona_task_group_t;

yona_task_group_t* yona_rt_group_begin(void) {
    yona_task_group_t* g = (yona_task_group_t*)calloc(1, sizeof(yona_task_group_t));
    g->child_cap = YONA_GROUP_INITIAL_CAP;
    g->children = (yona_promise_t**)malloc(g->child_cap * sizeof(yona_promise_t*));
    g->io_child_cap = YONA_GROUP_INITIAL_CAP;
    g->io_children = (uint64_t*)malloc(g->io_child_cap * sizeof(uint64_t));
    pthread_mutex_init(&g->mutex, NULL);
    pthread_cond_init(&g->done_cond, NULL);
    return g;
}

void yona_rt_group_register(yona_task_group_t* g, yona_promise_t* p) {
    if (!g) return;
    pthread_mutex_lock(&g->mutex);
    if (g->child_count >= g->child_cap) {
        g->child_cap *= 2;
        g->children = (yona_promise_t**)realloc(g->children, g->child_cap * sizeof(yona_promise_t*));
    }
    g->children[g->child_count++] = p;
    __atomic_fetch_add(&g->pending_count, 1, __ATOMIC_SEQ_CST);
    pthread_mutex_unlock(&g->mutex);
}

void yona_rt_group_register_io(yona_task_group_t* g, uint64_t io_id) {
    if (!g) return;
    pthread_mutex_lock(&g->mutex);
    if (g->io_child_count >= g->io_child_cap) {
        g->io_child_cap *= 2;
        g->io_children = (uint64_t*)realloc(g->io_children, g->io_child_cap * sizeof(uint64_t));
    }
    g->io_children[g->io_child_count++] = io_id;
    __atomic_fetch_add(&g->pending_count, 1, __ATOMIC_SEQ_CST);
    pthread_mutex_unlock(&g->mutex);
}

/* Cancel: set flag + submit IORING_OP_ASYNC_CANCEL for io children */
void yona_rt_group_cancel(yona_task_group_t* g);  /* forward decl — implemented after uring include */

int yona_rt_group_is_cancelled(yona_task_group_t* g) {
    if (!g) return 0;
    return __atomic_load_n(&g->cancelled, __ATOMIC_SEQ_CST);
}

/* Await all children, then re-raise first error if any */
int64_t yona_rt_group_await_all(yona_task_group_t* g);  /* forward decl — needs async_await */

void yona_rt_group_attach_arena(yona_task_group_t* g, void* arena) {
    if (!g) return;
    g->arena = arena;
}

void yona_rt_group_detach_arena(void* g_ptr) {
    yona_task_group_t* g = (yona_task_group_t*)g_ptr;
    if (!g || !g->arena) return;
    yona_rt_arena_destroy(g->arena);
    g->arena = NULL;
}

static void yona_rt_promise_destroy(yona_promise_t* p) {
    if (!p) return;
    pthread_mutex_lock(&p->mutex);
    while (!p->completed)
        pthread_cond_wait(&p->cond, &p->mutex);
    pthread_mutex_unlock(&p->mutex);
    pthread_mutex_destroy(&p->mutex);
    pthread_cond_destroy(&p->cond);
    free(p);
}

void yona_rt_group_end(void* g_ptr) {
    yona_task_group_t* g = (yona_task_group_t*)g_ptr;
    if (!g) return;
    yona_rt_group_detach_arena(g);
    for (int i = 0; i < g->child_count; i++)
        yona_rt_promise_destroy(g->children[i]);
    pthread_mutex_destroy(&g->mutex);
    pthread_cond_destroy(&g->done_cond);
    free(g->children);
    free(g->io_children);
    free(g);
}

/* ===== Task Queue ===== */

typedef struct yona_task {
    yona_async_fn_t fn;     /* single-arg function (legacy) */
    yona_thunk_fn_t thunk;  /* zero-arg thunk (multi-arg via closure) */
    int64_t arg;
    yona_promise_t* promise;
    yona_task_group_t* group; /* owning group (NULL if ungrouped) */
    struct yona_task* next;
} yona_task_t;

/* Thread pool state */
static pthread_t yona_pool_threads[YONA_POOL_SIZE];
static yona_task_t* yona_task_head = NULL;
static yona_task_t* yona_task_tail = NULL;
static pthread_mutex_t yona_pool_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t yona_pool_cond = PTHREAD_COND_INITIALIZER;
static _Atomic int yona_pool_initialized = 0;

/* Channel liveness tracking.
 *
 * The fixed worker pool uses managed blocking: when a worker blocks on a
 * channel while queued work exists, the runtime may add a compensation worker.
 * Deadlock is reported only when every known worker task is blocked and no
 * queued task can make progress. This replaces the old timeout heuristic, so
 * slow producers are allowed to be slow without being misreported as deadlocks.
 */
static pthread_mutex_t yona_liveness_mutex = PTHREAD_MUTEX_INITIALIZER;
static int64_t yona_next_task_id = 1;
static int yona_worker_threads = 0;
static int yona_queued_tasks = 0;
static int yona_running_workers = 0;
static int yona_blocked_workers = 0;
static int yona_active_external_tasks = 0;
static int yona_external_waiters = 0;
static _Thread_local int64_t yona_current_task_id = 0;
static _Thread_local int yona_current_task_is_worker = 0;
static _Thread_local int yona_external_task_registered = 0;
static _Thread_local int yona_channel_wait_kind = 0; /* 1 worker, 2 external */
static _Thread_local int yona_deadlock_candidate_seen = 0;

static void* yona_pool_worker(void* unused);

static void start_pool_worker_unlocked(void) {
    pthread_t thread;
    if (pthread_create(&thread, NULL, yona_pool_worker, NULL) == 0) {
        pthread_detach(thread);
        yona_worker_threads++;
    }
}

static void maybe_spawn_compensation_worker_unlocked(void) {
    if (yona_queued_tasks <= 0) return;
    if (yona_worker_threads >= YONA_POOL_MAX_THREADS) return;
    if (yona_running_workers > 0) return;
    start_pool_worker_unlocked();
}

static void liveness_task_queued(void) {
    pthread_mutex_lock(&yona_liveness_mutex);
    yona_queued_tasks++;
    pthread_mutex_unlock(&yona_liveness_mutex);
}

static void liveness_register_external_task_unlocked(void) {
    if (yona_current_task_is_worker || yona_external_task_registered) return;
    yona_external_task_registered = 1;
    yona_current_task_id = yona_next_task_id++;
    yona_active_external_tasks++;
}

static void liveness_worker_begin(void) {
    pthread_mutex_lock(&yona_liveness_mutex);
    if (yona_queued_tasks > 0) yona_queued_tasks--;
    yona_running_workers++;
    yona_current_task_id = yona_next_task_id++;
    yona_current_task_is_worker = 1;
    pthread_mutex_unlock(&yona_liveness_mutex);
}

static void liveness_worker_end(void) {
    pthread_mutex_lock(&yona_liveness_mutex);
    if (yona_channel_wait_kind == 1) {
        if (yona_blocked_workers > 0) yona_blocked_workers--;
        yona_channel_wait_kind = 0;
    } else if (yona_current_task_is_worker && yona_running_workers > 0) {
        yona_running_workers--;
    }
    yona_current_task_id = 0;
    yona_current_task_is_worker = 0;
    pthread_mutex_unlock(&yona_liveness_mutex);
}

int yona_rt_channel_wait_begin(void* channel, int op, int64_t count, int64_t cap,
                               int closed, int opposite_waiters) {
    (void)channel;
    (void)op;
    (void)count;
    (void)cap;
    (void)closed;
    pthread_mutex_lock(&yona_liveness_mutex);
    if (yona_current_task_is_worker) {
        if (yona_running_workers > 0) yona_running_workers--;
        yona_blocked_workers++;
        yona_channel_wait_kind = 1;
        maybe_spawn_compensation_worker_unlocked();
    } else {
        liveness_register_external_task_unlocked();
        if (yona_active_external_tasks > 0) yona_active_external_tasks--;
        yona_external_waiters++;
        yona_channel_wait_kind = 2;
        maybe_spawn_compensation_worker_unlocked();
    }
    /* Blocked workers on *other* channels can still make progress (e.g. a
     * producer/consumer pair on a work channel while main waits on done).
     * opposite_waiters is only for this channel, so treat other blocked
     * tasks as live progress. */
    int other_blocked = yona_current_task_is_worker ? (yona_blocked_workers > 1)
                                                    : (yona_blocked_workers > 0);
    int deadlock_candidate = (yona_running_workers == 0 &&
                              yona_active_external_tasks == 0 &&
                              yona_queued_tasks == 0 &&
                              opposite_waiters <= 0 &&
                              !other_blocked);
    /* A condition-variable signal can make a waiter runnable before it has
     * returned from timedwait and restored its liveness state. Confirm the
     * quiescent state across one wait cycle before raising :Deadlock. */
    int deadlocked = deadlock_candidate && yona_deadlock_candidate_seen;
    yona_deadlock_candidate_seen = deadlock_candidate ? 1 : 0;
    pthread_mutex_unlock(&yona_liveness_mutex);
    return deadlocked;
}

void yona_rt_channel_wait_end(void) {
    pthread_mutex_lock(&yona_liveness_mutex);
    if (yona_channel_wait_kind == 1) {
        if (yona_blocked_workers > 0) yona_blocked_workers--;
        yona_running_workers++;
    } else if (yona_channel_wait_kind == 2) {
        if (yona_external_waiters > 0) yona_external_waiters--;
        yona_active_external_tasks++;
    }
    yona_channel_wait_kind = 0;
    pthread_mutex_unlock(&yona_liveness_mutex);
}

void yona_rt_promise_complete(yona_promise_t* p, int64_t result, int is_error,
                              yona_task_group_t* group) {
    pthread_mutex_lock(&p->mutex);
    p->result = result;
    p->error = is_error ? 1 : 0;
    p->completed = 1;
    pthread_cond_signal(&p->cond);
    pthread_mutex_unlock(&p->mutex);

    if (group) {
        if (__atomic_fetch_sub(&group->pending_count, 1, __ATOMIC_SEQ_CST) == 1) {
            pthread_cond_signal(&group->done_cond);
        }
    }
}

static void fulfill_promise(yona_task_t* task, int64_t result, int is_error) {
    yona_rt_promise_complete(task->promise, result, is_error, task->group);
}

static void* yona_pool_worker(void* unused) {
    (void)unused;
    while (1) {
        pthread_mutex_lock(&yona_pool_mutex);
        while (!yona_task_head) {
            pthread_cond_wait(&yona_pool_cond, &yona_pool_mutex);
        }
        yona_task_t* task = yona_task_head;
        yona_task_head = task->next;
        if (!yona_task_head) yona_task_tail = NULL;
        pthread_mutex_unlock(&yona_pool_mutex);

        liveness_worker_begin();

        /* Check cancellation before executing */
        if (task->group && __atomic_load_n(&task->group->cancelled, __ATOMIC_SEQ_CST)) {
            fulfill_promise(task, 0, 1);
            liveness_worker_end();
            free(task);
            continue;
        }

        /* Execute with error capture via yona_sjlj_setjmp (matches yona_rt_raise;
         * see exceptions.c for the SJLJ buffer rationale). */
        void* jmp = yona_rt_try_push();
        if (yona_sjlj_setjmp(jmp) == 0) {
            int64_t result = task->thunk ? task->thunk() : task->fn(task->arg);
            yona_rt_try_end();
            fulfill_promise(task, result, 0);
        } else {
            /* Task raised an exception — capture in group */
            if (task->group) {
                pthread_mutex_lock(&task->group->mutex);
                if (!task->group->has_error) {
                    task->group->first_error_symbol = yona_rt_get_exception_symbol();
                    task->group->first_error_msg = yona_rt_get_exception_message();
                    task->group->has_error = 1;
                }
                pthread_mutex_unlock(&task->group->mutex);
                /* Cancel siblings */
                yona_rt_group_cancel(task->group);
            }
            fulfill_promise(task, 0, 1);
        }

        liveness_worker_end();
        free(task);
    }
    return NULL;
}

static void yona_pool_init(void) {
    if (yona_pool_initialized) return;
    yona_pool_initialized = 1;
    pthread_mutex_lock(&yona_liveness_mutex);
    for (int i = 0; i < YONA_POOL_SIZE; i++) {
        (void)yona_pool_threads[i];
        start_pool_worker_unlocked();
    }
    pthread_mutex_unlock(&yona_liveness_mutex);
}

/* Generic async: takes a thunk (zero-arg function returning i64).
 * The codegen generates thunks that capture multi-arg function calls. */
typedef int64_t (*yona_thunk_t)(void);

static yona_promise_t* make_promise(void) {
    yona_promise_t* p = (yona_promise_t*)malloc(sizeof(yona_promise_t));
    p->result = 0;
    p->completed = 0;
    p->error = 0;
    pthread_mutex_init(&p->mutex, NULL);
    pthread_cond_init(&p->cond, NULL);
    return p;
}

yona_promise_t* yona_rt_promise_new(void) {
    return make_promise();
}

static void enqueue_task(yona_task_t* task) {
    pthread_mutex_lock(&yona_pool_mutex);
    if (yona_task_tail) {
        yona_task_tail->next = task;
    } else {
        yona_task_head = task;
    }
    yona_task_tail = task;
    pthread_cond_signal(&yona_pool_cond);
    pthread_mutex_unlock(&yona_pool_mutex);
}

static yona_promise_t* submit_task(yona_async_fn_t fn, yona_thunk_fn_t thunk,
                                    int64_t arg, yona_task_group_t* group) {
    yona_pool_init();
    yona_promise_t* promise = make_promise();

    yona_task_t* task = (yona_task_t*)calloc(1, sizeof(yona_task_t));
    task->fn = fn;
    task->thunk = thunk;
    task->arg = arg;
    task->promise = promise;
    task->group = group;

    if (group) yona_rt_group_register(group, promise);
    if (!yona_current_task_is_worker) {
        pthread_mutex_lock(&yona_liveness_mutex);
        liveness_register_external_task_unlocked();
        pthread_mutex_unlock(&yona_liveness_mutex);
    }
    liveness_task_queued();
    enqueue_task(task);
    return promise;
}

yona_promise_t* yona_rt_async_call_thunk(yona_thunk_t thunk) {
    return submit_task(NULL, thunk, 0, NULL);
}

/* Submit async work to thread pool. Returns promise immediately (non-blocking). */
yona_promise_t* yona_rt_async_call(yona_async_fn_t fn, int64_t arg) {
    return submit_task(fn, NULL, arg, NULL);
}

/* Grouped variants for structured concurrency */
yona_promise_t* yona_rt_async_call_thunk_grouped(yona_thunk_t thunk, yona_task_group_t* group) {
    return submit_task(NULL, thunk, 0, group);
}

/* Spawn a Yona closure as a task. The closure layout is:
 *   [fn_ptr, ret_tag, arity, num_caps, heap_mask, caps...]
 * The function pointer takes the closure (env) as its first argument.
 * For zero-arity closures (thunks), we call fn(closure). */
extern int64_t yona_rt_closure_apply_thunk(int64_t* closure);

static int64_t spawn_closure_dispatch(int64_t closure_int) {
    int64_t* closure = (int64_t*)(intptr_t)closure_int;
    /* Call the closure as a thunk: fn(closure) */
    typedef int64_t (*thunk_fn_t)(int64_t*);
    thunk_fn_t fn = (thunk_fn_t)(intptr_t)closure[0];
    return fn(closure);
}

yona_promise_t* yona_rt_async_spawn_closure(int64_t* closure, yona_task_group_t* group) {
    /* Submit using the dispatch wrapper. Pass closure as the int64 arg. */
    return submit_task((yona_async_fn_t)spawn_closure_dispatch,
                       NULL, (int64_t)(intptr_t)closure, group);
}

yona_promise_t* yona_rt_async_call_grouped(yona_async_fn_t fn, int64_t arg, yona_task_group_t* group) {
    return submit_task(fn, NULL, arg, group);
}

/* Await without freeing — grouped let/comprehension; yona_rt_group_end destroys. */
int64_t yona_rt_async_await_keep(yona_promise_t* promise) {
    pthread_mutex_lock(&promise->mutex);
    while (!promise->completed)
        pthread_cond_wait(&promise->cond, &promise->mutex);
    int64_t result = promise->result;
    pthread_mutex_unlock(&promise->mutex);
    return result;
}

/* Standalone async: await, return result, and free the promise. */
int64_t yona_rt_async_await(yona_promise_t* promise) {
    int64_t result = yona_rt_async_await_keep(promise);
    pthread_mutex_destroy(&promise->mutex);
    pthread_cond_destroy(&promise->cond);
    free(promise);
    return result;
}

/* ===== Task Group: Cancel & Await All ===== */

/* Cancel: set flag. io_uring cancellation is done externally by the codegen
 * or by calling ring_cancel() for each io child (see yona/runtime/uring.h). */
void yona_rt_group_cancel(yona_task_group_t* g) {
    if (!g) return;
    __atomic_store_n(&g->cancelled, 1, __ATOMIC_SEQ_CST);
    /* io_uring cancellation: handled by platform layer if yona/runtime/uring.h is included.
     * The compiled_runtime.c includes this file after platform TUs; ring_cancel
     * is available there. We provide a hook for the platform layer. */
}

/* Await all children in the group, then re-raise first error if any.
 * Promises stay allocated until yona_rt_group_end (after await_keep in body). */
int64_t yona_rt_group_await_all(yona_task_group_t* g) {
    if (!g) return 0;

    /* Wait for all thread-pool children to complete */
    for (int i = 0; i < g->child_count; i++) {
        yona_promise_t* p = g->children[i];
        pthread_mutex_lock(&p->mutex);
        while (!p->completed)
            pthread_cond_wait(&p->cond, &p->mutex);
        pthread_mutex_unlock(&p->mutex);
    }

    /* Re-raise first error on the caller's thread */
    if (g->has_error) {
        int64_t sym = g->first_error_symbol;
        const char* msg = g->first_error_msg;
        /* Don't end group here — let the codegen do it after cleanup */
        yona_rt_raise(sym, msg);
    }

    return 0;
}

/* Test helper: async function that sleeps for N milliseconds then returns N */
int64_t yona_test_slow_identity(int64_t ms) {
    usleep((useconds_t)(ms * 1000));
    return ms;
}

/* Test helper: multi-arg async function — adds two numbers with a delay */
int64_t yona_test_slow_add(int64_t a, int64_t b) {
    usleep(10000); /* 10ms */
    return a + b;
}

/* Returns a runtime promise completed before the caller can await — exercises
 * `extern native` (C ABI: yona_promise_t*). Result is `x * 7` (e.g. 6 -> 42). */
yona_promise_t* yona_test_native_promise_immediate(int64_t x) {
    yona_promise_t* p = yona_rt_promise_new();
    if (!p) return NULL;
    yona_rt_promise_complete(p, x * 7, 0, NULL);
    return p;
}
