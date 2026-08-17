# Benchmark Results - macOS

**Date**: 2026-08-17
**Platform**: macOS 27.0 (build 26A5406e, Darwin 27.0.0), Apple M3, 16 GB RAM

**Provenance**: Tables regenerated with:

- `export PATH="$(brew --prefix llvm)/bin:$(brew --prefix openjdk)/bin:$PATH"`
- `export YONAC_CC=$(command -v clang)`  # Homebrew LLVM 22.1.8
- `python3 bench/runner.py --yonac out/build/x64-debug-macos/yonac -n 10 -O 2 --compare "c,erl,java,hs,js,py" --json --save`
- Raw output: `bench/macos-full-bench-2026-08-17-n10.log`
- Machine-readable: `bench/macos-full-bench-2026-08-17-n10.json`
- GPU crossover: `bench/macos-gpu-compare-2026-08-17-n10.json`, `bench/macos-gpu-bench-meta-2026-08-17.json`

**Compilers / Runtimes**:
- Yona `-O2` (debug `yonac`, LLVM 22.1.8), CMake Vulkan ON
- C: Homebrew clang 22.1.8 `-O2`
- Haskell: GHC 9.14.1 `-O2`
- Java: OpenJDK 26.0.2
- Node.js: v26.7.0
- Python: 3.14.6
- Erlang/OTP 29 (erts-17.0.5)

## Summary

- **45/45 benchmarks passing** on macOS (Yona lane).
- Comparison coverage this run: C 35/45, Erlang 33/45, Haskell 28/45, Java 31/45, Node.js 35/45, Python 34/45.
- Erlang lane included (OTP 29).

## Startup-adjusted numbers

The runner reports adjusted values (`app_time = max(total - startup, 0.01ms)`).

### Measured cold-start floors (this machine, this run)

| Runtime | Startup time | Startup RSS |
|---------|-------------:|------------:|
| Yona | 12.6 ms | 5.4 MB |
| C | 9.12 ms | 1.6 MB |
| Erlang | 686 ms | 71.2 MB |
| Haskell | 23.8 ms | 11.2 MB |
| Java | 38.8 ms | 39.0 MB |
| Node.js | 60.6 ms | 42.8 MB |
| Python | 34.8 ms | 14.2 MB |

## CPU / Algorithms (adjusted ms)

| Benchmark | Yona | C | Erlang | Haskell | Java | Node.js | Python |
|-----------|-----:|--:|-------:|--------:|-----:|--------:|-------:|
| fibonacci(35) | 140 | 199 | 163 | 270 | 206 | 911 | 2.07e+03 |
| tak(30,20,10) | 111 | 96.6 | 0.01 | 125 | 81.3 | 226 | 3.74e+03 |
| sieve | 20.9 | 14.5 | 0.01 | — | — | 0.01 | 4.43 |
| sort(200) | 19.1 | 17.1 | 0.01 | 3.41 | 5.68 | 8.24 | 5.20 |
| ackermann | 919 | 1.35e+03 | 2.18e+03 | — | — | 2.3e+03 | — |
| queens(10) | 56.7 | 16.2 | 0.01 | 6.19 | 22.9 | 32.8 | 89.7 |
| sum_squares(1M) | 52.2 | 30.0 | 0.01 | 116 | 104 | 237 | 604 |

## Collections (adjusted ms)

| Benchmark | Yona | C | Erlang | Haskell | Java | Node.js | Python |
|-----------|-----:|--:|-------:|--------:|-----:|--------:|-------:|
| dict_build (10K) | 35.1 | 15.8 | 0.01 | 1.81 | 6.44 | 14.6 | 4.61 |
| set_build (10K) | 24.1 | 16.2 | 0.01 | 2.52 | 4.03 | 0.165 | 5.14 |
| list_map_filter | 16.1 | 18.9 | 0.01 | 131 | 130 | 204 | 28.6 |
| list_reverse | 21.6 | 17.7 | 0.01 | 4.04 | 2.06 | 1.58 | 4.48 |
| list_sum | 15.5 | 20.8 | 0.01 | 2.04 | 0.239 | 10.4 | 3.96 |
| list_sum_explicit_borrow | 17.0 | — | — | — | — | — | — |
| int_array_fill_sum | 14.4 | 17.4 | 0.01 | 2.25 | 0.01 | 0.01 | 12.6 |
| int_array_map | 19.3 | 15.3 | 0.01 | 2.28 | 2.97 | 3.79 | 5.80 |
| int_array_sum | 20.4 | 16.3 | 0.01 | 2.27 | 1.66 | 0.01 | 3.74 |

