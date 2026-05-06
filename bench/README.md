# Yona Benchmark Suite

## Quick Start

```bash
# Run all benchmarks (3 iterations, -O2)
python3 bench/runner.py

# Run specific benchmark
python3 bench/runner.py fibonacci

# Compare against C reference implementations
python3 bench/runner.py --compare-c

# Compare against Erlang (uses erlc, so run includes VM boot)
python3 bench/runner.py --compare-erl

# Compare against several languages at once
python3 bench/runner.py --compare=c,erl

# Compare all optimization levels (O0, O1, O2, O3)
python3 bench/runner.py --all-opt-levels

# Compare everything
python3 bench/runner.py --all-opt-levels --compare-c

# Custom iterations and optimization level
python3 bench/runner.py -n 5 -O3

# JSON output for CI/scripting
python3 bench/runner.py --json

# Std\GPU: CPU vs Vulkan wall times (same correctness checks)
python3 bench/run_gpu_compare.py
python3 bench/run_gpu_compare.py --only float_scale   # FloatArray gpu_stub f64 (golden 0)

# Reference lanes only: stdout must match each benchmark's .expected (CI-friendly; no yonac)
python3 bench/runner.py --verify-reference-outputs
python3 bench/runner.py core/fibonacci --verify-reference-outputs
python3 bench/runner.py --verify-reference-outputs --reference-verify-langs c,java
python3 bench/runner.py --verify-reference-outputs --reference-verify-langs all

# Windows: some OTP installs crash (`erlc`/`erl` exit -1073741819). Omit Erlang with:
#   `--skip-erl`  — same as env YONA_BENCH_SKIP_ERLANG=1 (drops erl from --compare-* / verify all)
python3 bench/runner.py --skip-erl --compare=c,erl,hs
```
Use Linux/WSL Erlang when you need the Erlang reference row unchanged.

## What It Measures

- **Wall-clock time** (ms) — min/avg/max over N iterations
- **Peak RSS** (KB) — maximum resident set size via `/usr/bin/time -v`
- **Correctness** — each benchmark has a `.expected` file verified before timing
- **Reference conformance** — `bench/runner.py --verify-reference-outputs`
  (optional `--reference-verify-langs`, default `c`) runs reference programs
  only (no Yona compile) and exits non-zero if any lane's stdout differs from
  the golden `.expected` file.
- **Reference comparison** — optional ratio vs C, Erlang, or both (via
  `--compare-c` / `--compare-erl` / `--compare=c,erl`). Erlang numbers
  include VM startup (~1s), so short benchmarks look lopsided.

## Structure

```
bench/
  runner.py              # Benchmark runner
  README.md              # This file
  core/                  # Core language benchmarks
    fibonacci.yona       # Recursive Fibonacci (function call overhead)
    tak.yona             # Takeuchi function (deep recursion)
    sieve.yona           # Sieve of Eratosthenes (list filtering)
    collections/           # Collection operation benchmarks
    concurrency/task_group_arena.yona  # multi-binding let + bump arena (vs C stack loop)
    list_sum.yona        # Sum 100K elements (fold)
    list_reverse.yona    # Reverse 10K list (foldl + cons)
    list_map_filter.yona # Map + filter + fold pipeline
  numeric/               # Numeric computation benchmarks
    ackermann.yona       # Ackermann function (deep recursion)
    sum_squares.yona     # Sum of squares (tight loop, TCE)
  accelerators/           # CPU/GPU-style columnar execution benchmarks
    gpu_columnar_pipeline.yona  # upload -> map -> filter -> reduce (20k rows)
    gpu_columnar_pipeline_5k.yona  # same pipeline at 5k rows (crossover curve)
    gpu_materialize_sum.yona    # upload -> map -> materialize -> fold
    gpu_materialize_sum_5k.yona # same at 5k rows
    gpu_map_reduce_hot.yona     # upload -> mapAdd -> reduceSum (100k rows, timing)
    gpu_map_reduce_10k.yona     # same at 10k rows
    gpu_filter_hot.yona         # upload -> filterGreaterThan -> reduceSum (100k rows)
    gpu_filter_10k.yona           # same at 10k rows
    gpu_float_scale_hot.yona      # FloatArray scale async (gpu_stub); stdout must match .expected
  reference/             # Reference implementations (C, Erlang, Haskell,
    fibonacci.c          # Java, JavaScript, Python). Only .c and .erl are
    fibonacci.erl        # currently wired into the runner's comparison
    fibonacci.hs         # output; the others are kept for reading.
    ...
```

