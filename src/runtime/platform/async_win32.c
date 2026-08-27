/* ===== Async Runtime (Windows native) =====
 *
 * Same semantics as async_posix.c: fixed-size thread pool, promises,
 * structured concurrency. Uses CRITICAL_SECTION + CONDITION_VARIABLE
 * instead of pthread.
 */

#ifndef _WIN32
#error "async_win32.c is for Windows builds only"
#endif

#include <stdint.h>
#include <stdlib.h>
#include <windows.h>

#include "yona/runtime/sjlj.h"

void yona_rt_arena_destroy(void* arena_ptr);

#define YONA_POOL_SIZE 8
#define YONA_POOL_MAX_THREADS 32
#define YONA_GROUP_INITIAL_CAP 8

void* yona_rt_try_push(void);
void yona_rt_try_end(void);
void yona_rt_raise(int64_t symbol, const char* message);
int64_t yona_rt_get_exception_symbol(void);
const char* yona_rt_get_exception_message(void);

typedef struct yona_promise {
	int64_t result;
	int completed;
	int error;
	CRITICAL_SECTION mutex;
	CONDITION_VARIABLE cond;
} yona_promise_t;

typedef int64_t (*yona_async_fn_t)(int64_t);
typedef int64_t (*yona_thunk_fn_t)(void);

typedef struct yona_task_group {
	int cancelled;
	int pending_count;
	yona_promise_t** children;
	int child_count, child_cap;
	uint64_t* io_children;
	int io_child_count, io_child_cap;
	int64_t first_error_symbol;
	const char* first_error_msg;
	int has_error;
	void* arena;
	CRITICAL_SECTION mutex;
	CONDITION_VARIABLE done_cond;
} yona_task_group_t;

yona_task_group_t* yona_rt_group_begin(void) {
	yona_task_group_t* g = (yona_task_group_t*)calloc(1, sizeof(yona_task_group_t));
	g->child_cap = YONA_GROUP_INITIAL_CAP;
	g->children = (yona_promise_t**)malloc(g->child_cap * sizeof(yona_promise_t*));
	g->io_child_cap = YONA_GROUP_INITIAL_CAP;
	g->io_children = (uint64_t*)malloc(g->io_child_cap * sizeof(uint64_t));
	InitializeCriticalSection(&g->mutex);
	InitializeConditionVariable(&g->done_cond);
	return g;
}

void yona_rt_group_register(yona_task_group_t* g, yona_promise_t* p) {
	if (!g) return;
	EnterCriticalSection(&g->mutex);
	if (g->child_count >= g->child_cap) {
		g->child_cap *= 2;
		g->children = (yona_promise_t**)realloc(g->children, g->child_cap * sizeof(yona_promise_t*));
	}
	g->children[g->child_count++] = p;
	(void)__atomic_fetch_add(&g->pending_count, 1, __ATOMIC_SEQ_CST);
	LeaveCriticalSection(&g->mutex);
}

void yona_rt_group_register_io(yona_task_group_t* g, uint64_t io_id) {
	if (!g) return;
	EnterCriticalSection(&g->mutex);
	if (g->io_child_count >= g->io_child_cap) {
		g->io_child_cap *= 2;
		g->io_children = (uint64_t*)realloc(g->io_children, g->io_child_cap * sizeof(uint64_t));
	}
	g->io_children[g->io_child_count++] = io_id;
	(void)__atomic_fetch_add(&g->pending_count, 1, __ATOMIC_SEQ_CST);
	LeaveCriticalSection(&g->mutex);
}

void yona_rt_group_cancel(yona_task_group_t* g);

int yona_rt_group_is_cancelled(yona_task_group_t* g) {
	if (!g) return 0;
	return __atomic_load_n(&g->cancelled, __ATOMIC_SEQ_CST);
}

int64_t yona_rt_group_await_all(yona_task_group_t* g);

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
	EnterCriticalSection(&p->mutex);
	while (!p->completed)
		SleepConditionVariableCS(&p->cond, &p->mutex, INFINITE);
	LeaveCriticalSection(&p->mutex);
	DeleteCriticalSection(&p->mutex);
	free(p);
}

void yona_rt_group_end(void* g_ptr) {
	yona_task_group_t* g = (yona_task_group_t*)g_ptr;
	if (!g) return;
	yona_rt_group_detach_arena(g);
	for (int i = 0; i < g->child_count; i++)
		yona_rt_promise_destroy(g->children[i]);
	DeleteCriticalSection(&g->mutex);
	free(g->children);
	free(g->io_children);
	free(g);
}

