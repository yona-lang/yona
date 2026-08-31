---
title: Concurrency in depth
description:
  The complete Yona concurrency model — transparent async, task groups,
  channels, and parallel comprehensions.
---

Yona's concurrency model has one governing principle: **the program text
describes data dependencies, and the compiler extracts the parallelism**. There
is no `async` keyword, no `await` keyword, and no colored functions. This page
consolidates the full model: how transparent async works under the hood, how the
compiler decides what may run concurrently, how task groups give structure and
cancellation to concurrent work, and how channels and parallel comprehensions
extend the model to pipelines and batch parallelism.

For a gentler introduction, start with [Concurrency](/learn/concurrency/) in the
Learn track.

## Transparent async

An I/O call like `readFile` does not block. It _starts_ the operation and
returns immediately; the program only waits when it actually needs the result:

```yona
let content = readFile "data.txt" in     # starts the read, does not block
let banner = "== report ==" in           # runs while the read is in flight
banner ++ "\n" ++ content                # waits here, at first real use
```

Semantically, `content` has type `Promise String` between the call and the first
use. You never see this type in source code: the type checker tracks `Promise T`
internally and inserts an **await coercion** at every site where a `Promise T`
value flows into a position that requires a plain `T`. After the coercion, the
binding is an ordinary `String`.

_Implementation note._ Standard-library functions are marked in `.yonai`
interface files as `FN` (pure), `IO` (kernel I/O), or `AFN` (async CPU-bound).
`IO` calls submit to the kernel — io_uring on Linux, IOCP on Windows, kqueue on
macOS — and return a submission ID immediately. `AFN` calls run on a fixed-size
work-stealing thread pool and return a promise. Codegen's `auto_await` checks
whether a value is promise-typed at each use and emits the matching wait
(`YonaRuntimeIoAwait` or `YonaRuntimeTaskAwait`). When io_uring is unavailable
(some containers), I/O falls back to blocking calls with the same observable
semantics.

### Buffer pinning

Async writes hand user buffers to the kernel, which holds them until the
operation completes. Reference counting alone would allow the buffer to be freed
while the kernel still reads it. The runtime therefore copies write and send
payloads into a pinned, RC-managed buffer at submission time and releases it
only after the kernel signals completion. Read buffers are allocated by the
runtime and are unreachable until the await returns, so they need no pinning.
None of this is visible in Yona code — it is what makes transparent async
memory-safe.

## Dependency analysis of let bindings

The unit of parallelism is the multi-binding `let`. Two bindings are
**independent** when neither's right-hand side refers to the other's name
(directly or through intermediate bindings). Independent async bindings are
submitted together; dependent bindings are evaluated in order, exactly as
written:

```yona
let
    a = readFile "users.csv",       # independent — submitted immediately
    b = readFile "orders.csv",      # independent — submitted immediately
    n = length a                    # depends on a — awaits a first
in (n, b)
```

`a` and `b` overlap; total latency is `max(read a, read b)`, not the sum. `n`
forces an await on `a` because its right-hand side uses `a`. Sequential
`let … in let … in …` chains express dependency by construction and are never
reordered. The rule to remember: **Yona preserves your ordering wherever a
dependency exists and removes the waiting wherever none does.**

```yona
import exec from Std\Process in
let
    build = exec "make" ["build"],
    test  = exec "make" ["test"],
    lint  = exec "make" ["lint"]
in (build, test, lint)               # => all three commands ran in parallel
```

## Structured concurrency

Concurrency without structure leaks: a failed sibling keeps running, errors
vanish on background threads. Yona wraps every multi-binding `let` that contains
async work in an implicit **task group**:

- The group tracks all in-flight children (thread-pool promises and io_uring
  operations).
- If one child fails, the group is cancelled: queued thread-pool siblings are
  skipped, and in-flight kernel operations are cancelled through the io_uring
  cancellation interface.
- The first error is re-raised on the parent at the end of the `let`, so
  failures propagate exactly as if the code were sequential.
- No child outlives the scope that created it.

```yona
let
    a = readFile "exists.txt",
    b = readFile "missing.txt"       # raises — a is cancelled,
in a ++ b                            # error propagates to the caller
```

There is no syntax for any of this; it is the semantics of `let`.

_Implementation note._ Codegen emits `group_begin` before the bindings and
`group_await_all` / `group_end` after the body. Worker threads capture
exceptions and record the first error in the group; the runtime's `raise` path
also tears down in-flight groups when an exception unwinds past them, so group
resources are reclaimed on both the success and failure paths.

### Cooperative cancellation

Long-running CPU-bound work can poll for cancellation with the built-in
`Cancel.check` effect, which raises `:Cancelled` if the enclosing task group has
been cancelled:

```yona
let processItem item =
    do
        perform Cancel.check ()      # raises :Cancelled if group cancelled
        heavyComputation item
    end
in processItem work
```

### Spawning explicit tasks

Transparent async parallelizes _bindings_. When you need a long-lived or
detached unit of work — a producer feeding a channel, an actor loop — use
`spawn` from `Std\Task`. It runs a zero-argument closure on a thread-pool worker
and returns a promise, which is auto-awaited at first use like any other async
result:

```yona
import spawn from Std\Task in
let
    a = spawn (\() -> fib 30),
    b = spawn (\() -> fib 31)
in a + b                             # => auto-awaits both tasks
```

Spawned tasks participate in the enclosing task group, so cancellation and error
propagation apply to them too. Exceptions raised inside the closure surface at
the await point.

Task results are type-directed across the native boundary: the compiler passes
an ownership descriptor for the inferred result type. Standalone await
transfers a heap result to the caller; grouped observation retains its own
reference before group cleanup. Cancellation never exposes an untyped result.

