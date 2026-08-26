# Std.Task

Task spawning for concurrent execution.

## Functions

### spawn

```
spawn : (() -> a) -> a
```

Spawn a Yona closure as a concurrent task on a thread pool worker.
Returns a promise that resolves to the closure's return value when the
task completes.

`spawn` is declared as a native promise function — its result is auto-awaited at
the value's first use, just like `readFile` and other I/O calls. This
means the existing transparent async machinery handles task lifecycle:

```yona
import spawn from Std\Task in
let
    a = spawn (\() -> compute1 ()),   -- runs concurrently
    b = spawn (\() -> compute2 ())    -- runs concurrently
in a + b                              -- auto-awaits both
```

The let-binding auto-grouping mechanism (structured concurrency) treats
both spawn calls as parallel tasks. They submit immediately, run on
separate worker threads, and the let body waits for both before computing
`a + b`.

## Combining with Channels

The typical pattern is producer-consumer via `Std\Channel`:

```yona
import channel, send, recv, close from Std\Channel in
import spawn from Std\Task in

with ch = channel 16 in
    let _ = spawn (\() ->
        -- producer: send values, then close
        let _ = send ch 1 in
        let _ = send ch 2 in
        close ch
    ) in
    -- consumer (main task): receive until None
    let loop acc = case recv ch of
        Some v -> loop (acc + v)
        None -> acc
    end in
    loop 0
end
```

The producer runs on a worker thread; the main task reads from the
channel. When the channel is full, the producer's worker blocks until
the consumer drains some. When the channel is empty, the consumer
blocks until the producer sends more or closes.

## Implementation Notes

- Backed by `yona_rt_async_spawn_closure` in the runtime
- The closure runs on the next available thread pool worker (8 threads by default)
- The promise is fulfilled when the closure returns
- Exceptions from the spawned closure propagate to the awaiting task
- Compatible with structured concurrency (task group cancellation)