## Concurrency (adjusted ms)

| Benchmark | Yona | C | Erlang | Haskell | Java | Node.js | Python |
|-----------|-----:|--:|-------:|--------:|-----:|--------:|-------:|
| par_map (20 cubes) | 18.7 | 22.2 | 0.01 | — | 20.8 | 1.36 | 23.4 |
| parallel_async | 122 | 118 | 0.01 | 266 | 325 | 297 | 458 |
| sequential_async | 849 | 881 | 0.01 | 695 | 425 | 839 | 762 |
| channel_pipeline | 18.0 | 15.8 | 0.01 | 1.90 | 11.7 | 0.852 | 10.4 |
| channel_fanin | 17.8 | 24.2 | 37.2 | 120 | 170 | 188 | 24.3 |
| channel_throughput | 18.1 | 16.7 | 0.01 | 0.284 | 12.4 | 0.01 | 18.4 |
| seq_map | 38.4 | 27.2 | 109 | 123 | 119 | 197 | 94.7 |
| task_group_arena | 51.8 | 29.5 | 0.01 | 122 | 114 | 192 | 118 |

## I/O - small files (adjusted ms)

| Benchmark | Yona | C | Erlang | Haskell | Java | Node.js | Python |
|-----------|-----:|--:|-------:|--------:|-----:|--------:|-------:|
| binary_read_chunks | 29.7 | 16.9 | 0.01 | 0.01 | 18.5 | 12.1 | 5.48 |
| binary_write_read | 24.6 | 22.6 | 0.01 | 1.28 | 21.4 | 165 | 111 |
| file_read | 25.2 | 17.3 | 0.01 | 5.31 | 15.0 | 3.82 | 5.70 |
| file_write_read | 54.0 | 33.0 | 0.01 | 12.2 | 16.9 | 8.92 | 5.14 |
| file_parallel_read | 41.4 | 30.1 | 37.2 | — | 223 | 187 | 232 |
| file_readlines | 25.8 | — | 0.01 | — | — | 19.4 | 16.7 |
| process_exec | 57.9 | 39.6 | — | 24.2 | 55.1 | 37.1 | 144 |
| process_spawn | 34.4 | — | — | — | — | 124 | 261 |

## I/O - large files (50 MB, adjusted ms)

| Benchmark | Yona | C | Erlang | Haskell | Java | Node.js | Python |
|-----------|-----:|--:|-------:|--------:|-----:|--------:|-------:|
| file_read_large | 33.7 | 21.2 | 0.01 | 14.9 | 41.1 | 58.8 | 30.9 |
| file_write_read_large | 89.8 | 65.0 | 0.01 | 77.2 | 172 | 81.3 | 81.2 |
| file_parallel_read_large | 126 | 35.3 | 0.01 | — | 226 | 27.3 | 40.7 |
| file_readlines_large | 348 | 129 | 336 | 124 | 607 | 713 | 561 |

## Std\GPU accelerators (adjusted ms)

| Benchmark | Yona | C | Erlang | Haskell | Java | Node.js | Python |
|-----------|-----:|--:|-------:|--------:|-----:|--------:|-------:|
| gpu_map_reduce_hot | 722 | — | — | — | — | — | — |
| gpu_map_reduce_10k | 17.2 | — | — | — | — | — | — |
| gpu_filter_hot | 744 | — | — | — | — | — | — |
| gpu_filter_10k | 21.0 | — | — | — | — | — | — |
| gpu_columnar_pipeline | 168 | 20.2 | — | — | — | — | — |
| gpu_columnar_pipeline_5k | 34.2 | — | — | — | — | — | — |
| gpu_materialize_sum | 26.3 | 14.4 | — | — | — | — | — |
| gpu_materialize_sum_5k | 30.0 | — | — | — | — | — | — |
| gpu_float_scale_hot | 57.8 | — | — | — | — | — | — |