## Adding a Benchmark

1. Create `bench/<category>/<name>.yona`
2. Create `bench/<category>/<name>.expected` — expected output
3. Optionally `bench/reference/<name>.c` — C equivalent
4. Optionally `bench/reference/<name>.erl` — Erlang equivalent. Use the
   escript shebang (`#!/usr/bin/env escript`) and export `main/1`. The
   runner rewrites it to a regular module and compiles with `erlc`
   before timing.

The runner discovers all `.yona` files automatically. Each benchmark has a
10-second timeout to prevent runaway execution.

## GPU / Vulkan (`Std\GPU`)

The accelerators under `bench/accelerators/` use `Std\GPU` (CPU SIMD by default;
optional Vulkan when the compiler and runtime support it). The main benchmark
runner does **not** set GPU env vars — each process uses normal capability
detection.

To compare **CPU-forced** vs **Vulkan opt-in** wall times on the same sources:

```bash
python3 bench/run_gpu_compare.py
python3 bench/run_gpu_compare.py -n 5 -O3 --only map_reduce
python3 bench/gpu_bench_meta.py   # JSON: build N / filter thresholds / let N bindings per gpu_*.yona
```

The script sets `YONA_GPU_DISABLE_VULKAN=1` for the first pass and
`YONA_GPU_VULKAN_COMPUTE=1` plus `YONA_GPU_VULKAN_MIN_LEN=1` for the second.
Output must match the `.expected` file for both passes. It prints a summary
table: **CPU avg (ms)** vs **GPU avg (ms)** (Vulkan opt-in), **Delta %**, and a
short verdict (e.g. `GPU faster (1.2x)`). On machines without a usable GPU,
the GPU column may match CPU time (same kernels). Programs using
**`Std\GPU.floatArray*Async`** (native `Promise` + optional `VkDevice` init)
are **not** in the fixed `BENCHES` list yet — add them after a deterministic
bench harness can init Vulkan or asserts a shared error outcome for both env columns.

Use `--json` for a machine-readable list after the table (array of results).
Use `--json-report` for the same data wrapped with host / yonac / `-O` / iteration
metadata (`docs/gpu-transparent-lowering.md` — benchmark corpus for crossover work).

See `docs/gpu-architecture.md` for all `YONA_GPU_VULKAN_*` variables (including
`YONA_GPU_VULKAN_FILTER_CPU_PREFIX=1` to A/B the legacy host prefix for filter).

Compiler **call-site JSON** (no codegen): `yonac bench/accelerators/gpu_map_reduce_10k.yona -I lib
--emit-accelerator-report` (modules: add `--emit-accelerator-report-with-types` for a typechecked
`report_kind` of `module`) — see `docs/gpu-transparent-lowering.md` (*Rollout* step 2).

## `@borrow` vs inference (parity, not speedup)

For parameters where **borrow inference** already applies (no escape in the
function body), **`@borrow` is redundant for codegen**: the same refcount
call sites are optimized. Explicit `@borrow` still helps as a checked
contract and catches future body edits that would break borrow rules.

`collections/list_sum.yona` and `collections/list_sum_explicit_borrow.yona`
are the same workload; comparing them with `python3 bench/runner.py
collections/list_sum` is a **parity / regression** check (wall times should
match within noise). They do **not** demonstrate a speed benefit of explicit
annotations over inference.

## Known Limitations

- Sieve uses O(n²) list filtering — expected to be slow
- No REPL-level benchmarks yet (compile time not measured)
- Erlang comparison includes VM startup overhead (~1s); short-running
  benchmarks will show very low Yona/Erl ratios that mostly reflect
  boot cost, not computational speed
