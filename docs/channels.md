# Channels

Yona provides bounded MPMC channels for communication between concurrent
tasks. Combined with `spawn` from `Std\Task`, channels enable patterns
that the existing primitives (Iterator, parallel comprehensions, transparent
async let bindings) can't express:

- **Pipeline parallelism**: producer and consumer running on different threads
- **Backpressure**: bounded buffer enforces memory limits
- **Dynamic work distribution**: producers and consumers with variable rates
- **Fan-out / fan-in**: multiple workers reading from one channel
- **Actor-style stateful tasks**: long-running task with a mailbox channel

## Quick Reference

```yona
import channel, send, recv, close from Std\Channel in
import spawn from Std\Task in

let ch = channel 16 in                           -- bounded buffer
let _ = spawn (\() ->                            -- producer task
    let _ = send ch 1 in
    let _ = send ch 2 in
    close ch) in
let loop acc = case recv ch of                   -- consumer (main task)
    Some v -> loop (acc + v)
    None   -> acc                                 -- closed and drained
end in
loop 0
```

## Design

### Why "between tasks" only?

Yona's channels are **synchronous** primitives — `send` blocks if the
buffer is full; `recv` blocks if empty. The blocking happens on the
calling task's worker thread.

This is fine when producer and consumer are on **different** tasks: one
blocks, the other runs, both make progress. But if every live task is
blocked on channels and there is no queued work left that could unblock
them, no progress is possible.

The runtime tracks worker tasks, external tasks, queued async work, and
channel waiters. When `send` or `recv` would block, it records the wait
and raises `:Deadlock` once the blocked task confirms that no runnable
task remains. This is deterministic; it is not based on a fixed timeout.
If the fixed worker pool is saturated by blocking channel operations but
runnable work is queued, the runtime starts a compensation worker instead
of reporting a deadlock.

The canonical pattern is:
1. Create a channel
2. Spawn the producer in a separate task via `Std\Task.spawn`
3. Read from the channel in the consumer (main task or another spawn)

### Comparison with other approaches

| Approach | Producer/Consumer | Memory | Use case |
|----------|---|---|---|
| `Iterator` | sequential, pull-based | O(1) per call | streaming with no parallelism |
| `pmap` / parallel comprehension | static partition, batch | full result in memory | embarrassingly parallel batch |
| `let p = ..., q = ... in ...` (transparent async) | independent I/O | full results | independent ops with results |
| **`channel + spawn`** | **concurrent, bidirectional** | **bounded buffer** | **pipelines, queues, actors** |

Channels open up patterns that the other primitives can't express:
long-lived stateful tasks, dynamic work queues, push-based event streams,
true pipeline parallelism.

## Patterns

### Producer-consumer pipeline

```yona
import channel, send, recv, close from Std\Channel in
import spawn from Std\Task in

let ch = channel 16 in
let _ = spawn (\() -> produceData ch) in
consumeData ch
```

The producer fills the channel; the consumer drains it. Each runs on
its own worker thread. Memory bounded by capacity.

### Fan-out (worker pool)

```yona
let work = channel 16 in
let _ = spawn (\() -> distribute work) in
-- N workers reading from the same channel
let w1 = spawn (\() -> worker work),
    w2 = spawn (\() -> worker work),
    w3 = spawn (\() -> worker work),
    w4 = spawn (\() -> worker work)
in (w1, w2, w3, w4)
```

Dynamic load balancing: fast workers pull more items. Different from
`pmap` which partitions statically.

### Fan-in (multiple producers)

```yona
let results = channel 100 in
let p1 = spawn (\() -> producer1 results),
    p2 = spawn (\() -> producer2 results),
    p3 = spawn (\() -> producer3 results)
in collect results
```

Multiple producers send to the same channel; one consumer aggregates.
Each `send` is atomic (mutex-protected).

### Long-lived actor

```yona
let mailbox = channel 64 in
let _ = spawn (\() -> actorLoop mailbox initialState) in
-- Other tasks send messages to the actor
let _ = send mailbox msg1 in
let _ = send mailbox msg2 in
...
```

The actor processes messages serially. The channel is its mailbox.

## Cancellation

Channels integrate with structured concurrency. When a task group is
cancelled (e.g., one task raises an exception), blocked sends and recvs
in that group wake up and raise `:Cancelled`.

## Deadlock Detection

If a task blocks on `send` or `recv` for more than ~5 seconds without
making progress, the runtime raises `:Deadlock`. This catches:

- Forgot to `spawn` the producer
- Producer crashed without closing the channel
- Cycle through multiple channels

The detection is heuristic (timed waits) — production code should
prefer correct design over relying on the detector.

## Performance Notes

- **Send/recv overhead**: ~50ns uncontended (single mutex acquire + memcpy)
- **Buffer size**: choose to match producer/consumer rate ratio
- **Cap = 1**: rendezvous channel — sender blocks until receiver picks up
- **Large cap**: more buffering, less synchronization, more memory

For high-throughput hot paths, lock-free MPSC channels would be faster.
The current mutex-based implementation prioritizes simplicity and
correctness; lock-free is a future optimization.

## See Also

- [`Std\Channel` API](api/Channel.md)
- [`Std\Task` API](api/Task.md)
- [Structured Concurrency](structured-concurrency.md)