typedef struct yona_task {
	yona_async_fn_t fn;
	yona_thunk_fn_t thunk;
	int64_t arg;
	yona_promise_t* promise;
	yona_task_group_t* group;
	struct yona_task* next;
} yona_task_t;

static yona_task_t* yona_task_head = NULL;
static yona_task_t* yona_task_tail = NULL;
static CRITICAL_SECTION yona_pool_mutex;
static CONDITION_VARIABLE yona_pool_cond;
static INIT_ONCE yona_pool_init_once = INIT_ONCE_STATIC_INIT;

/* Channel liveness tracking and managed blocking. */
static CRITICAL_SECTION yona_liveness_mutex;
static INIT_ONCE yona_liveness_init_once = INIT_ONCE_STATIC_INIT;
static int64_t yona_next_task_id = 1;
static int yona_worker_threads = 0;
static int yona_queued_tasks = 0;
static int yona_running_workers = 0;
static int yona_blocked_workers = 0;
static int yona_active_external_tasks = 0;
static int yona_external_waiters = 0;
static __declspec(thread) int64_t yona_current_task_id = 0;
static __declspec(thread) int yona_current_task_is_worker = 0;
static __declspec(thread) int yona_external_task_registered = 0;
static __declspec(thread) int yona_channel_wait_kind = 0; /* 1 worker, 2 external */
static __declspec(thread) int yona_deadlock_candidate_seen = 0;

static DWORD WINAPI yona_pool_worker_win32(void* unused);

static BOOL CALLBACK yona_liveness_init_once_cb(PINIT_ONCE initOnce, PVOID parameter, PVOID* context) {
	(void)initOnce;
	(void)parameter;
	(void)context;
	InitializeCriticalSection(&yona_liveness_mutex);
	return TRUE;
}

static void liveness_init(void) {
	InitOnceExecuteOnce(&yona_liveness_init_once, yona_liveness_init_once_cb, NULL, NULL);
}

static void start_pool_worker_unlocked(void) {
	HANDLE h = CreateThread(NULL, 0, yona_pool_worker_win32, NULL, 0, NULL);
	if (h) {
		CloseHandle(h);
		yona_worker_threads++;
	}
}

static void maybe_spawn_compensation_worker_unlocked(void) {
	if (yona_queued_tasks <= 0) return;
	if (yona_worker_threads >= YONA_POOL_MAX_THREADS) return;
	if (yona_running_workers > 0) return;
	start_pool_worker_unlocked();
}

static void liveness_register_external_task_unlocked(void) {
	if (yona_current_task_is_worker || yona_external_task_registered) return;
	yona_external_task_registered = 1;
	yona_current_task_id = yona_next_task_id++;
	yona_active_external_tasks++;
}

static void liveness_task_queued(void) {
	liveness_init();
	EnterCriticalSection(&yona_liveness_mutex);
	yona_queued_tasks++;
	LeaveCriticalSection(&yona_liveness_mutex);
}

static void liveness_worker_begin(void) {
	liveness_init();
	EnterCriticalSection(&yona_liveness_mutex);
	if (yona_queued_tasks > 0) yona_queued_tasks--;
	yona_running_workers++;
	yona_current_task_id = yona_next_task_id++;
	yona_current_task_is_worker = 1;
	LeaveCriticalSection(&yona_liveness_mutex);
}

static void liveness_worker_end(void) {
	liveness_init();
	EnterCriticalSection(&yona_liveness_mutex);
	if (yona_channel_wait_kind == 1) {
		if (yona_blocked_workers > 0) yona_blocked_workers--;
		yona_channel_wait_kind = 0;
	} else if (yona_current_task_is_worker && yona_running_workers > 0) {
		yona_running_workers--;
	}
	yona_current_task_id = 0;
	yona_current_task_is_worker = 0;
	LeaveCriticalSection(&yona_liveness_mutex);
}

int yona_rt_channel_wait_begin(void* channel, int op, int64_t count, int64_t cap, int closed,
			       int opposite_waiters) {
	(void)channel;
	(void)op;
	(void)count;
	(void)cap;
	(void)closed;
	liveness_init();
	EnterCriticalSection(&yona_liveness_mutex);
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
	/* See async_posix.c: opposite_waiters is this channel only. */
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
	LeaveCriticalSection(&yona_liveness_mutex);
	return deadlocked;
}