## Memory (raw peak RSS, MB)

macOS runner captures per-process peak RSS via `wait4` `ru_maxrss` (bytes → KB).
Each row is a **separate process**, so the max is not a leak across the suite.
Yona max is `file_readlines_large`: 500k lines / 55 MB file, materialized as
`[line for line = iter]` before `foldl` (every line string + seq cell live at once).
`file_write_read_large` (~111 MB) holds the 55 MB `readFile` buffer and the re-read copy together.
C/Python stream those files; Yona median ~6 MB matches the no-op floor.
The table below summarizes per-runtime peak RSS across benchmark rows.

| Runtime | Min MB | Median MB | Max MB |
|---------|-------:|----------:|-------:|
| Yona | 5.4 | 6.1 | 143.5 |
| C | 1.6 | 1.6 | 6.6 |
| Erlang | 70.9 | 71.6 | 193.2 |
| Haskell | 11.2 | 11.3 | 117.9 |
| Java | 39.0 | 41.4 | 204.3 |
| Node.js | 43.6 | 47.9 | 217.3 |
| Python | 14.2 | 14.9 | 119.5 |

## Std\GPU / Vulkan crossover (this machine)

Captured with `python3 bench/run_gpu_compare.py --yonac out/build/x64-debug-macos/yonac -n 10 -O2 --json-report`.
Device: Apple M3 via MoltenVK (no `shaderInt64` / typically no `shaderFloat64`; IntArray uses i32 when values fit, float scale uses f32).

| Benchmark | CPU avg (ms) | GPU avg (ms) | Status |
|-----------|-------------:|-------------:|--------|
| map_reduce | 738 | 817 | CPU faster (1.11x) |
| filter | 711 | 752 | CPU faster (1.06x) |
| pipeline | 40.6 | 60.8 | CPU faster (1.50x) |
| materialize | 39.5 | 56.1 | CPU faster (1.42x) |
| map_reduce_10k | 33.5 | 45.6 | CPU faster (1.36x) |
| filter_10k | 29.2 | 49.8 | CPU faster (1.70x) |
| pipeline_5k | 27.6 | 48.1 | CPU faster (1.74x) |
| materialize_5k | 29.0 | 43.4 | CPU faster (1.50x) |
| float_scale | 66.5 | 40.4 | GPU faster (1.65x) |

Archive both JSON files next to a short note (GPU model, MoltenVK / Metal).
Problem sizes are documented in `bench/README.md` (hot vs `*_10k` / `*_5k` accelerators).
See `docs/gpu-transparent-lowering.md` for how this feeds the crossover cost model.

## Caveats

- Startup-adjusted floor can exaggerate ratios when values clamp to `0.01ms`.
- This report uses warm-cache behavior for file workloads.
- Startup RSS values are cached per runtime; rerun after toolchain/runtime changes.
- `yonac` used here is the **debug** macOS build (`x64-debug-macos`) with `-DYONA_ENABLE_VULKAN=ON`.
- The main `bench/runner.py` matrix does not force CPU or Vulkan; accelerator rows use default `Std\GPU` discovery.
- Erlang/OTP 29 is on PATH; cells are `—` only when that row has no working `.erl` reference.
- Erlang cold-start is ~686 ms (BEAM boot). Most short rows clamp to the `0.01ms` adjusted floor; prefer raw times in the JSON for those cells.
- Comparison cells are `—` when a reference failed to compile, produced the wrong stdout, or has no implementation for that row.
- MoltenVK / Metal: IntArray GPU kernels use i32 when values fit; float scale uses f32. GPU column includes device submit overhead.