## Channels

Transparent async covers "start several things, use the results". It cannot
express a producer and consumer running _at the same time_ over a stream of
values. `Std\Channel` provides bounded, multi-producer multi-consumer channels
for exactly that.

### Creating a channel: linear endpoints

`channel n` creates a channel with buffer capacity `n` and returns the two
endpoints, each wrapped in `Linear`:

```yona
import channel from Std\Channel in
let (sl, rl) = channel 16 in         # (Linear (Sender a), Linear (Receiver a))
case sl of Linear sender ->
case rl of Linear receiver ->
    useThem sender receiver
end end
```

The `Linear` wrappers are compile-time obligations checked by the linearity
checker: each endpoint must be unwrapped by pattern matching **exactly once**.
Dropping an endpoint without unwrapping it is flagged as a resource leak, and
using a `Linear` binding after it has been consumed is error E0600. After
unwrapping, the `Sender a` can only `send` and the `Receiver a` can only `recv`
/ `tryRecv` — the producer/consumer split is enforced by the types. See
[Memory and linearity](/guides/memory/) for the full linearity rules.

### Operations

```yona
send sender v        # blocks while the buffer is full; returns ()
recv receiver        # blocks while empty; Some v, or None when closed+drained
tryRecv receiver     # non-blocking; returns immediately
close sender         # closes the channel; wakes all blocked sends/recvs
isClosed s           # => true after close
length s             # buffered element count
capacity s           # buffer capacity fixed at creation
```

**Close semantics.** `close` marks the channel closed. Receivers first drain any
buffered values (`recv` keeps returning `Some v`), then receive `None`. `None`
is the end-of-stream signal — consumer loops terminate on it. Blocked senders
and receivers are woken when the channel closes.

**Backpressure.** The buffer bound is the memory bound: a fast producer blocks
on `send` when the buffer is full until the consumer catches up. Capacity 1
gives a rendezvous channel; larger capacities decouple bursty rates.

**Cancellation.** Channels integrate with task groups: when a group is
cancelled, sends and recvs blocked inside it wake up and raise `:Cancelled`.

**Deadlock detection.** `send` and `recv` block their worker thread, so the
runtime tracks channel waiters against runnable work. If a blocked task confirms
that no runnable task remains that could unblock it, the runtime raises
`:Deadlock` deterministically — catching a forgotten `spawn` or a producer that
crashed without `close`. If the worker pool is merely saturated while runnable
work is queued, a compensation worker is started instead.

_Implementation note._ Send and receive are mutex-protected with roughly 50 ns
uncontended overhead; every `send` is atomic, so any number of producers and
consumers may share the two endpoints' unwrapped handles.

## Parallel comprehensions

For batch parallelism over a collection, `[| … ]` runs each element's body as
its own thread-pool task, grouped under one task group (any failure cancels the
rest), and collects results **in order**:

```yona
[| x * 2 for x = [1, 2, 3, 4, 5] ]   # => [2, 4, 6, 8, 10]
```

`Std\Parallel` wraps this in the usual combinators — `pmap f xs` for parallel
map, `pfor f xs` for parallel side effects:

```yona
import pmap from Std\Parallel in
pmap (\x -> x * x) [1, 2, 3]         # => [1, 4, 9]

import pfor from Std\Parallel in
pfor (\x -> x + 1) [1, 2, 3]         # => 3 completed elements
```

Use parallel comprehensions when the work is embarrassingly parallel and the
whole result fits in memory; use channels when producers and consumers run at
different rates or the stream is unbounded.

## Choosing a primitive

| Need                                             | Use                                                          |
| ------------------------------------------------ | ------------------------------------------------------------ |
| Several independent I/O or CPU results           | multi-binding `let` (transparent async)                      |
| Batch-parallel map over a collection             | `[\| … ]` or `Std\Parallel.pmap`                             |
| Detached or long-lived unit of work              | `Std\Task.spawn`                                             |
| Streaming pipeline with backpressure             | `Std\Channel` + `spawn`                                      |
| Sequential O(1)-memory streaming, no parallelism | `Iterator` — see [Iterators and streams](/guides/iterators/) |

## Worked example: a channel pipeline

A producer task generates work items and a consumer aggregates them, both
running concurrently with a bounded buffer between them:

```yona
import channel, send, recv, close from Std\Channel,
       spawn from Std\Task in
let (sl, rl) = channel 8 in
case sl of Linear sender ->
case rl of Linear receiver ->
    let
        produce n =
            if n > 100 then close sender
            else let _ = send sender (n * n) in produce (n + 1),
        consume acc = case recv receiver of
            Some v -> consume (acc + v)
            None   -> acc
        end,
        _ = spawn (\() -> produce 1)
    in consume 0                      # => 338350 (sum of squares 1..100)
end end
```

Reading the example:

1. `channel 8` bounds the pipeline: the producer can run at most 8 items ahead
   of the consumer before `send` blocks.
2. The producer is spawned onto a worker thread; the consumer runs on the
   current task. Both endpoints were unwrapped exactly once, satisfying
   linearity.
3. `close sender` ends the stream; the consumer's `None` arm returns the
   accumulated result after draining the buffer.
4. If the producer raised instead of closing, group cancellation would wake the
   blocked `recv` with `:Cancelled` rather than hanging it.

To fan the work out across several consumers, spawn N copies of the consumer
loop reading the same `receiver` — MPMC channels balance load dynamically,
unlike `pmap`'s static partitioning.

## Further reading

- [Memory and linearity](/guides/memory/) — why endpoints are linear, and how
  the RC runtime stays safe under concurrency
- [The type system](/guides/type-system/) — how `Promise T` and effect rows are
  tracked
- [Language specification](/reference/specification/) — normative semantics