void yona_rt_channel_wait_end(void) {
	liveness_init();
	EnterCriticalSection(&yona_liveness_mutex);
	if (yona_channel_wait_kind == 1) {
		if (yona_blocked_workers > 0) yona_blocked_workers--;
		yona_running_workers++;
	} else if (yona_channel_wait_kind == 2) {
		if (yona_external_waiters > 0) yona_external_waiters--;
		yona_active_external_tasks++;
	}
	yona_channel_wait_kind = 0;
	LeaveCriticalSection(&yona_liveness_mutex);
}

static void fulfill_promise(yona_task_t* task, int64_t result, int is_error) {
	EnterCriticalSection(&task->promise->mutex);
	task->promise->result = result;
	task->promise->error = is_error;
	task->promise->completed = 1;
	WakeConditionVariable(&task->promise->cond);
	LeaveCriticalSection(&task->promise->mutex);

	if (task->group) {
		if (__atomic_fetch_sub(&task->group->pending_count, 1, __ATOMIC_SEQ_CST) == 1)
			WakeConditionVariable(&task->group->done_cond);
	}
}

static DWORD WINAPI yona_pool_worker_win32(void* unused) {
	(void)unused;
	for (;;) {
		EnterCriticalSection(&yona_pool_mutex);
		while (!yona_task_head)
			SleepConditionVariableCS(&yona_pool_cond, &yona_pool_mutex, INFINITE);
		yona_task_t* task = yona_task_head;
		yona_task_head = task->next;
		if (!yona_task_head) yona_task_tail = NULL;
		LeaveCriticalSection(&yona_pool_mutex);

		liveness_worker_begin();

		if (task->group && __atomic_load_n(&task->group->cancelled, __ATOMIC_SEQ_CST)) {
			fulfill_promise(task, 0, 1);
			liveness_worker_end();
			free(task);
			continue;
		}

		/* This pairs with yona_rt_raise's SJLJ longjmp; see exceptions.c. The
		 * shared macro uses the target-compatible AArch64 implementation when
		 * Clang lacks its normal SJLJ builtin (native Windows ARM64). */
		void* jmp = yona_rt_try_push();
		if (yona_sjlj_setjmp(jmp) == 0) {
			int64_t result = task->thunk ? task->thunk() : task->fn(task->arg);
			yona_rt_try_end();
			fulfill_promise(task, result, 0);
		} else {
			if (task->group) {
				EnterCriticalSection(&task->group->mutex);
				if (!task->group->has_error) {
					task->group->first_error_symbol = yona_rt_get_exception_symbol();
					task->group->first_error_msg = yona_rt_get_exception_message();
					task->group->has_error = 1;
				}
				LeaveCriticalSection(&task->group->mutex);
				yona_rt_group_cancel(task->group);
			}
			fulfill_promise(task, 0, 1);
		}
		liveness_worker_end();
		free(task);
	}
}

static BOOL CALLBACK yona_pool_init_once_cb(PINIT_ONCE initOnce, PVOID parameter, PVOID* context) {
	(void)initOnce;
	(void)parameter;
	(void)context;
	InitializeCriticalSection(&yona_pool_mutex);
	InitializeConditionVariable(&yona_pool_cond);
	liveness_init();
	EnterCriticalSection(&yona_liveness_mutex);
	for (int i = 0; i < YONA_POOL_SIZE; i++) {
		start_pool_worker_unlocked();
	}
	LeaveCriticalSection(&yona_liveness_mutex);
	return TRUE;
}

static void yona_pool_init(void) {
	InitOnceExecuteOnce(&yona_pool_init_once, yona_pool_init_once_cb, NULL, NULL);
}

typedef int64_t (*yona_thunk_t)(void);

static yona_promise_t* make_promise(void) {
	yona_promise_t* p = (yona_promise_t*)malloc(sizeof(yona_promise_t));
	p->result = 0;
	p->completed = 0;
	p->error = 0;
	InitializeCriticalSection(&p->mutex);
	InitializeConditionVariable(&p->cond);
	return p;
}

yona_promise_t* yona_rt_promise_new(void) {
	return make_promise();
}

void yona_rt_promise_complete(yona_promise_t* p, int64_t result, int is_error,
			      yona_task_group_t* group) {
	if (!p) return;
	EnterCriticalSection(&p->mutex);
	if (p->completed) {
		LeaveCriticalSection(&p->mutex);
		return;
	}
	p->result = result;
	p->error = is_error ? 1 : 0;
	p->completed = 1;
	WakeConditionVariable(&p->cond);
	LeaveCriticalSection(&p->mutex);

	if (group) {
		if (__atomic_fetch_sub(&group->pending_count, 1, __ATOMIC_SEQ_CST) == 1)
			WakeConditionVariable(&group->done_cond);
	}
}

