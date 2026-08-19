---
title: Concurrency
description: Transparent async — parallel let bindings, auto-await, sequential do blocks, scoped resources, and parallel comprehensions, with no async/await keywords.
---

Yona has no `async` or `await` keywords. I/O and other asynchronous
operations return promises *internally*; the compiler inserts an await
automatically at the first point where the value is actually used. Your code
reads as if it were synchronous, and independent work runs in parallel
without any annotation.

This page describes the user-level model. For the runtime machinery —
io_uring, the thread pool, task groups, cancellation — see
[Concurrency internals](/guides/concurrency/).

## Independent `let` bindings run in parallel

When a `let` expression has several bindings that do not depend on each
other, every asynchronous right-hand side is **submitted before any of them
is awaited**:

```yona
import readFile from Std\File in
let
  a = readFile "foo.txt",
  b = readFile "bar.txt",
  c = readFile "baz.txt"
in a ++ b ++ c
```

All three reads start immediately; the first await happens at the `++` in
the body, where the string values are needed. Elapsed time is approximately
the **maximum** of the three read times, not their sum.

The ordering guarantee is precise:

- Binding right-hand sides are *submitted* in source order, but the program
  does not wait for one to complete before submitting the next.
- A binding that mentions an earlier binding's name depends on its value, so
  the dependency is awaited first. `let a = readFile p, b = process a in b`
  runs sequentially because `b` needs `a`.
- The body's first *use* of a bound value awaits it. Unused promise-valued
  bindings are still awaited before the `let` scope exits, so no work leaks
  past the scope.

## Contrast with JavaScript-style await

In JavaScript, `await` is explicit and each one is a sequencing point:

```javascript
const a = await readFile("foo.txt");
const b = await readFile("bar.txt");  // starts only after a completes!
```

Getting parallelism back requires restructuring into `Promise.all`. In Yona
the parallel version *is* the naive version:

```yona
let a = readFile "foo.txt",
    b = readFile "bar.txt"
in a ++ b
```

There are no "colored" functions: an effectful function is called exactly
like a pure one, and callers never change shape when a function becomes
asynchronous.

## Auto-await at use sites

The type system tracks asynchronous values as `Promise<T>` internally. When
a `Promise<T>` appears where a `T` is expected — an operator operand, a
function argument, a condition — the compiler records a coercion and the
runtime awaits at that point:

```yona
import readFile from Std\File, split from Std\String in
let content = readFile "data.txt" in     # content: Promise<String> internally
let lines = split "\n" content in        # first use — awaited here
length lines                             # lines is a plain Seq, no await
```

You never see the `Promise` type in ordinary code and never write an await.

*Implementation note.* Two backends serve these promises. Kernel I/O
(files, sockets) is submitted to io_uring on Linux (IOCP on Windows, kqueue
on macOS) and returns a submission ID immediately. CPU-bound or blocking
operations run on a work-stealing thread pool. The await coercion picks the
matching completion call; when io_uring is unavailable (some containers),
the runtime falls back to blocking I/O transparently.

## Error propagation and cancellation

A multi-binding `let` forms an implicit **task group**. If one binding
raises, its siblings are cancelled — queued thread-pool tasks are skipped
and in-flight kernel I/O is cancelled — and the error propagates to the
caller after the group has quiesced:

```yona
let
  a = readFile "exists.txt",
  b = readFile "missing.txt"    # raises
in a ++ b
# The read of "exists.txt" is cancelled; the error propagates.
```

No exception escapes while sibling tasks are still running, and no task
outlives the `let` scope that created it. This is structured concurrency
without any scope syntax.

## `do` for guaranteed sequential effects

Expressions in a `do` block execute strictly top to bottom; the last
expression is the block's value. Use `do` when *ordering itself* is the
point — writes, protocol steps, anything where interleaving would be wrong:

```yona
import println from Std\IO in
do
    println "first"
    println "second"
    42
end
# first
# second
# => 42
```

`do` blocks support intermediate bindings with `name = expr`, which also
execute in order:

```yona
do
    fd = tcpConnect "localhost" 8080
    send fd "hello"
    response = recv fd 4096
    close fd
    response
end
```

Rule of thumb: `let` for values (the compiler may parallelize independent
bindings), `do` for effects (the compiler must not reorder). A `do` block
never runs its steps concurrently, even when they look independent.

## `with` for scoped resources

`with name = resource in body` binds a resource for the extent of `body` and
releases it deterministically when the body completes:

```yona
with handle = tcpConnect "localhost" 8080 in
    send handle "hello"
# handle is closed here after the body completes
```

The resource's type must implement the `Closeable` trait; this is checked at
compile time, and a type without a `Closeable` instance is a compile error
(not a warning). The built-in `Closeable Int` instance covers file
descriptors and sockets; `Closeable FileHandle` covers binary file handles.
Release order for nested `with` scopes is innermost first:

```yona
with server = tcpListen "0.0.0.0" 9000 in
with client = tcpAccept server in
    recv client 1024
# client closed first, then server
```

*Current limitation.* Release runs when the body completes normally; if an
exception propagates out of the body, `close` is not currently invoked on
the unwind path. Combine `with` and `try`/`catch` inside the body when you
must handle failures before the scope exits.

## Parallel comprehensions

`[| expr for var = source ]` evaluates the body for every element
**concurrently**, one thread-pool task per element, and collects results in
source order:

```yona
[| x * 2 for x = [1, 2, 3, 4, 5] ]
# => [2, 4, 6, 8, 10]

[| httpGet url for url = urls ]   # all requests in flight at once
```

The tasks form a task group with the same guarantees as a multi-binding
`let`: if any element's task raises, the remaining tasks are cancelled and
the error propagates. Result order is always the source order regardless of
completion order.

Ordinary comprehensions `[ expr for var = source ]` remain sequential; the
`[|` opener is the only difference.

## `extern async`

Foreign C functions can join the transparent-async model. The `async`
modifier submits the call to the thread pool and returns a promise
immediately, with the usual auto-await at use sites:

```yona
extern async slowCompute : Int -> Int in
let a = slowCompute 40,
    b = slowCompute 2
in a + b
# both C calls run concurrently; total ≈ max, not sum
```

Standard-library I/O is declared the same way in module interfaces, which is
why `Std\File`, `Std\Net`, and `Std\Process` calls parallelize with no user
action:

```yona
import exec from Std\Process in
let build = exec "make build",
    test  = exec "make test",
    lint  = exec "make lint"
in (build, test, lint)
# all three subprocesses run in parallel
```

## Beyond the basics

Transparent async covers independent operations with results. For pipelines,
work queues, and actor-style tasks, `Std\Channel` provides bounded channels
and `Std\Task` provides `spawn`; `Std\Parallel` provides `pmap` and `pfor`.
These build on the same runtime and integrate with task-group cancellation.
See [Concurrency internals](/guides/concurrency/) for channels, deadlock
detection, and the scheduler, and the
[specification](/reference/specification/) for the formal semantics.
