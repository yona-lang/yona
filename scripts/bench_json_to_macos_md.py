#!/usr/bin/env python3
"""Turn bench/runner.py --json output into docs/benchmark-results-macos.md."""

from __future__ import annotations

import json
import math
import statistics
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

SECTIONS = [
    (
        "CPU / Algorithms (adjusted ms)",
        [
            "core/fibonacci",
            "core/tak",
            "core/sieve",
            "core/sort",
            "numeric/ackermann",
            "core/queens",
            "numeric/sum_squares",
        ],
    ),
    (
        "Collections (adjusted ms)",
        [
            "collections/dict_build",
            "collections/set_build",
            "collections/list_map_filter",
            "collections/list_reverse",
            "collections/list_sum",
            "collections/list_sum_explicit_borrow",
            "collections/int_array_fill_sum",
            "collections/int_array_map",
            "collections/int_array_sum",
        ],
    ),
    (
        "Concurrency (adjusted ms)",
        [
            "concurrency/par_map",
            "concurrency/parallel_async",
            "concurrency/sequential_async",
            "concurrency/channel_pipeline",
            "concurrency/channel_fanin",
            "concurrency/channel_throughput",
            "concurrency/seq_map",
            "concurrency/task_group_arena",
        ],
    ),
    (
        "I/O - small files (adjusted ms)",
        [
            "io/binary_read_chunks",
            "io/binary_write_read",
            "io/file_read",
            "io/file_write_read",
            "io/file_parallel_read",
            "io/file_readlines",
            "io/process_exec",
            "io/process_spawn",
        ],
    ),
    (
        "I/O - large files (50 MB, adjusted ms)",
        [
            "io/file_read_large",
            "io/file_write_read_large",
            "io/file_parallel_read_large",
            "io/file_readlines_large",
        ],
    ),
    (
        "Std\\GPU accelerators (adjusted ms)",
        [
            "accelerators/gpu_map_reduce_hot",
            "accelerators/gpu_map_reduce_10k",
            "accelerators/gpu_filter_hot",
            "accelerators/gpu_filter_10k",
            "accelerators/gpu_columnar_pipeline",
            "accelerators/gpu_columnar_pipeline_5k",
            "accelerators/gpu_materialize_sum",
            "accelerators/gpu_materialize_sum_5k",
            "accelerators/gpu_float_scale_hot",
        ],
    ),
]

LABELS = {
    "core/fibonacci": "fibonacci(35)",
    "core/tak": "tak(30,20,10)",
    "core/sieve": "sieve",
    "core/sort": "sort(200)",
    "numeric/ackermann": "ackermann",
    "core/queens": "queens(10)",
    "numeric/sum_squares": "sum_squares(1M)",
    "collections/dict_build": "dict_build (10K)",
    "collections/set_build": "set_build (10K)",
    "collections/list_map_filter": "list_map_filter",
    "collections/list_reverse": "list_reverse",
    "collections/list_sum": "list_sum",
    "collections/list_sum_explicit_borrow": "list_sum_explicit_borrow",
    "collections/int_array_fill_sum": "int_array_fill_sum",
    "collections/int_array_map": "int_array_map",
    "collections/int_array_sum": "int_array_sum",
    "concurrency/par_map": "par_map (20 cubes)",
    "concurrency/parallel_async": "parallel_async",
    "concurrency/sequential_async": "sequential_async",
    "concurrency/channel_pipeline": "channel_pipeline",
    "concurrency/channel_fanin": "channel_fanin",
    "concurrency/channel_throughput": "channel_throughput",
    "concurrency/seq_map": "seq_map",
    "concurrency/task_group_arena": "task_group_arena",
    "io/binary_read_chunks": "binary_read_chunks",
    "io/binary_write_read": "binary_write_read",
    "io/file_read": "file_read",
    "io/file_write_read": "file_write_read",
    "io/file_parallel_read": "file_parallel_read",
    "io/file_readlines": "file_readlines",
    "io/process_exec": "process_exec",
    "io/process_spawn": "process_spawn",
    "io/file_read_large": "file_read_large",
    "io/file_write_read_large": "file_write_read_large",
    "io/file_parallel_read_large": "file_parallel_read_large",
    "io/file_readlines_large": "file_readlines_large",
    "accelerators/gpu_map_reduce_hot": "gpu_map_reduce_hot",
    "accelerators/gpu_map_reduce_10k": "gpu_map_reduce_10k",
    "accelerators/gpu_filter_hot": "gpu_filter_hot",
    "accelerators/gpu_filter_10k": "gpu_filter_10k",
    "accelerators/gpu_columnar_pipeline": "gpu_columnar_pipeline",
    "accelerators/gpu_columnar_pipeline_5k": "gpu_columnar_pipeline_5k",
    "accelerators/gpu_materialize_sum": "gpu_materialize_sum",
    "accelerators/gpu_materialize_sum_5k": "gpu_materialize_sum_5k",
    "accelerators/gpu_float_scale_hot": "gpu_float_scale_hot",
}