static void enqueue_task(yona_task_t* task) {
	yona_pool_init();
	EnterCriticalSection(&yona_pool_mutex);
	if (yona_task_tail)
		yona_task_tail->next = task;
	else
		yona_task_head = task;
	yona_task_tail = task;
	WakeConditionVariable(&yona_pool_cond);
	LeaveCriticalSection(&yona_pool_mutex);
}

static yona_promise_t* submit_task(yona_async_fn_t fn, yona_thunk_fn_t thunk, int64_t arg,
				   yona_task_group_t* group) {
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
		liveness_init();
		EnterCriticalSection(&yona_liveness_mutex);
		liveness_register_external_task_unlocked();
		LeaveCriticalSection(&yona_liveness_mutex);
	}
	liveness_task_queued();
	enqueue_task(task);
	return promise;
}

yona_promise_t* yona_rt_async_call_thunk(yona_thunk_t thunk) {
	return submit_task(NULL, thunk, 0, NULL);
}

yona_promise_t* yona_rt_async_call(yona_async_fn_t fn, int64_t arg) {
	return submit_task(fn, NULL, arg, NULL);
}

yona_promise_t* yona_rt_async_call_thunk_grouped(yona_thunk_t thunk, yona_task_group_t* group) {
	return submit_task(NULL, thunk, 0, group);
}

extern int64_t yona_rt_closure_apply_thunk(int64_t* closure);

static int64_t spawn_closure_dispatch(int64_t closure_int) {
	int64_t* closure = (int64_t*)(intptr_t)closure_int;
	typedef int64_t (*thunk_fn_t)(int64_t*);
	thunk_fn_t fn = (thunk_fn_t)(intptr_t)closure[0];
	return fn(closure);
}

yona_promise_t* yona_rt_async_spawn_closure(int64_t* closure, yona_task_group_t* group) {
	return submit_task((yona_async_fn_t)spawn_closure_dispatch, NULL, (int64_t)(intptr_t)closure,
			   group);
}

yona_promise_t* yona_rt_async_call_grouped(yona_async_fn_t fn, int64_t arg, yona_task_group_t* group) {
	return submit_task(fn, NULL, arg, group);
}

int64_t yona_rt_async_await_keep(yona_promise_t* promise) {
	EnterCriticalSection(&promise->mutex);
	while (!promise->completed)
		SleepConditionVariableCS(&promise->cond, &promise->mutex, INFINITE);
	int64_t result = promise->result;
	LeaveCriticalSection(&promise->mutex);
	return result;
}

int64_t yona_rt_async_await(yona_promise_t* promise) {
	int64_t result = yona_rt_async_await_keep(promise);
	DeleteCriticalSection(&promise->mutex);
	free(promise);
	return result;
}

void yona_rt_group_cancel(yona_task_group_t* g) {
	if (!g) return;
	__atomic_store_n(&g->cancelled, 1, __ATOMIC_SEQ_CST);
}

int64_t yona_rt_group_await_all(yona_task_group_t* g) {
	if (!g) return 0;
	for (int i = 0; i < g->child_count; i++) {
		yona_promise_t* p = g->children[i];
		EnterCriticalSection(&p->mutex);
		while (!p->completed)
			SleepConditionVariableCS(&p->cond, &p->mutex, INFINITE);
		LeaveCriticalSection(&p->mutex);
	}
	if (g->has_error) {
		int64_t sym = g->first_error_symbol;
		const char* msg = g->first_error_msg;
		yona_rt_raise(sym, msg);
	}
	return 0;
}

/* Test helper: `extern native` returns an already-completed promise (x * 7). */
yona_promise_t* yona_test_native_promise_immediate(int64_t x) {
	yona_promise_t* p = yona_rt_promise_new();
	if (!p) return NULL;
	yona_rt_promise_complete(p, x * 7, 0, NULL);
	return p;
}

int64_t yona_test_slow_identity(int64_t ms) {
	if (ms > 0) {
		DWORD d = (ms > 0x7FFFFFFF) ? 0x7FFFFFFF : (DWORD)ms;
		Sleep(d);
	}
	return ms;
}

int64_t yona_test_slow_add(int64_t a, int64_t b) {
	Sleep(10);
	return a + b;
}
