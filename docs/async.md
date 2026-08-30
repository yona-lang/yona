# Transparent Async in Yona

Yona's async model is unique: there are no `async`/`await` keywords. The compiler automatically parallelizes independent computations and inserts await coercions at use sites.

## How It Works

### Independent Let Bindings Run in Parallel

```yona
let
    a = readFile "foo.txt",
    b = readFile "bar.txt",
    c = readFile "baz.txt"
in
    a ++ b ++ c
```

All three file reads are submitted to io_uring simultaneously. The results are auto-awaited only when used in the concatenation. Total time = max(read times), not sum.

### No Async/Await Keywords

In JavaScript you write:
```javascript
const a = await readFile("foo.txt");
const b = await readFile("bar.txt");  // waits for a first!
```

In Yona, the equivalent is just:
```yona
let a = readFile "foo.txt",
    b = readFile "bar.txt"
in a ++ b
```

Both reads happen concurrently. The compiler handles the rest.

### Two Async Backends

**io_uring (IO functions)**: File I/O, network I/O, and other kernel operations are submitted directly to Linux's io_uring interface. The function returns immediately with a submission ID; completion is awaited lazily.

```yona
-- These functions are marked IO in .yonai:
-- IO YonaStdFileReadFile 1 STRING -> STRING
-- The codegen emits a direct call that returns a uring ID,
-- then inserts io_await at the use site.
```

**Thread pool (AFN functions)**: CPU-bound or blocking operations run in a work-stealing thread pool. The function returns a promise immediately.

```yona
-- These functions are marked AFN in .yonai:
-- AFN exec 2 STRING SEQ -> STRING
-- The codegen wraps the call in thread pool submission,
-- then inserts async_await at the use site.
```

### Per-OS backend status

| OS | IO backend | `YonaRuntimeIoAwait` source | Notes |
|----|------------|---------------------------|-------|
| Linux | io_uring submit + completion | `src/Runtime/Platform/FileLinux.c` | Native submit-and-return for file/net operations. |
| Windows | IOCP submit + `io_await` completion (with direct-result fallback where ordering-sensitive) | `src/Runtime/Platform/FileWindows.c` | `Std\Net` data/control paths run on overlapped Winsock + IOCP; file submit APIs preserve Linux-compatible semantics with direct-result IDs where needed. |
| macOS | kqueue submit + completion (file workers + socket readiness) | `src/Runtime/Platform/FileMacOs.c` | Same submit-and-return IDs as Linux; `YonaRuntimeKqueueAwait` in `KqueueMacOs.c`. |

### Auto-Await

The compiler automatically inserts await coercions when a `Promise<T>` value is used where `T` is expected:

```yona
let content = readFile "data.txt" in   -- returns Promise<String>
let lines = split "\n" content in      -- content auto-awaited here (needs String)
length lines                            -- lines is [String], no await needed
```

You never see promises. You never write await. The type system handles it.

## Process Parallelism

The Process module uses thread pool async for non-blocking subprocess execution:

```yona
import exec from Std\Process in
let a = exec "make" ["build"],
    b = exec "make" ["test"],
    c = exec "make" ["lint"]
in (a, b, c)
-- All three commands run in parallel
```

## How It's Implemented

1. **IO functions** submit to io_uring via raw syscalls (no liburing dependency)
2. **AFN functions** submit to a fixed-size work-stealing thread pool
3. **auto_await** in the codegen checks if a `TypedValue` has `CType::PROMISE` and inserts the appropriate await call (`YonaRuntimeIoAwait` or `YonaRuntimeTaskAwait`)
4. When io_uring is unavailable (containers, low-memory), functions fall back to blocking I/O with transparent direct result registration

## Comparison

| Language | Model | Trade-off |
|----------|-------|-----------|
| **Yona** | Transparent async, no keywords | Zero boilerplate, automatic parallelism |
| Go | Goroutines + channels | Explicit `go` keyword, manual channel management |
| Rust | async/await + tokio | Explicit async, colored functions, Pin complexity |
| JavaScript | async/await + Promises | Explicit async, colored functions |
| Haskell | IO monad + forkIO | Monadic boilerplate |
| Erlang | Actor model | Message passing overhead |
