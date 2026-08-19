---
title: Performance
description: How Yona's compilation pipeline shapes performance, honest benchmark numbers against C, and practical guidance for writing fast Yona.
---

Yona's performance story is built on two rules: **measured claims only**, and
**a reproducible harness** anyone can run. Every number on this page comes from
the benchmark suite in the compiler repository, where each benchmark has a
golden `.expected` output that is verified before timing — a benchmark that
produces the wrong answer does not get to report a time.

## How the compiler shapes performance

### Monomorphization

Yona has no uniform boxed representation for generic code. Functions are stored
as AST at definition and compiled **at the call site**, where concrete argument
types are known — so a generic `map` over `Int` compiles to a loop over 64-bit
integers, not to calls through a boxed interface. The same mechanism works
across module boundaries: exported functions embed their source text in the
`.yonai` interface and are re-specialized in the caller when call-site types
differ (see [Modules and interfaces](/guides/modules-interfaces/)).

### Stream fusion in comprehensions

Chained comprehensions fuse into a single loop with no intermediate
collections:

```yona
let nums = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10],
    doubled = [x * 2 for x = nums]
in [x for x = doubled, if x > 10]
# => [12, 14, 16, 18, 20]
```

The map and the guarded filter compile into one pass; `doubled` never
materializes when it is only consumed by the next comprehension. This is why
the `list_map_filter` benchmark below matches C: the C version is a single
loop, and so is the compiled Yona.

Implementation note. Fusion currently applies to comprehension pipelines. The
combinator pipelines of `Std\Stream` are *not* fused yet — each step allocates
a per-element closure — so prefer comprehensions or a single `foldl` for the
hottest sequential loops.

### Reference counting with Perceus transfer

Heap values are managed by atomic reference counting, but the compiler removes
most of the traffic. All heap types follow a **callee-owns** calling
convention: when an argument is provably at its last use, ownership transfers
without an increment; the callee consumes it directly. Combined with automatic
**borrow inference** — parameters that provably don't escape skip both the
call-site increment and the function-exit decrement — typical fold/map/filter
code runs with almost no refcount operations in the loop.

When a sequence or dictionary has reference count 1, operations mutate it in
place instead of copying: consing to a uniquely-owned sequence is O(1) with no
allocation, and inserting into a uniquely-owned dictionary updates the node
directly. Persistent semantics are preserved — the fast path only triggers
when nobody else can observe the value.

Implementation note. Introducing Perceus transfer made the list benchmarks
(`list_sum`, `list_reverse`, `list_map_filter`) 2–3× faster with 3× less
memory, and cut the `queens` benchmark's footprint from 43 MB to 2.2 MB. An
explicit `@borrow` parameter annotation exists, but for bodies that pass its
checks the inference already produces identical code — the annotation is a
compile-time-checked contract, not a speedup.

### Arena and pool allocation

Let-bound values that provably don't escape their scope are bump-allocated
from a per-scope arena — roughly 3× faster than malloc — and freed in bulk at
scope exit with no per-object refcounting. Everything else goes through a
slab-based pool allocator with thread-local free lists for the common small
sizes. Multi-binding `let` blocks (which form implicit task groups) attach an
arena to the group and reclaim it wholesale when the group ends, even when
unwinding through a `raise`.

## Benchmarks vs C

Measured against equivalent C compiled with `gcc -O2`, 10 iterations,
wall-clock time. Ratio is Yona time / C time — 1.0x means parity:

| Benchmark | Ratio | Notes |
|-----------|-------|-------|
| par_map | **1.0x** | Parallel comprehension, 20 elements |
| list_map_filter | **1.0x** | Stream fusion eliminates intermediate allocations |
| parallel_async / sequential_async | **1.0x** | io_uring + thread pool |
| tak | **1.1x** | Deep recursion |
| sum_squares | 1.3x | Tight loop, tail-call elimination |
| sieve | 1.4x | List filtering |
| fibonacci | 2.4x | Function-call overhead |
| dict_build / set_build (10K) | 2.2x | Persistent HAMT vs raw C array/hash |
| ackermann | 2.6x | Deep recursion |
| queens | 10.6x | Allocation-heavy (43 MB vs 2 MB) — under investigation |

