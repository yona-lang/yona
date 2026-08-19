---
title: Iterators and streams
description: Streaming data processing in Yona with the prelude Iterator type and the lazy Std\Stream module — constant-memory pipelines in a strict language.
---

Yona is strictly evaluated, but two library types give you streaming,
demand-driven data processing: the prelude **`Iterator`** (a stateful pull
handle, ideal for I/O sources) and **`Std\Stream`** (a pure lazy sequence built
from explicit thunks). Both let you process data far larger than memory —
one element resident at a time.

## The `Iterator` type

`Iterator` is a prelude type — available everywhere without an import:

```yona
type Iterator a = Iterator (() -> Option a)
```

An iterator wraps a *next* function: each call returns `Some element` until the
source is exhausted, then `None`. The state (file offset, scan position) lives
behind the closure, so iterators are inherently **single-use** — once drained,
they cannot be rewound.

## Streaming sources in the stdlib

Several stdlib functions return iterators instead of materialized sequences:

| Function | Returns | Yields |
|----------|---------|--------|
| `Std\File::readLines path` | `Iterator String` | file lines, 64 KB buffered |
| `Std\String::chars str` | `Iterator Int` | character codes |
| `Std\String::split delim str` | `Iterator String` | substrings, on demand |
| `Std\String::lines str` | `Iterator String` | lines split on `\n` |

```yona
import chars, split from Std\String in
let codes = [c for c = chars "hi"],
    parts = [s for s = split "," "a,b,c"]
in codes
# => [104, 105]        (parts is ["a", "b", "c"])
```

## Generators consume iterators in O(1) memory

Comprehensions detect an `Iterator` source and compile to a streaming loop:
call `next()`, stop on `None`, evaluate the body on the element, append to the
result. Only one source element is live at a time.

```yona
import readLines from Std\File, length from Std\String in
[length line for line = readLines "large_file.txt"]
# => one Int per line — the file is never fully resident
```

Implementation note. The generator loop appends with an O(1)-amortized
`seq_snoc`, so results grow without a size limit. `readLines` is backed by a C
iterator holding a 64 KB read buffer that is reused across `next()` calls;
memory use is O(64 KB) regardless of file size. The *result* sequence is
materialized — if you also want the output to stay small, fold instead of
collecting (see the worked example below).

## Iterators vs materialized sequences

| Scenario | `Seq` (eager) | `Iterator` (streaming) |
|----------|---------------|------------------------|
| 50 MB file, count lines | O(50 MB) memory | O(64 KB) memory |
| 1M-char string, per-char work | O(1M) allocations up front | O(1) per char |
| Split a 10K-field CSV row | 10K strings up front | one string per field |

Guidance: use a `Seq` when the data is small, when you need random access,
length, or multiple passes. Use an iterator when the source is I/O, when the
data may be large, or when you will consume it exactly once, front to back.
Iterators are forward-only, have no `length`, and are single-use.

## `Std\Stream`: lazy sequences from explicit thunks

`Iterator` hides mutable state in the runtime. `Std\Stream` is the pure
alternative: laziness encoded directly in an ADT, with the "rest of the
sequence" as an explicit thunk. This is how a strict language expresses lazy
streams — the same shape as OCaml's `Seq` or ML lazy streams:

```yona
type Stream a = Yield a (() -> Stream a) | Nil
```

`Yield x rest` exposes the head element and a function that produces the rest
when called. Nothing runs until a consumer forces the next step. There is no
hidden state: "what comes next" lives in the recursive arguments of whatever
operator built the stream.

### Producers

`empty`, `singleton`, `fromSeq`, `range`, `naturals`, `repeat`, `iterate`,
`unfold`, and `fromIterator` start a pipeline:

```yona
import range, iterate, unfold from Std\Stream in
range 1 5            # 1, 2, 3, 4         (hi is exclusive)
iterate (\n -> n * 2) 1   # 1, 2, 4, 8, ...  (infinite)
unfold (\s -> if s > 3 then None else Some (s, s + 1)) 1  # 1, 2, 3
```

`repeat`, `iterate`, and `naturals` are infinite — always bound them with
`take` or a short-circuiting terminator before materializing.

### Lazy transformers

`map`, `filter`, `take`, `drop`, `takeWhile`, `dropWhile`, `zip`, `zipWith`,
`concat`, `flatMap`, `scan`, and `chunksOf` transform a stream without running
it. Pipelines read naturally with `|>`:

```yona
import fromSeq, map, sum from Std\Stream in
fromSeq [1, 2, 3] |> map (\x -> x * x) |> sum
# => 14
```

