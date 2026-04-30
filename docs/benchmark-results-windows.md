# Benchmark Results - Windows

**Date**: 2026-04-24  
**Platform**: Windows 11 (build 26220)

**Provenance**: Tables regenerated with:

- `set YONAC_CC=C:\local\LLVM\bin\clang.exe`
- `python bench/runner.py --yonac out/build/x64-debug/yonac.exe -n 10 -O 2 --compare "c,erl,java,hs,js,py" --json`
- Raw output: `bench/windows-full-bench-2026-04-24-n10-v011.json`

## Summary

- **35/35 benchmarks passing** on Windows (Yona lane).
- C lanes use Windows-specific refs (`*.win.c`) when available.
- Comparison coverage this run: C 35/35, Erlang 0/35, Haskell 35/35, Java 35/35, Node.js 35/35, Python 35/35.
- Erlang lane unavailable in this environment (`erl.exe`/`erlc.exe` crash with `0xC0000005`).

## Startup-adjusted numbers

The runner reports adjusted values (`app_time = max(total - startup, 0.01ms)`).

### Measured cold-start floors (this machine, this run)

| Runtime | Startup time | Startup RSS |
|---------|-------------:|------------:|
| Yona | 10.7 ms | 6.2 MB |
| C | 9.05 ms | 3.9 MB |
| Erlang | — | — |
| Haskell | 26 ms | 10.5 MB |
| Java | 54.2 ms | 40.3 MB |
| Node.js | 40.4 ms | 32.3 MB |
| Python | 22.8 ms | 10.9 MB |

## CPU / Algorithms (adjusted ms)

| Benchmark | Yona | C | Erlang | Haskell | Java | Node.js | Python |
|-----------|-----:|--:|-------:|--------:|-----:|--------:|-------:|
| fibonacci(35) | 107 | 104 | — | 17.1 | 11.4 | 54.3 | 637 |
| tak(30,20,10) | 156 | 159 | — | 73 | 46 | 204 | 2.27e+03 |
| sieve(500) | 7.27 | 0.267 | — | 0.01 | 0.01 | 0.01 | 0.01 |
| sort(200) | 4.54 | 0.549 | — | 0.01 | 10.5 | 2.16 | 6.06 |
| ackermann(3,11) | 251 | 249 | — | 282 | 147 | 229 | 1.04e+04 |
| queens(10) | 19.7 | 18.6 | — | 0.01 | 5.76 | 12.7 | 88.1 |
| sum_squares(1M) | 2.2 | 1.11 | — | 0.01 | 0.01 | 12.3 | 69.6 |

## Collections (adjusted ms)

| Benchmark | Yona | C | Erlang | Haskell | Java | Node.js | Python |
|-----------|-----:|--:|-------:|--------:|-----:|--------:|-------:|
| dict_build (10K) | 18.8 | 2.33 | — | 1.94 | 11.9 | 1.6 | 0.7 |
| set_build (10K) | 17.8 | 1.3 | — | 0.01 | 8.6 | 0.01 | 0.01 |
| list_map_filter | 3.6 | 4.28 | — | 0.01 | 14.5 | 0.01 | 0.01 |
| list_reverse | 4.02 | 5.18 | — | 0.01 | 0.01 | 0.01 | 0.01 |
| list_sum | 2.27 | 1.37 | — | 0.01 | 8.46 | 0.01 | 0.01 |
| int_array_fill_sum | 2.05 | 1.82 | — | 0.01 | 0.01 | 0.01 | 2.56 |
| int_array_map | 3.5 | 2.3 | — | 0.01 | 0.01 | 0.01 | 3.18 |
| int_array_sum | 4.39 | 1.57 | — | 0.01 | 0.01 | 0.01 | 2.55 |

## Concurrency (adjusted ms)

| Benchmark | Yona | C | Erlang | Haskell | Java | Node.js | Python |
|-----------|-----:|--:|-------:|--------:|-----:|--------:|-------:|
| par_map (20 cubes) | 3.91 | 5.38 | — | 0.01 | 0.931 | 0.01 | 18.2 |
| parallel_async | 117 | 113 | — | 95.8 | 104 | 106 | 180 |
| sequential_async | 442 | 437 | — | 397 | 427 | 426 | 507 |
| channel_pipeline | 2.64 | 0.945 | — | 0.01 | 18 | 0.01 | 10.1 |
| channel_fanin | 2.58 | 0.427 | — | 0.01 | 7.39 | 0.01 | 11.4 |
| channel_throughput | 3.64 | 2.12 | — | 0.01 | 22.9 | 0.01 | 13.8 |
| seq_map | 1.21 | 3.68 | — | 0.01 | 0.01 | 0.01 | 0.01 |
| task_group_arena | 2.37 | 1.53 | — | 0.01 | 0.01 | 0.01 | 0.01 |

## I/O - small files (adjusted ms)