LANGS = [("c", "C"), ("erl", "Erlang"), ("hs", "Haskell"), ("java", "Java"), ("js", "Node.js"), ("py", "Python")]


def fmt_ms(v):
    if v is None:
        return "—"
    if v >= 1000:
        return f"{v:.3g}"
    if v >= 100:
        return f"{v:.0f}"
    if v >= 10:
        return f"{v:.1f}"
    if v >= 1:
        return f"{v:.2f}"
    return f"{v:.3g}"


def load_json_object(path: Path):
    text = path.read_text()
    decoder = json.JSONDecoder()
    idx = 0
    while True:
        start = text.find("{", idx)
        if start < 0:
            raise SystemExit(f"no JSON object in {path}")
        try:
            data, _end = decoder.raw_decode(text, start)
        except json.JSONDecodeError:
            idx = start + 1
            continue
        if isinstance(data, dict):
            return data
        idx = start + 1


def load_results(path: Path):
    data = load_json_object(path)
    if "O2" in data:
        rows = data["O2"]
    else:
        rows = next(iter(data.values()))
    by_name = {r["name"]: r for r in rows}
    return rows, by_name


def cell(row, key):
    if not row or row.get("status") != "ok":
        return "—"
    if key == "yona":
        return fmt_ms(row.get("avg_ms_adj"))
    return fmt_ms(row.get(f"{key}_avg_ms_adj"))


def coverage(rows, key):
    ok = sum(1 for r in rows if r.get("status") == "ok" and f"{key}_avg_ms_adj" in r)
    total = sum(1 for r in rows if r.get("status") == "ok")
    return ok, total