```yona
import range, filter, take, toSeq from Std\Stream in
range 1 1000000 |> filter (\x -> x % 7 == 0) |> take 3 |> toSeq
# => [7, 14, 21]      (the range is never fully evaluated)
```

`chunksOf n` groups consecutive elements into `Seq` chunks of size `n` (the
last chunk may be shorter) — here `[1, 2, 3]`, `[4, 5, 6]`, `[7]`:

```yona
import range, chunksOf, count from Std\Stream in
range 1 8 |> chunksOf 3 |> count
# => 3
```

### Terminators

`toSeq`, `foldl`, `forEach`, `count`, `sum`, `anyMatch`, `allMatch`, `find`,
`head`, and `isEmpty` actually pull elements through the pipeline. `anyMatch`,
`allMatch`, `find`, and `head` short-circuit:

```yona
import naturals, map, find from Std\Stream in
case naturals |> map (\n -> n * n) |> find (\sq -> sq > 50) of
    Some sq -> sq
    None -> 0
end
# => 64
```

### Resource scoping: `bracket` <span class="yona-status yona-status--partial">Partial</span>

`bracket acquire release produce` runs `release` exactly once when the stream
from `produce` is fully drained; the resource is held across the whole stream,
not per element:

```yona
import bracket, forEach from Std\Stream in
bracket (\_ -> openThing 0) (\r -> closeThing r) (\r -> streamFrom r)
    |> forEach handle
```

The partial part: abandoning a bracketed stream *before* `Nil` (for example
`take 10` of a longer source) currently leaks the resource — a consumer-drop
signal is planned. `acquire` and `release` take an ignored `Int` argument
rather than `()` for calling-convention reasons.

### Pipeline parallelism: `async` and `buffered`

By default an entire pipeline runs in the consumer's task — forcing the next
element is just a function call. To split work across tasks, insert one
explicit `async` at the boundary you want:

```yona
import fromIterator, map, filter, async, take, toSeq from Std\Stream,
       readLines from Std\File in
fromIterator (readLines "input.txt")
    |> map parse          # runs in the caller's task
    |> filter valid
    |> async              # pipeline boundary: bounded channel, capacity 16
    |> map enrich         # runs in a spawned task
    |> take 100
    |> toSeq
```

`async` spawns a producer task that pulls from upstream and sends into a
bounded channel; the downstream stream pulls from that channel. Backpressure
is automatic — a slow consumer blocks the channel, which blocks the producer.
`buffered n` is `async` with an explicit capacity. There is no implicit
threading: you can read a pipeline and see exactly where the task boundaries
are.

Implementation note. If the spawned producer raises, the consumer currently
sees an early end-of-stream rather than the error, and cancellation of the
consumer does not yet propagate upstream promptly. Error forwarding and
cancellation across `async` are planned; where they matter today, use
`Std\Channel` directly.

## Worked example: a large file in constant memory

Total the line lengths of a file without ever holding more than one line (plus
the 64 KB read buffer) in memory. The comprehension streams from the iterator
and the fold consumes each element as it arrives:

```yona
import readLines from Std\File,
       foldl from Std\List,
       length from Std\String in
foldl (\total n -> total + n) 0
      [length line for line = readLines "lines.txt"]
```

```bash
printf 'alpha\nbeta\ngamma\n' > lines.txt
yonac -o total total.yona
./total
# => 14
```

The same shape with `Std\Stream` keeps everything in one lazy pipeline and
adds an easy upgrade path to pipeline parallelism (insert `async` before the
expensive stage):

```yona
import fromIterator, map, sum from Std\Stream,
       readLines from Std\File,
       length from Std\String in
fromIterator (readLines "lines.txt") |> map (\line -> length line) |> sum
# => 14
```

(The lambda wrapper around `length` is currently required — passing an
imported function directly as a higher-order argument is a known compiler
gap.)

## Limitations

- **Iterators are linear.** Forward-only, no `length` without draining,
  single-use. Wrapping the same iterator with `fromIterator` twice yields two
  streams that share and corrupt state — lift each iterator exactly once.
- **Streams are single-consumer.** `toSeq` drains the stream; a second
  consumer would re-run the pipeline from scratch (or read an already-drained
  channel after `async`). A broadcast primitive is planned separately.
- **No stream fusion for `Std\Stream` yet.** Each `map`/`filter` step
  allocates a closure per element. Comprehension pipelines *are* fused (see
  [Performance](/guides/performance/)); for the hottest sequential loops,
  prefer a comprehension or a single `foldl` over a long stream pipeline.
- **Dict/Set iteration** is not yet exposed as an iterator.
- **`zip` termination.** `zip` stops when either input ends; the other
  stream's producer is left dangling (same root cause as the `bracket`
  abandonment gap).
