#!/usr/bin/env python3
"""
Time Std\\GPU columnar benches twice: CPU-forced vs Vulkan opt-in.

Requires a yonac built with Vulkan headers (`YONA_COMPILE_GPU_VULKAN`). If no
GPU or init fails, the "Vulkan" row still runs but falls back to the same CPU
implementation — compare wall times on a machine with a discrete GPU + drivers.
On Windows, **`VULKAN_SDK`** must be set when running yonac so **`Std\GPU` float**
benchmarks (`float_scale`) can link **`vulkan-1.lib`**.

Usage:
  python3 bench/run_gpu_compare.py
  python3 bench/run_gpu_compare.py -n 5 -O2
  python3 bench/run_gpu_compare.py --only map_reduce

Env (see docs/gpu-architecture.md):
  YONA_GPU_VULKAN_COMPUTE=1  (set by this script for the Vulkan column)

Machine-readable output:
  --json          array of per-bench dicts (backward compatible)
  --json-report   same benches plus meta (schema yona.gpu_compare.v1); for crossover logs
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BENCH = ROOT / "bench" / "accelerators"


def _default_yonac() -> Path:
    for rel in (
        "out/build/x64-debug-macos/yonac",
        "out/build/x64-release-macos/yonac",
        "out/build/x64-debug-linux/yonac",
        "out/build/x64-release-linux/yonac",
        "out/build/x64-debug/yonac.exe",
        "out/build/x64-release/yonac.exe",
    ):
        p = ROOT / rel
        if p.exists():
            return p
    return ROOT / "out/build/x64-debug/yonac.exe"


def _default_sysroot() -> Path:
    br = _default_yonac().parent
    if (br / "runtime").exists():
        return br
    return ROOT


def _tool_env() -> dict:
    env = os.environ.copy()
    extra: list[str] = []
    if os.name == "nt":
        llvm_bin = Path("C:/local/LLVM/bin")
        if llvm_bin.exists():
            extra.append(str(llvm_bin))
    if extra:
        env["PATH"] = os.pathsep.join(extra + [env.get("PATH", "")])
    return env


def _exe_suffix(path: Path) -> Path:
    if os.name == "nt" and path.suffix.lower() != ".exe":
        return Path(str(path) + ".exe")
    return path


BENCHES = (
    ("map_reduce", "gpu_map_reduce_hot.yona"),
    ("filter", "gpu_filter_hot.yona"),
    ("pipeline", "gpu_columnar_pipeline.yona"),
    ("materialize", "gpu_materialize_sum.yona"),
    ("map_reduce_10k", "gpu_map_reduce_10k.yona"),
    ("filter_10k", "gpu_filter_10k.yona"),
    ("pipeline_5k", "gpu_columnar_pipeline_5k.yona"),
    ("materialize_5k", "gpu_materialize_sum_5k.yona"),
    # FloatArray scale via gpu_stub.f64 fence path (.expected 0 = success; fails on hosts
    # without Vulkan + shaderFloat64 — same deterministic output across CPU/Vulkan env cols).
    ("float_scale", "gpu_float_scale_hot.yona"),
)

BENCH_ONLY_CHOICES = [b[0] for b in BENCHES] + ["all"]


def compile_yona(yonac: Path, sysroot: Path, lib: Path, opt: int, src: Path, exe: Path) -> bool:
    cmd = [
        str(yonac),
        f"-O{opt}",
        "--sysroot",
        str(sysroot),
        "-I",
        str(lib),
        "-o",
        str(exe),
        str(src),
    ]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=120, env=_tool_env())
    return r.returncode == 0


def run_once(exe: Path, env: dict, timeout: float = 120.0) -> tuple[str | None, float]:
    t0 = time.perf_counter_ns()
    r = subprocess.run(
        [str(exe)],
        capture_output=True,
        text=True,
        timeout=timeout,
        env=env,
    )
    ms = (time.perf_counter_ns() - t0) / 1_000_000
    if r.returncode != 0:
        return None, ms
    return r.stdout.strip(), ms


def _cell(s: str, w: int) -> str:
    """Pad or truncate to width w (visible length; no ANSI)."""
    if len(s) > w:
        return s[: w - 2] + ".."
    return s + " " * (w - len(s))


def _hline(inner: int, ch: str = "=") -> str:
    return "+" + ch * inner + "+"


def print_results_table(
    rows: list[dict],
    yonac: Path,
    iterations: int,
    opt: int,
) -> None:
    """rows: each dict has benchmark, cpu_avg, cpu_min, vk_avg, vk_min, status, error?"""
    # Inner width between '+' borders (= data row width minus 2)
    inner = 115
    print()
    print(_hline(inner))
    print("| " + _cell("Std\\GPU — CPU-only vs Vulkan opt-in (same .expected output)", inner) + " |")
    print("| " + _cell(f"yonac: {yonac}", inner) + " |")
    print(
        "| "
        + _cell(
            f"runs/variant={iterations} each  |  -O{opt}  |  "
            "CPU-only: YONA_GPU_DISABLE_VULKAN=1  |  "
            "GPU path: YONA_GPU_VULKAN_COMPUTE=1, YONA_GPU_VULKAN_MIN_LEN=1 (Vulkan off in env)",
            inner,
        )
        + " |"
    )
    print(_hline(inner))

    c0, c1, c2, c3, c4, c5 = 22, 13, 13, 12, 10, 28
    head = (
        "| "
        + _cell("Benchmark", c0)
        + " | "
        + _cell("CPU avg (ms)", c1)
        + " | "
        + _cell("GPU avg (ms)", c2)
        + " | "
        + _cell("min CPU|GPU", c3)
        + " | "
        + _cell("Delta %", c4)
        + " | "
        + _cell("Verdict", c5)
        + " |"
    )
    print(head)
    print(_hline(inner, "-"))

    for r in rows:
        if r.get("status") != "ok":
            err = (r.get("error") or r.get("status", "?"))[:40]
            print(
                "| "
                + _cell(r["benchmark"], c0)
                + " | "
                + _cell("--", c1)
                + " | "
                + _cell("--", c2)
                + " | "
                + _cell("--", c3)
                + " | "
                + _cell("--", c4)
                + " | "
                + _cell(err, c5)
                + " |"
            )
            continue

        ca, va = r["cpu_avg"], r["vk_avg"]
        cmin, vmin = r["cpu_min"], r["vk_min"]
        pct = ((va - ca) / ca * 100.0) if ca > 0 else 0.0
        pct_s = f"{pct:+.1f}%"

        if va < ca - 1e-6:
            mult = ca / va if va > 0 else 0.0
            verdict = f"GPU faster ({mult:.2f}x)"
        elif va > ca + 1e-6:
            mult = va / ca if ca > 0 else 0.0
            verdict = f"CPU faster ({mult:.2f}x)"
        else:
            verdict = "tie (same path?)"

        mins = f"{cmin:.0f}|{vmin:.0f}"
        print(
            "| "
            + _cell(r["benchmark"], c0)
            + " | "
            + _cell(f"{ca:.2f}", c1)
            + " | "
            + _cell(f"{va:.2f}", c2)
            + " | "
            + _cell(mins, c3)
            + " | "
            + _cell(pct_s, c4)
            + " | "
            + _cell(verdict, c5)
            + " |"
        )

    print(_hline(inner, "-"))
    foot = (
        "Delta% = (GPU avg - CPU avg) / CPU avg; negative means GPU column faster. "
        "No Vulkan: both columns similar."
    )
    print("| " + _cell(foot, inner) + " |")
    print(_hline(inner))
    print()


def main() -> int:
    ap = argparse.ArgumentParser(description="Compare GPU bench CPU vs Vulkan env")
    ap.add_argument("-n", type=int, default=3, help="iterations per configuration")
    ap.add_argument("-O", dest="opt", type=int, default=2, help="yonac optimization level")
    ap.add_argument("--yonac", type=Path, default=None, help="path to yonac")
    ap.add_argument(
        "--only",
        choices=BENCH_ONLY_CHOICES,
        default="all",
        metavar="NAME",
        help="benchmark key or all (includes crossover variants *_10k / *_5k and float_scale)",
    )
    ap.add_argument(
        "--json",
        action="store_true",
        help="print machine-readable summary after table (array of bench result dicts)",
    )
    ap.add_argument(
        "--json-report",
        action="store_true",
        help="like --json but wraps results in meta (host, yonac, iterations, -O) for crossover logs",
    )
    args = ap.parse_args()

    yonac = args.yonac or _default_yonac()
    if not yonac.exists():
        print(f"yonac not found: {yonac}", file=sys.stderr)
        return 1

    sysroot = _default_sysroot()
    lib = ROOT / "lib"
    build = Path(os.environ.get("TEMP", "/tmp")) / "yona_gpu_compare_build"
    build.mkdir(parents=True, exist_ok=True)

    benches = BENCHES if args.only == "all" else [b for b in BENCHES if b[0] == args.only]

    rows_out: list[dict] = []

    for key, yname in benches:
        src = BENCH / yname
        exp = src.with_suffix(".expected")
        row: dict = {"benchmark": key, "source": str(src)}
        if not src.exists():
            row["status"] = "missing"
            row["error"] = "source not found"
            rows_out.append(row)
            continue
        expected = exp.read_text().strip() if exp.exists() else None

        exe = _exe_suffix(build / f"gpu_cmp_{key}_O{args.opt}")
        if not compile_yona(yonac, sysroot, lib, args.opt, src, exe):
            row["status"] = "compile_error"
            rows_out.append(row)
            continue

        env_cpu = _tool_env()
        env_cpu["YONA_GPU_DISABLE_VULKAN"] = "1"
        env_cpu.pop("YONA_GPU_VULKAN_COMPUTE", None)
        env_cpu.pop("YONA_GPU_VULKAN_MAPADD", None)
        env_cpu.pop("YONA_GPU_VULKAN_MAPMUL", None)
        env_cpu.pop("YONA_GPU_VULKAN_REDUCE", None)
        env_cpu.pop("YONA_GPU_VULKAN_FILTER", None)

        env_vk = _tool_env()
        env_vk.pop("YONA_GPU_DISABLE_VULKAN", None)
        env_vk["YONA_GPU_VULKAN_COMPUTE"] = "1"
        env_vk["YONA_GPU_VULKAN_MIN_LEN"] = "1"

        def bench_env(e: dict) -> list[float] | None:
            times: list[float] = []
            for _ in range(args.n):
                out, ms = run_once(exe, e)
                if out is None:
                    return None
                if expected is not None and out != expected:
                    row["status"] = "wrong_output"
                    row["error"] = f"expected {expected}, got {out}"
                    return None
                times.append(ms)
            return times

        t_cpu = bench_env(env_cpu)
        if t_cpu is None:
            if row.get("status") != "wrong_output":
                row["status"] = "run_failed"
                row["error"] = "CPU path failed or timeout"
            rows_out.append(row)
            continue

        t_vk = bench_env(env_vk)
        if t_vk is None:
            if row.get("status") != "wrong_output":
                row["status"] = "run_failed"
                row["error"] = "Vulkan path failed or timeout"
            rows_out.append(row)
            continue

        avg_cpu = sum(t_cpu) / len(t_cpu)
        avg_vk = sum(t_vk) / len(t_vk)
        row["status"] = "ok"
        row["cpu_avg"] = avg_cpu
        row["cpu_min"] = min(t_cpu)
        row["vk_avg"] = avg_vk
        row["vk_min"] = min(t_vk)
        row["ratio_vk_over_cpu"] = avg_vk / avg_cpu if avg_cpu > 0 else 0.0
        row["speedup_cpu_over_vk"] = avg_cpu / avg_vk if avg_vk > 0 else 0.0
        rows_out.append(row)

    print_results_table(rows_out, yonac, args.n, args.opt)

    if args.json_report or args.json:
        if args.json_report:
            report = {
                "schema": "yona.gpu_compare.v1",
                "meta": {
                    "generated_at": datetime.now(timezone.utc).isoformat(),
                    "platform": platform.platform(),
                    "python": sys.version.split()[0],
                    "yonac": str(yonac.resolve()),
                    "iterations_per_variant": args.n,
                    "opt_level": args.opt,
                    "only": args.only,
                },
                "benches": rows_out,
            }
            print(json.dumps(report, indent=2))
        else:
            print(json.dumps(rows_out, indent=2))

    failed = sum(1 for r in rows_out if r.get("status") != "ok")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
