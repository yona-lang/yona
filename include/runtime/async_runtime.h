#pragma once

#include <stdint.h>

struct yona_promise;
typedef struct yona_promise yona_promise_t;

struct yona_task_group;
typedef struct yona_task_group yona_task_group_t;

#ifdef __cplusplus
extern "C" {
#endif

yona_promise_t* yona_rt_promise_new(void);
void yona_rt_promise_complete(yona_promise_t* p, int64_t result, int is_error,
                              yona_task_group_t* group);
int64_t yona_rt_async_await(yona_promise_t* promise);

void yona_rt_group_register(yona_task_group_t* g, yona_promise_t* p);

/* Structured concurrency — non-zero means `yona_rt_group_cancel` was called. Used by
 * channel I/O and (optionally) GPU fence completion to complete with a cancel error. */
int yona_rt_group_is_cancelled(yona_task_group_t* g);

/* Codegen tests: `extern native` — promise already completed before await. */
yona_promise_t* yona_test_native_promise_immediate(int64_t x);

#ifdef __cplusplus
}
#endif