| Benchmark | Yona | C | Erlang | Haskell | Java | Node.js | Python |
|-----------|-----:|--:|-------:|--------:|-----:|--------:|-------:|
| binary_read_chunks | 5.37 | 1.9 | — | 5.25 | 0.01 | 0.01 | 0.01 |
| binary_write_read | 5.44 | 5.13 | — | 2.46 | 9.15 | 1.54 | 2.27 |
| file_read | 1.47 | 2.15 | — | 0.01 | 20.5 | 0.01 | 0.01 |
| file_write_read | 1.82 | 2.21 | — | 7.83 | 4.88 | 0.953 | 1.84 |
| file_parallel_read | 2.67 | 2.13 | — | 0.01 | 13.8 | 0.116 | 19.6 |
| file_readlines | 5.33 | 3.73 | — | 9.47 | 31.6 | 1.49 | 1.23 |
| process_exec | 27.1 | 43.5 | — | 32.6 | 59 | 41.9 | 44.9 |
| process_spawn | 225 | 206 | — | 173 | 226 | 207 | 191 |

## I/O - large files (50 MB, adjusted ms)

| Benchmark | Yona | C | Erlang | Haskell | Java | Node.js | Python |
|-----------|-----:|--:|-------:|--------:|-----:|--------:|-------:|
| file_read_large | 12.7 | 29.4 | — | 9.28 | 26.8 | 45 | 18.3 |
| file_write_read_large | 103 | 159 | — | 137 | 85.1 | 330 | 449 |
| file_parallel_read_large | 9.66 | 1.68 | — | 3.78 | 28.5 | 9.29 | 28.7 |
| file_readlines_large | 65.1 | 136 | — | 15.5 | 86 | 103 | 73.2 |

## Memory (raw peak RSS, MB)

Windows runner captures per-process peak working set (KB) via native APIs.
The table below summarizes per-runtime peak RSS across benchmark rows.

| Runtime | Min MB | Median MB | Max MB |
|---------|-------:|----------:|-------:|
| Yona | 6.2 | 6.7 | 111.3 |
| C | 4.0 | 4.0 | 8.7 |
| Haskell | 10.3 | 11.8 | 115.3 |
| Java | 40.4 | 41.5 | 203.7 |
| Node.js | 34.6 | 35.7 | 140.4 |
| Python | 10.9 | 11.8 | 115.9 |

## Delta vs 2026-04-22 run (Yona adjusted time)

- Median delta across shared rows: **-40.2%**
- Mean delta across shared rows: **-35.0%**

Largest regressions:
- `file_write_read_large`: +52.7% (67.8 -> 103 ms)
- `set_build (10K)`: +6.8% (16.7 -> 17.8 ms)
- `fibonacci(35)`: +1.6% (106 -> 107 ms)
- `sequential_async`: -0.1% (443 -> 442 ms)
- `tak(30,20,10)`: -0.4% (157 -> 156 ms)
- `parallel_async`: -0.7% (118 -> 117 ms)

Largest improvements:
- `file_write_read`: -79.4% (8.8 -> 1.82 ms)
- `file_read`: -72.5% (5.33 -> 1.47 ms)
- `file_parallel_read`: -71.3% (9.29 -> 2.67 ms)
- `sum_squares(1M)`: -71.3% (7.66 -> 2.2 ms)
- `seq_map`: -68.0% (3.79 -> 1.21 ms)
- `list_sum`: -65.6% (6.6 -> 2.27 ms)

## Stability pass (3 full reruns, `-n 10`)

Additional full reruns were captured to estimate run-to-run variance:

- `bench/windows-full-bench-2026-04-24-n10-v011-run1.json`
- `bench/windows-full-bench-2026-04-24-n10-v011-run2.json`
- `bench/windows-full-bench-2026-04-24-n10-v011-run3.json`

All three reruns passed **35/35** on the Yona lane.

Large-file I/O (Yona adjusted time) variability summary:

| Benchmark | Run1 | Run2 | Run3 | Median | CV |
|-----------|-----:|-----:|-----:|-------:|---:|
| file_read_large | 15.531 | 16.844 | 15.950 | 15.950 | 4.2% |
| file_write_read_large | 201.256 | 143.128 | 112.032 | 143.128 | 29.8% |
| file_parallel_read_large | 11.793 | 12.073 | 11.302 | 11.793 | 3.3% |
| file_readlines_large | 77.622 | 65.338 | 68.099 | 68.099 | 9.2% |

Takeaway:

- `file_write_read_large` is the only consistently high-variance large-file row in this pass.
- Most other large-file rows are reasonably stable (single-digit CV), so trend claims there are more trustworthy.
- Across all 35 rows, 15/35 are <=10% CV; short-running microbenches remain the noisiest.

## Std\GPU / Vulkan crossover (procedure)

Crossover timings for transparent lowering are **not** part of the main
`bench/runner.py` matrix: use `bench/run_gpu_compare.py` on this machine (or CI
with a GPU worker). Recommended capture:

```text
set YONAC_CC=C:\local\LLVM\bin\clang.exe
py -3 bench/gpu_bench_meta.py > gpu-bench-meta.json
py -3 bench/run_gpu_compare.py -n 10 -O2 --json-report > gpu-compare-report.json
```

Archive both JSON files next to a short note (GPU model, driver, integrated vs
discrete). Re-run after runtime or shader changes. Problem sizes are documented
in `bench/README.md` (hot vs `*_10k` / `*_5k` accelerators). See
`docs/gpu-transparent-lowering.md` for how this feeds the crossover cost model.

## Caveats

- Startup-adjusted floor can exaggerate ratios when values clamp to `0.01ms`.
- This report uses warm-cache behavior for file workloads.
- Startup RSS values are cached per runtime; rerun after toolchain/runtime changes.
- Inter-run deltas include noise from filesystem cache and process scheduling.
