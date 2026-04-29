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
```

## What It Measures

- **Wall-clock time** (ms) — min/avg/max over N iterations
- **Peak RSS** (KB) — maximum resident set size via `/usr/bin/time -v`
- **Correctness** — each benchmark has a `.expected` file verified before timing
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
    gpu_columnar_pipeline.yona  # upload -> map -> filter -> reduce
    gpu_materialize_sum.yona    # upload -> map -> materialize -> fold
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

## Known Limitations

- Sieve uses O(n²) list filtering — expected to be slow
- No REPL-level benchmarks yet (compile time not measured)
- Erlang comparison includes VM startup overhead (~1s); short-running
  benchmarks will show very low Yona/Erl ratios that mostly reflect
  boot cost, not computational speed