Read the table honestly: pipelines that fuse, parallel workloads, and
recursion-heavy numeric code sit at or near C. Allocation-heavy backtracking
(`queens`) is currently the worst case by a wide margin — the allocator
pressure is a known problem being worked on, not a fact of life we're hiding.
Persistent dictionaries and sets pay roughly 2× over raw mutable C structures
in exchange for O(1) structural sharing.

## Running the harness

The suite lives in `bench/` in the compiler repository. Reference
implementations in C, Erlang, Haskell, Java, JavaScript, and Python are under
`bench/reference/` (C and Erlang are wired into the runner's comparison
output; the rest are kept for reading).

```bash
# All benchmarks, compare against the C references, 10 iterations
python3 bench/runner.py --compare-c -n 10

# One benchmark
python3 bench/runner.py fibonacci

# Compare optimization levels, JSON for CI
python3 bench/runner.py --all-opt-levels
python3 bench/runner.py --json

# Verify reference programs against golden outputs (no Yona compile)
python3 bench/runner.py --verify-reference-outputs
```

The runner reports min/avg/max wall time and peak RSS, and refuses to time
any program whose output does not match its `.expected` file. Erlang
comparisons include ~1 s of VM startup, so short benchmarks look lopsided —
noted in the harness docs rather than quietly cropped.

## Writing fast Yona

### Prefer folds over repeated concatenation

`++` copies; building a list by appending in a loop is O(n²). Fold with cons
(O(1) on a uniquely-owned sequence) and reverse once, or use a comprehension:

```yona
import foldl, reverse from Std\List in
# O(n): cons then one reverse
reverse (foldl (\acc x -> (x * x) :: acc) [] [1, 2, 3, 4])
# => [1, 4, 9, 16]

# Simpler and fused:
[x * x for x = [1, 2, 3, 4]]
# => [1, 4, 9, 16]
```

`foldl` is loop-compiled — no stack growth on large inputs.

### Stream large data instead of materializing it

Functions like `readLines`, `chars`, and `split` return iterators, and
comprehensions over them run in constant memory. A 50 MB file costs 64 KB of
buffer, not 50 MB of sequence:

```yona
import readLines from Std\File, foldl from Std\List in
foldl (\n _ -> n + 1) 0 [1 for _ = readLines "big.log"]
# => the line count, in O(64 KB) memory
```

See [Iterators and streams](/guides/iterators/) for the full model and for
when a materialized `Seq` is the better choice (small data, random access,
multiple passes).

### Use parallel comprehensions for independent work

`[| … ]` evaluates the body across the thread pool; independent multi-`let`
bindings also auto-parallelize:

```yona
[| expensiveCheck x for x = candidates ]
# => same result as the sequential comprehension, elements computed concurrently
```

This is the shape behind the `par_map` 1.0x row: twenty independent bodies,
saturating the pool with no annotations beyond `[|`.

### Pick an optimization level

`yonac` compiles at `-O2` by default. `-O3` occasionally helps tight numeric
loops; `-O0` compiles fastest for development; `-g` adds DWARF debug info at
any level:

```bash
yonac -O3 -o hot hot.yona
yonac -O0 -g -o dev dev.yona
```

See [the CLI reference](/reference/cli/) for all flags.

### Columnar workloads: consider the GPU path

For large `IntArray` / `FloatArray` map/filter/reduce pipelines, the compiler
lowers recognized shapes to an accelerator ABI that can execute on a GPU when
one is available — with identical results on the CPU otherwise. Crossover
benchmarks (CPU-forced vs GPU-opted-in on the same binaries) ship with the
harness:

```bash
python3 bench/run_gpu_compare.py            # CPU vs Vulkan wall times
python3 bench/run_gpu_compare.py --json-report
```

See [Accelerators (GPU)](/guides/accelerators/) for the execution model, the
supported kernel library, and the tuning knobs.

## Reproducing and reporting

If you measure something different, the most useful report includes the
harness output (`--json`), your hardware, and the compiler version. Benchmark
results published by the project are always tied to the harness so they can be
re-run — treat any Yona performance claim that can't be reproduced with
`bench/runner.py` as suspect, including ours.
