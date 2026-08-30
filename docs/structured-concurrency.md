# Structured Concurrency

## Overview

Yona has transparent async — independent let bindings run concurrently without explicit `async/await` syntax. Structured concurrency adds lifecycle management on top: error propagation, cancellation, and parallel comprehensions.

## Automatic Scoped Error Propagation

No new syntax needed. Let blocks with multiple async bindings automatically create a **task group**. If one binding fails, siblings are cancelled and the error propagates:

```yona
let
    a = readFile "foo.txt",
    b = readFile "bar.txt"
in a ++ b
-- If readFile "foo" fails:
--   1. readFile "bar" is cancelled (thread pool: skip, io_uring: IORING_OP_ASYNC_CANCEL)
--   2. Error propagated to caller
--   3. Scope exits cleanly
```

This is fully automatic — the compiler wraps multi-binding let blocks in task groups.

## Cancel Effect

Long-running computations can cooperatively check for cancellation:

```yona
let processItem item =
    do
        perform Cancel.check ()   -- raises :Cancelled if group is cancelled
        heavyComputation item
    end
in processItem myItem
```

`Cancel.check` is a built-in effect recognized by the compiler. It emits a check against the current task group's `cancelled` flag and raises `:Cancelled` if set.

## Parallel Comprehensions

Process all elements of a collection concurrently:

```yona
[| f x for x = items ]
```

Each `f x` runs as a separate task in the thread pool. All tasks are grouped — if any fails, the rest are cancelled. Results are collected in order.

```yona
-- Double each element concurrently
[| x * 2 for x = [1, 2, 3, 4, 5] ]
-- Result: [2, 4, 6, 8, 10]

-- Fetch multiple URLs concurrently
[| httpGet url for url = urls ]
```

The syntax `[|` opens a parallel comprehension; `]` closes it. The body expression is the same as regular comprehensions (`expr for var = source`).

## Std\Parallel Module

```yona
import pmap, pfor from Std\Parallel in

-- Parallel map
pmap (\x -> x * 2) [1, 2, 3]   -- [2, 4, 6]

-- Parallel for-each (side effects)
pfor (\item -> upload item) items
```

## Architecture

### Task Group Runtime (`src/Runtime/Concurrency/AsyncPosix.c` / `AsyncWin32.c`)

```c
typedef struct yona_task_group {
    int cancelled;           // atomic flag
    int pending_count;       // atomic count of incomplete children
    YonaTask** children;  // thread pool promises
    uint64_t* io_children;     // io_uring operation IDs
    int64_t first_error;       // error symbol from first failure
    const char* first_error_msg;
    void* arena;               // bump arena for parent-thread scope (see below)
} YonaTaskGroup;
```

API:
- `YonaRuntimeTaskGroupBegin()` — create a group
- `YonaRuntimeTaskGroupAttachArena(group, arena)` — attach bump arena created with `YonaRuntimeArenaCreate` (codegen for multi-binding `let`)
- `YonaRuntimeTaskGroupRegister(group, promise)` — add thread-pool child
- `YonaRuntimeTaskGroupRegisterIo(group, io_id)` — add io_uring child
- `YonaRuntimeTaskGroupCancel(group)` — set cancelled flag
- `YonaRuntimeTaskGroupIsCancelled(group)` — check flag (for Cancel.check)
- `YonaRuntimeTaskGroupAwaitAll(group)` — wait for all children, re-raise first error
- `YonaRuntimeTaskGroupEnd(group)` — destroy attached arena (if any), mutex/cond, free group
- `YonaRuntimeTaskGroupArenaBindPush(group)` / `YonaRuntimeTaskGroupArenaBindPop()` — TLS stack keyed by `yona_try_depth` so `YonaRuntimeRaise` can call `YonaRuntimeTaskGroupEnd` for in-flight groups before `longjmp` (success path pops after `group_end`)

### Worker Error Capture

Worker threads wrap task execution in `setjmp`. On exception:
1. Error captured in the task group (`first_error_symbol`, `first_error_msg`)
2. Group cancelled flag set → siblings skip execution
3. `group_await_all` re-raises the error on the caller's thread

### io_uring Cancellation (`include/yona/Runtime/Platform/IoUring.h`)

When a group is cancelled, `YonaRuntimeIoUringCancel(target_id)` submits `IORING_OP_ASYNC_CANCEL` SQEs for each pending io_uring operation. The kernel cancels in-flight operations, which complete with `res = -ECANCELED`. The await path handles this by cleaning up the I/O context.

### Codegen Integration

In `codegen_let` (`src/Codegen/CodegenExpr.cpp`):
1. Multi-binding let blocks emit `group_begin()` before alias codegen
2. A **task-group bump arena** is always created (`arena_create`), attached with `group_attach_arena`, and registered via `group_arena_bind_push` for exception-safe teardown
3. `current_arena_` points at that arena for **alias codegen and the let body**, so non-escaping heap (`analyze_let_escaping`) bump-allocates into the group arena
4. Async calls use grouped variants (`async_call_grouped`) — worker threads do **not** receive the parent arena; async RHS stay on `YonaRuntimeAllocate`
5. On fall-through: `group_await_all()` → `cleanup_let_scope` (skips `rc_dec` for arena payloads) → `group_arena_bind_pop` → `group_end()` (destroys arena + frees group)
6. If the body already terminated (`raise`, etc.), await/cleanup/end are skipped in IR; `YonaRuntimeRaise` walks the bind stack and calls `group_end` for each deeper task group before `longjmp`
7. `current_group_` tracks the active group (like `handler_stack_` for effects)

Benchmark: `bench/concurrency/task_group_arena.yona` (one outer two-binding `let` with small seq literals, then 50k iterations of an inner tail-recursive counter) plus `bench/Reference/TaskGroupArena.c` for a stack-only C baseline.

## Comparison

| Language | Approach | Yona's advantage |
|----------|----------|-----------------|
| Kotlin | `coroutineScope { launch { } }` | Automatic — no explicit scope needed |
| Swift | `TaskGroup { group.addTask { } }` | Implicit grouping for let blocks |
| Java | `StructuredTaskScope` | No boilerplate, functional style |
| Trio | `async with open_nursery() as n:` | Compile-time Cancel effect |
| Go | Manual `errgroup.Group` | Type-safe, compiler-managed |