def main():
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "bench" / "macos-full-bench-2026-08-17-n10.json"
    gpu_src = Path(sys.argv[2]) if len(sys.argv) > 2 else None
    rows, by_name = load_results(src)
    yona_ok = sum(1 for r in rows if r.get("status") == "ok")
    yona_n = len(rows)
    startup = {}
    for r in rows:
        if r.get("status") != "ok":
            continue
        if "Yona" not in startup and r.get("startup_ms") is not None:
            startup["Yona"] = (r.get("startup_ms"), r.get("startup_rss_kb"))
        for key, label in LANGS:
            if label not in startup and f"{key}_startup_ms" in r:
                startup[label] = (r.get(f"{key}_startup_ms"), r.get(f"{key}_startup_rss_kb"))

    cov = []
    for key, label in LANGS:
        ok, total = coverage(rows, key)
        if total:
            cov.append(f"{label} {ok}/{total}")

    lines = []
    lines.append("# Benchmark Results - macOS")
    lines.append("")
    lines.append("**Date**: 2026-08-17")
    lines.append("**Platform**: macOS 27.0 (build 26A5406e, Darwin 27.0.0), Apple M3, 16 GB RAM")
    lines.append("")
    lines.append("**Provenance**: Tables regenerated with:")
    lines.append("")
    lines.append("- `export PATH=\"$(brew --prefix llvm)/bin:$(brew --prefix openjdk)/bin:$PATH\"`")
    lines.append("- `export YONAC_CC=$(command -v clang)`  # Homebrew LLVM 22.1.8")
    lines.append("- `python3 bench/runner.py --yonac out/build/arm64-debug-macos/yonac -n 10 -O 2 --compare \"c,erl,java,hs,js,py\" --json --save`")
    lines.append("- Raw output: `bench/macos-full-bench-2026-08-17-n10.log`")
    lines.append("- Machine-readable: `bench/macos-full-bench-2026-08-17-n10.json`")
    lines.append("- GPU crossover: `bench/macos-gpu-compare-2026-08-17-n10.json`, `bench/macos-gpu-bench-meta-2026-08-17.json`")
    lines.append("")
    lines.append("**Compilers / Runtimes**:")
    lines.append("- Yona `-O2` (debug `yonac`, LLVM 22.1.8), CMake Vulkan ON")
    lines.append("- C: Homebrew clang 22.1.8 `-O2`")
    lines.append("- Haskell: GHC 9.14.1 `-O2`")
    lines.append("- Java: OpenJDK 26.0.2")
    lines.append("- Node.js: v26.7.0")
    lines.append("- Python: 3.14.6")
    lines.append("- Erlang/OTP 29 (erts-17.0.5)")
    lines.append("")
    lines.append("## Summary")
    lines.append("")
    lines.append(f"- **{yona_ok}/{yona_n} benchmarks passing** on macOS (Yona lane).")
    lines.append("- Comparison coverage this run: " + ", ".join(cov) + ".")
    lines.append("- Erlang lane included (OTP 29).")
    lines.append("")
    lines.append("## Startup-adjusted numbers")
    lines.append("")
    lines.append("The runner reports adjusted values (`app_time = max(total - startup, 0.01ms)`).")
    lines.append("")
    lines.append("### Measured cold-start floors (this machine, this run)")
    lines.append("")
    lines.append("| Runtime | Startup time | Startup RSS |")
    lines.append("|---------|-------------:|------------:|")
    order = ["Yona", "C", "Erlang", "Haskell", "Java", "Node.js", "Python"]
    for label in order:
        if label not in startup:
            if label == "Erlang":
                lines.append("| Erlang | — | — |")
            continue
        ms, rss = startup[label]
        rss_mb = (rss or 0) / 1024.0
        lines.append(f"| {label} | {fmt_ms(ms)} ms | {rss_mb:.1f} MB |")
    lines.append("")

    for title, names in SECTIONS:
        lines.append(f"## {title}")
        lines.append("")
        lines.append("| Benchmark | Yona | C | Erlang | Haskell | Java | Node.js | Python |")
        lines.append("|-----------|-----:|--:|-------:|--------:|-----:|--------:|-------:|")
        for name in names:
            row = by_name.get(name)
            label = LABELS.get(name, name.split("/")[-1])
            cells = [cell(row, "yona")]
            for key, _ in LANGS:
                cells.append(cell(row, key))
            lines.append("| " + label + " | " + " | ".join(cells) + " |")
        lines.append("")

    rss_by_lang = {"Yona": []}
    for key, label in LANGS:
        rss_by_lang[label] = []
    for r in rows:
        if r.get("status") != "ok":
            continue
        if r.get("peak_rss_kb"):
            rss_by_lang["Yona"].append(r["peak_rss_kb"] / 1024.0)
        for key, label in LANGS:
            kb = r.get(f"{key}_rss_kb")
            if kb:
                rss_by_lang[label].append(kb / 1024.0)

    lines.append("## Memory (raw peak RSS, MB)")
    lines.append("")
    lines.append("macOS runner captures per-process peak RSS via `wait4` `ru_maxrss` (bytes → KB).")
    lines.append("Each row is a **separate process**, so the max is not a leak across the suite.")
    lines.append("Yona max is `file_readlines_large`: 500k lines / 55 MB file, materialized as")
    lines.append("`[line for line = iter]` before `foldl` (every line string + seq cell live at once).")
    lines.append("`file_write_read_large` (~111 MB) holds the 55 MB `readFile` buffer and the re-read copy together.")
    lines.append("C/Python stream those files; Yona median ~6 MB matches the no-op floor.")
    lines.append("The table below summarizes per-runtime peak RSS across benchmark rows.")
    lines.append("")
    lines.append("| Runtime | Min MB | Median MB | Max MB |")
    lines.append("|---------|-------:|----------:|-------:|")
    for label in order:
        vals = rss_by_lang.get(label) or []
        if not vals:
            if label == "Erlang":
                lines.append("| Erlang | — | — | — |")
            continue
        lines.append(
            f"| {label} | {min(vals):.1f} | {statistics.median(vals):.1f} | {max(vals):.1f} |"
        )
    lines.append("")

    if gpu_src and gpu_src.exists():
        gpu = load_json_object(gpu_src)
        if isinstance(gpu, list):
            gpu_rows = gpu
        elif isinstance(gpu, dict):
            gpu_rows = gpu.get("benches") or gpu.get("rows") or gpu.get("results") or []
        else:
            gpu_rows = []
        lines.append("## Std\\GPU / Vulkan crossover (this machine)")
        lines.append("")
        lines.append("Captured with `python3 bench/run_gpu_compare.py --yonac out/build/arm64-debug-macos/yonac -n 10 -O2 --json-report`.")
        lines.append("Device: Apple M3 via MoltenVK (no `shaderInt64` / typically no `shaderFloat64`; IntArray uses i32 when values fit, float scale uses f32).")
        lines.append("")
        if gpu_rows:
            lines.append("| Benchmark | CPU avg (ms) | GPU avg (ms) | Status |")
            lines.append("|-----------|-------------:|-------------:|--------|")
            for gr in gpu_rows:
                if not isinstance(gr, dict):
                    continue
                status = gr.get("status", "")
                if status == "ok" and gr.get("cpu_avg") and gr.get("vk_avg"):
                    ca, va = gr["cpu_avg"], gr["vk_avg"]
                    if va < ca:
                        status = f"GPU faster ({ca / va:.2f}x)"
                    elif va > ca:
                        status = f"CPU faster ({va / ca:.2f}x)"
                    else:
                        status = "tie"
                lines.append(
                    f"| {gr.get('benchmark', '?')} | {fmt_ms(gr.get('cpu_avg'))} | "
                    f"{fmt_ms(gr.get('vk_avg'))} | {status} |"
                )
            lines.append("")
        else:
            lines.append(f"See raw report: `{gpu_src.relative_to(ROOT)}`.")
            lines.append("")
        lines.append("Archive both JSON files next to a short note (GPU model, MoltenVK / Metal).")
        lines.append("Problem sizes are documented in `bench/README.md` (hot vs `*_10k` / `*_5k` accelerators).")
        lines.append("See `docs/gpu-transparent-lowering.md` for how this feeds the crossover cost model.")
        lines.append("")

    lines.append("## Caveats")
    lines.append("")
    lines.append("- Startup-adjusted floor can exaggerate ratios when values clamp to `0.01ms`.")
    lines.append("- This report uses warm-cache behavior for file workloads.")
    lines.append("- Startup RSS values are cached per runtime; rerun after toolchain/runtime changes.")
    lines.append("- `yonac` used here is the native Apple Silicon debug macOS build (`arm64-debug-macos`) with `-DYONA_ENABLE_VULKAN=ON`.")
    lines.append("- The main `bench/runner.py` matrix does not force CPU or Vulkan; accelerator rows use default `Std\\GPU` discovery.")
    lines.append("- Erlang/OTP 29 is on PATH; cells are `—` only when that row has no working `.erl` reference.")
    lines.append("- Erlang cold-start is ~686 ms (BEAM boot). Most short rows clamp to the `0.01ms` adjusted floor; prefer raw times in the JSON for those cells.")
    lines.append("- Comparison cells are `—` when a reference failed to compile, produced the wrong stdout, or has no implementation for that row.")
    lines.append("- MoltenVK / Metal: IntArray GPU kernels use i32 when values fit; float scale uses f32. GPU column includes device submit overhead.")
    lines.append("")
    out = ROOT / "docs" / "benchmark-results-macos.md"
    out.write_text("\n".join(lines))
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
