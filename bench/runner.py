#!/usr/bin/env python3
"""
Yona Benchmark Runner

Discovers .yona benchmarks, compiles with yonac, runs with timing + memory,
optionally compares against C references and across optimization levels.

Usage:
    python3 bench/runner.py                      # run all, -O2
    python3 bench/runner.py core/fibonacci        # specific benchmark
    python3 bench/runner.py --compare-c           # compare vs C
    python3 bench/runner.py --verify-reference-outputs   # C refs vs .expected only, exit 1 on mismatch
    python3 bench/runner.py --verify-reference-outputs --reference-verify-langs all
    python3 bench/runner.py --skip-erl   # omit Erlang when Windows OTP crashes
    python3 bench/runner.py --all-opt-levels      # compare O0/O1/O2/O3
    python3 bench/runner.py --json                # machine-readable output
"""

import argparse
import json
import os
import subprocess
import sys
import time
import tempfile
import shutil
import ctypes
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BENCH_DIR = ROOT / "bench"
def _default_yonac():
    candidates = [
        ROOT / "out" / "build" / "x64-debug-macos" / "yonac",
        ROOT / "out" / "build" / "x64-release-macos" / "yonac",
        ROOT / "out" / "build" / "x64-debug-linux" / "yonac",
        ROOT / "out" / "build" / "x64-release-linux" / "yonac",
        ROOT / "out" / "build" / "x64-debug" / "yonac.exe",
        ROOT / "out" / "build" / "x64-release" / "yonac.exe",
    ]
    for c in candidates:
        if c.exists():
            return c
    return candidates[0]


YONAC = _default_yonac()
BUILD_DIR = Path(tempfile.gettempdir()) / "yona_bench"
STARTUP_CACHE_VERSION = "v2"

# Per-benchmark timeout (seconds) — prevents runaway benchmarks while keeping
# slower VM startup / process-heavy rows reliable on Windows.
BENCH_TIMEOUT = 30

# Disable Erlang refs via --skip-erl or env YONA_BENCH_SKIP_ERLANG=1 when OTP/BEAM
# exits 0xC0000005 on Windows (broken install / AV interaction); see bench/README.md.
_ERLANG_REFERENCE_ENABLED = True


def erlang_reference_enabled():
    if not _ERLANG_REFERENCE_ENABLED:
        return False
    v = os.environ.get("YONA_BENCH_SKIP_ERLANG", "").strip().lower()
    return v not in ("1", "true", "yes")


def _default_sysroot():
    build_root = YONAC.parent
    if (build_root / "runtime").exists():
        return build_root
    return ROOT


def _tool_env():
    env = os.environ.copy()
    extra_paths = []
    if os.name == "nt":
        llvm_bin = Path("C:/local/LLVM/bin")
        if llvm_bin.exists():
            extra_paths.append(str(llvm_bin))
    if extra_paths:
        env["PATH"] = os.pathsep.join(extra_paths + [env.get("PATH", "")])
    return env


def with_exe_suffix(path):
    if os.name == "nt" and path.suffix.lower() != ".exe":
        return Path(str(path) + ".exe")
    return path


def _ref_source(stem: str, ext: str, platform_specific: bool = False):
    """Pick platform-specific ref first when available.

    On Windows we prefer `name.win.ext` and fall back to `name.ext`.
    Other platforms keep using `name.ext` unchanged.
    """
    if platform_specific and os.name == "nt":
        win_src = BENCH_DIR / "reference" / f"{stem}.win.{ext}"
        if win_src.exists():
            return win_src
    src = BENCH_DIR / "reference" / f"{stem}.{ext}"
    return src if src.exists() else None


def _write_bytes(path, chunk: bytes, total: int):
    with open(path, "wb") as f:
        left = total
        while left > 0:
            n = min(left, len(chunk))
            f.write(chunk[:n])
            left -= n


def _ensure_text_fixture(path: Path, total_bytes: int, newline_count: int):
    """Create deterministic LF-only text fixture with exact byte size."""
    if path.exists() and path.stat().st_size == total_bytes:
        raw = path.read_bytes()
        if raw.count(b"\n") == newline_count:
            max_line = 0
            cur = 0
            for b in raw:
                if b == 0x0A:
                    if cur > max_line:
                        max_line = cur
                    cur = 0
                else:
                    cur += 1
            if max_line <= 1023:
                return
    path.parent.mkdir(parents=True, exist_ok=True)
    if newline_count <= 0 or total_bytes <= newline_count:
        raise ValueError(f"invalid fixture shape for {path}")
    text_bytes = total_bytes - newline_count
    base_len = text_bytes // newline_count
    extra = text_bytes % newline_count
    if base_len <= 0:
        raise ValueError(f"invalid fixture line size for {path}")
    with open(path, "wb") as f:
        for i in range(newline_count):
            line_len = base_len + (1 if i < extra else 0)
            f.write(b"a" * line_len)
            f.write(b"\n")


def _normalize_lf(path: Path):
    if not path.exists():
        return
    raw = path.read_bytes()
    if b"\r\n" not in raw:
        return
    path.write_bytes(raw.replace(b"\r\n", b"\n"))


def ensure_benchmark_data():
    data_dir = BENCH_DIR / "data"
    data_dir.mkdir(parents=True, exist_ok=True)
    # Keep canonical LF text sizes so benchmark expected outputs are cross-platform stable.
    _ensure_text_fixture(data_dir / "bench_text.txt", total_bytes=1206681, newline_count=20000)
    _ensure_text_fixture(data_dir / "large_text.txt", total_bytes=55022092, newline_count=500000)
    chunk_sizes = [9101111, 9101111, 9101111, 9101109]  # sum = 36404442
    for i, size in enumerate(chunk_sizes, start=1):
        p = data_dir / f"chunk_{i}.txt"
        if not p.exists() or p.stat().st_size != size:
            _write_bytes(p, b"c", size)
    # Binary fixture for binary I/O benches (5 MiB).
    bin_path = data_dir / "bench_binary.bin"
    if not bin_path.exists() or bin_path.stat().st_size != 5 * 1024 * 1024:
        pat = bytes(range(256))
        _write_bytes(bin_path, pat, 5 * 1024 * 1024)
    # If git checkout converted line endings, normalize back to LF.
    _normalize_lf(data_dir / "bench_text.txt")


def find_benchmarks(filter_name=None):
    benchmarks = []
    for category in sorted(BENCH_DIR.iterdir()):
        if not category.is_dir() or category.name in ("reference", "__pycache__"):
            continue
        for yona_file in sorted(category.glob("*.yona")):
            name = f"{category.name}/{yona_file.stem}"
            if filter_name and filter_name not in name:
                continue
            expected_file = yona_file.with_suffix(".expected")
            benchmarks.append({
                "name": name,
                "source": yona_file,
                "expected": expected_file if expected_file.exists() else None,
                "category": category.name,
            })
    return benchmarks


def compile_yona(source, opt_level, exe_path):
    lib_path = ROOT / "lib"
    cmd = [str(YONAC), f"-O{opt_level}", "--sysroot", str(_default_sysroot()), "-I", str(lib_path),
           "-o", str(exe_path), str(source)]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=120, env=_tool_env())
        if result.returncode != 0:
            err = (result.stderr or result.stdout or "").strip()
            if err:
                print(f"  compile failed: {err[:400]}", file=sys.stderr)
            return False
        return True
    except subprocess.TimeoutExpired:
        return False


def compile_c(c_file, exe_path):
    cc = os.environ.get("YONAC_CC")
    if not cc:
        cc = (shutil.which("clang")
              or shutil.which("cc")
              or shutil.which("gcc")
              or "clang")
    cmd = [cc, "-O2", "-o", str(exe_path), str(c_file)]
    if os.name != "nt":
        cmd.insert(2, "-lm")
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=10, env=_tool_env())
        return result.returncode == 0
    except subprocess.TimeoutExpired:
        return False


# Reference-language registry. Each entry returns a runnable command list for
# /usr/bin/time or None if prerequisites are missing.
def _prep_c(stem, build_dir):
    src = _ref_source(stem, "c", platform_specific=True)
    if src is None:
        return None
    exe = with_exe_suffix(build_dir / f"ref_c_{stem}")
    if not compile_c(src, exe):
        return None
    return [str(exe)]


def _prep_erl(stem, build_dir):
    """Prepare an Erlang reference: compile the escript to a .beam so the
    benchmark run measures compiled Erlang, not the escript interpreter.
    The .erl file is expected to define `main/1`; we convert it to a
    regular module, compile with erlc, and invoke via erl -noshell."""
    if not erlang_reference_enabled():
        return None
    src = _ref_source(stem, "erl")
    if src is None:
        return None
    from shutil import which
    if not which("erl") or not which("erlc"):
        return None
    # Rewrite the escript header to a module declaration so erlc can
    # compile it. Cache the generated source + beam in BUILD_DIR.
    mod_name = f"yonabench_{stem}"
    erl_src = build_dir / f"{mod_name}.erl"
    beam = build_dir / f"{mod_name}.beam"
    if not beam.exists() or src.stat().st_mtime > beam.stat().st_mtime:
        raw = src.read_text().splitlines()
        # Drop the escript shebang + any leading `%%` comment lines that
        # sit above the first `main/1` clause; replace with a module hdr.
        body = []
        for line in raw:
            if line.startswith("#!"):
                continue
            body.append(line)
        erl_src.write_text(
            f"-module({mod_name}).\n"
            f"-export([main/1]).\n"
            + "\n".join(body) + "\n"
        )
        result = subprocess.run(
            ["erlc", "-o", str(build_dir), str(erl_src)],
            capture_output=True, text=True, timeout=30,
        )
        if result.returncode != 0 or not beam.exists():
            # On some Windows hosts `erlc`/`erl` exit with 0xC0000005 (access
            # violation) with no stderr — see docs/todo-list.md (Erlang lane).
            detail = (result.stderr or result.stdout or "").strip()
            if detail:
                print(f"  erlc {mod_name}: {detail}", flush=True)
            else:
                print(
                    f"  erlc {mod_name}: failed (exit {result.returncode}); "
                    f"no .beam — if exit is -1073741819 / 3221225477, OTP/BEAM "
                    f"may be crashing on this host",
                    flush=True,
                )
            return None
    # Note: Erlang VM startup is ~1s on this box, so these numbers reflect
    # VM boot + computation and will look lopsided on sub-millisecond
    # benchmarks. -eval invokes main/1 with [] (escript convention).
    return ["erl", "-noshell", "-pa", str(build_dir),
            "-eval", f"{mod_name}:main([]), init:stop()."]


def _prep_java(stem, build_dir):
    """Compile a Java reference with javac; invoke via `java ClassName`.
    The .java file is expected to define a top-level class matching `stem`
    (or one with a `main(String[])` method)."""
    src = _ref_source(stem, "java")
    if src is None:
        return None
    from shutil import which
    if not which("java") or not which("javac"):
        return None
    # javac's class output location is determined by the source's package
    # (default package → the current class dir). We copy the source into
    # build_dir/<stem>/ so the .class files don't collide across benches.
    jdir = build_dir / f"java_{stem}"
    jdir.mkdir(parents=True, exist_ok=True)
    # Find the public/main class name from the source.
    text = src.read_text()
    # Look for `class NAME` — fall back to `stem`.
    import re
    m = re.search(r"(?:public\s+)?class\s+(\w+)", text)
    cls = m.group(1) if m else stem
    class_file = jdir / f"{cls}.class"
    if not class_file.exists() or src.stat().st_mtime > class_file.stat().st_mtime:
        # Copy source next to where .class will land (javac default).
        staged = jdir / f"{cls}.java"
        staged.write_text(text)
        result = subprocess.run(
            ["javac", "-d", str(jdir), str(staged)],
            capture_output=True, text=True, timeout=30,
        )
        if result.returncode != 0 or not class_file.exists():
            return None
    return ["java", "-cp", str(jdir), cls]


def _prep_hs(stem, build_dir):
    """Compile a Haskell reference with ghc -O2 (native binary)."""
    src = _ref_source(stem, "hs")
    if src is None:
        return None
    from shutil import which
    ghc_cmd = None
    if which("ghc"):
        ghc_cmd = ["ghc"]
    elif which("stack"):
        # Fallback when users have Stack installed but ghc isn't on PATH.
        ghc_cmd = ["stack", "ghc", "--"]
    if not ghc_cmd:
        return None
    exe = with_exe_suffix(build_dir / f"ref_hs_{stem}")
    if not exe.exists() or src.stat().st_mtime > exe.stat().st_mtime:
        # -outputdir keeps .hi/.o clutter inside build_dir rather than
        # next to the source.
        hdir = build_dir / f"hs_{stem}"
        hdir.mkdir(parents=True, exist_ok=True)
        result = subprocess.run(
            ghc_cmd + ["-O2", "-outputdir", str(hdir), "-o", str(exe), str(src)],
            capture_output=True, text=True, timeout=60,
        )
        if result.returncode != 0 or not exe.exists():
            return None
    return [str(exe)]


def _prep_js(stem, build_dir):
    """Node.js reference — no compile step, invoke `node file.js` directly."""
    src = _ref_source(stem, "js")
    if src is None:
        return None
    from shutil import which
    if not which("node"):
        return None
    return ["node", str(src)]


def _prep_py(stem, build_dir):
    """Python reference — no compile step, invoke `python3 file.py` directly."""
    src = _ref_source(stem, "py")
    if src is None:
        return None
    from shutil import which
    py = which("python") or which("python3") or sys.executable
    if not py:
        return None
    return [py, str(src)]


def _prep_go(stem, build_dir):
    """Compile a Go reference with `go build`."""
    src = _ref_source(stem, "go")
    if src is None:
        return None
    from shutil import which
    if not which("go"):
        return None
    exe = with_exe_suffix(build_dir / f"ref_go_{stem}")
    if not exe.exists() or src.stat().st_mtime > exe.stat().st_mtime:
        result = subprocess.run(
            ["go", "build", "-o", str(exe), str(src)],
            capture_output=True, text=True, timeout=60,
        )
        if result.returncode != 0 or not exe.exists():
            return None
    return [str(exe)]


def measure_startup(lang, build_dir, iterations=3):
    """Return average startup time in ms for `lang` by running a trivial
    no-op program. Cached in build_dir so we only measure once per run.

    The probe for each language is a minimal "do nothing" program built
    and run via the same toolchain the reference impls use, so the
    measured number includes compiler-output/VM-boot overhead exactly
    the way benchmark runs do."""
    cache = build_dir / f".startup_{lang}.ms"
    if cache.exists():
        try:
            parts = cache.read_text().strip().split()
            # v2 format: "<version> <avg_ms> <peak_rss_kb>"
            if len(parts) >= 3 and parts[0] == STARTUP_CACHE_VERSION:
                avg_ms = float(parts[1])
                peak_rss = int(parts[2])
                # On Windows we now expect startup RSS to be measurable.
                # If legacy zero-RSS slipped in, force recompute.
                if (os.name == "nt" or sys.platform == "darwin") and peak_rss <= 0:
                    raise ValueError("stale startup RSS cache")
                return avg_ms, peak_rss
            # Legacy cache support (v1): "<avg_ms> [peak_rss_kb]"
            if len(parts) >= 2:
                avg_ms = float(parts[0])
                peak_rss = int(parts[1])
                if (os.name == "nt" or sys.platform == "darwin") and peak_rss <= 0:
                    raise ValueError("stale legacy startup RSS cache")
                return avg_ms, peak_rss
        except (ValueError, IndexError):
            pass
    probe_dir = build_dir / "_startup"
    probe_dir.mkdir(parents=True, exist_ok=True)

    from shutil import which
    cmd = None
    if lang == "c":
        cc = which(os.environ.get("YONAC_CC", "")) if os.environ.get("YONAC_CC") else None
        if not cc:
            cc = which("clang") or which("cc")
        if not cc:
            return 0.0, 0
        src = probe_dir / "startup.c"
        src.write_text("int main(void){return 0;}\n")
        exe = with_exe_suffix(probe_dir / "startup_c")
        if subprocess.run([cc, "-O2", "-o", str(exe), str(src)],
                          capture_output=True, timeout=30).returncode != 0:
            return 0.0, 0
        cmd = [str(exe)]
    elif lang == "erl":
        if not erlang_reference_enabled():
            return 0.0, 0
        if not (which("erl") and which("erlc")):
            return 0.0, 0
        src = probe_dir / "startup_erl.erl"
        src.write_text("-module(startup_erl).\n-export([main/1]).\nmain(_) -> ok.\n")
        if subprocess.run(["erlc", "-o", str(probe_dir), str(src)],
                          capture_output=True, timeout=30).returncode != 0:
            return 0.0, 0
        cmd = ["erl", "-noshell", "-pa", str(probe_dir),
               "-eval", "startup_erl:main([]), init:stop()."]
    elif lang == "java":
        if not (which("java") and which("javac")):
            return 0.0, 0
        src = probe_dir / "startup_java.java"
        src.write_text("public class startup_java { public static void main(String[] a) {} }\n")
        if subprocess.run(["javac", "-d", str(probe_dir), str(src)],
                          capture_output=True, timeout=30).returncode != 0:
            return 0.0, 0
        cmd = ["java", "-cp", str(probe_dir), "startup_java"]
    elif lang == "hs":
        ghc_cmd = None
        if which("ghc"):
            ghc_cmd = ["ghc"]
        elif which("stack"):
            ghc_cmd = ["stack", "ghc", "--"]
        if not ghc_cmd:
            return 0.0, 0
        src = probe_dir / "startup_hs.hs"
        src.write_text("main = return ()\n")
        exe = with_exe_suffix(probe_dir / "startup_hs")
        if subprocess.run(
            ghc_cmd + ["-O2", "-outputdir", str(probe_dir), "-o", str(exe), str(src)],
            capture_output=True, timeout=60,
        ).returncode != 0:
            return 0.0, 0
        cmd = [str(exe)]
    elif lang == "js":
        if not which("node"):
            return 0.0, 0
        src = probe_dir / "startup.js"
        src.write_text("")
        cmd = ["node", str(src)]
    elif lang == "py":
        py = which("python") or which("python3") or sys.executable
        if not py:
            return 0.0, 0
        src = probe_dir / "startup.py"
        src.write_text("")
        cmd = [py, str(src)]
    elif lang == "go":
        if not which("go"):
            return 0.0, 0
        src = probe_dir / "startup.go"
        src.write_text("package main\nfunc main(){}\n")
        exe = with_exe_suffix(probe_dir / "startup_go")
        if subprocess.run(["go", "build", "-o", str(exe), str(src)],
                          capture_output=True, timeout=60).returncode != 0:
            return 0.0, 0
        cmd = [str(exe)]
    elif lang == "yona":
        src = probe_dir / "startup.yona"
        src.write_text("0\n")
        exe = with_exe_suffix(probe_dir / "startup_yona")
        if not compile_yona(src, 2, exe):
            return 0.0, 0
        cmd = [str(exe)]

    if cmd is None:
        return 0.0, 0
    # Warmup once (file system caches, linker cache etc.) then measure.
    run_once(cmd)
    times, rss_values = [], []
    for _ in range(iterations):
        r = run_once(cmd)
        if r is None:
            continue
        times.append(r[1])
        rss_values.append(r[2])
    if not times:
        return 0.0, 0
    avg_ms = sum(times) / len(times)
    peak_rss = max(rss_values) if rss_values else 0
    cache.write_text(f"{STARTUP_CACHE_VERSION} {avg_ms} {peak_rss}\n")
    return avg_ms, peak_rss


REF_LANGS = {
    "c":    {"label": "C",    "prep": _prep_c},
    "erl":  {"label": "Erl",  "prep": _prep_erl},
    "java": {"label": "Java", "prep": _prep_java},
    "hs":   {"label": "Hs",   "prep": _prep_hs},
    "js":   {"label": "Node", "prep": _prep_js},
    "py":   {"label": "Py",   "prep": _prep_py},
    "go":   {"label": "Go",   "prep": _prep_go},
}


def _windows_peak_working_set_kb(proc: subprocess.Popen) -> int:
    """Return peak working set (KB) for a finished Windows process."""
    if os.name != "nt":
        return 0
    try:
        class PROCESS_MEMORY_COUNTERS(ctypes.Structure):
            _fields_ = [
                ("cb", ctypes.c_uint32),
                ("PageFaultCount", ctypes.c_uint32),
                ("PeakWorkingSetSize", ctypes.c_size_t),
                ("WorkingSetSize", ctypes.c_size_t),
                ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                ("PagefileUsage", ctypes.c_size_t),
                ("PeakPagefileUsage", ctypes.c_size_t),
            ]

        counters = PROCESS_MEMORY_COUNTERS()
        counters.cb = ctypes.sizeof(PROCESS_MEMORY_COUNTERS)
        ok = ctypes.windll.psapi.GetProcessMemoryInfo(
            ctypes.c_void_p(proc._handle),
            ctypes.byref(counters),
            counters.cb,
        )
        if not ok:
            return 0
        return int(counters.PeakWorkingSetSize // 1024)
    except Exception:
        return 0


def _posix_rss_kb_from_rusage(ru) -> int:
    """Convert wait4 ru_maxrss to kilobytes (Darwin: bytes; Linux: KB)."""
    rss = int(getattr(ru, "ru_maxrss", 0) or 0)
    if rss <= 0:
        return 0
    if sys.platform == "darwin":
        return rss // 1024
    return rss


def _posix_communicate_rusage(proc, timeout):
    """Read stdout/stderr then wait4 so the child's ru_maxrss is available."""
    import select

    chunks = {proc.stdout.fileno(): [], proc.stderr.fileno(): []}
    kind = {proc.stdout.fileno(): "out", proc.stderr.fileno(): "err"}
    pending = set(chunks)
    deadline = time.monotonic() + timeout
    while pending:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise subprocess.TimeoutExpired(proc.args, timeout)
        readable, _, _ = select.select(list(pending), [], [], remaining)
        if not readable:
            raise subprocess.TimeoutExpired(proc.args, timeout)
        for fd in readable:
            data = os.read(fd, 65536)
            if not data:
                pending.discard(fd)
                continue
            chunks[fd].append(data)
    _, status, ru = os.wait4(proc.pid, 0)
    proc.returncode = os.waitstatus_to_exitcode(status)
    stdout = b"".join(chunks[proc.stdout.fileno()]).decode()
    stderr = b"".join(chunks[proc.stderr.fileno()]).decode()
    return stdout, stderr, ru


def run_once(cmd_or_exe):
    """Run a command once. `cmd_or_exe` is a str (path) or a list of args.
    Returns (output, time_ms, peak_rss_kb) or None on failure."""
    proc = None
    try:
        cmd = cmd_or_exe if isinstance(cmd_or_exe, list) else [str(cmd_or_exe)]
        start = time.perf_counter_ns()
        if os.name == "nt":
            proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            stdout, stderr = proc.communicate(timeout=BENCH_TIMEOUT)
            elapsed_ms = (time.perf_counter_ns() - start) / 1_000_000
            if proc.returncode != 0:
                return None
            peak_rss_kb = _windows_peak_working_set_kb(proc)
            return stdout.strip(), elapsed_ms, peak_rss_kb
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
        stdout, stderr, ru = _posix_communicate_rusage(proc, BENCH_TIMEOUT)
        elapsed_ms = (time.perf_counter_ns() - start) / 1_000_000
        if proc.returncode != 0:
            return None
        return stdout.strip(), elapsed_ms, _posix_rss_kb_from_rusage(ru)
    except subprocess.TimeoutExpired:
        if proc is not None:
            try:
                proc.kill()
            except Exception:
                pass
            if os.name != "nt":
                try:
                    os.wait4(proc.pid, 0)
                except Exception:
                    pass
        return None
    except Exception:
        return None


def run_benchmark(exe, iterations):
    """Run N times, return stats dict or None."""
    times = []
    rss_values = []
    output = ""
    for i in range(iterations):
        result = run_once(exe)
        if result is None:
            return None
        out, ms, rss = result
        if i == 0:
            output = out
        times.append(ms)
        rss_values.append(rss)
    return {
        "output": output,
        "times_ms": times,
        "min_ms": min(times),
        "avg_ms": sum(times) / len(times),
        "max_ms": max(times),
        "peak_rss_kb": max(rss_values) if rss_values else 0,
    }


def check_correctness(bench, output):
    if bench["expected"] is None:
        return True
    expected = bench["expected"].read_text().strip()
    return output == expected


def verify_reference_outputs(benchmarks, langs: tuple, build_dir: Path) -> int:
    """Run each reference toolchain once per benchmark that has a `.expected`
    file; compare stdout to the golden string. Missing toolchain (prep None)
    is skipped, not a failure. Returns the number of failing lanes."""
    build_dir.mkdir(parents=True, exist_ok=True)
    failures = 0
    skipped_no_golden = 0
    skipped_no_tool = 0
    ok_count = 0

    for bench in benchmarks:
        if bench["expected"] is None:
            skipped_no_golden += 1
            continue
        expected = bench["expected"].read_text().strip()
        stem = bench["source"].stem
        for lang in langs:
            spec = REF_LANGS.get(lang)
            if not spec:
                print(f"  skip unknown language key: {lang}", file=sys.stderr)
                continue
            cmd = spec["prep"](stem, build_dir)
            if cmd is None:
                skipped_no_tool += 1
                if lang == "erl" and _ref_source(stem, "erl") is not None:
                    print(
                        f"  skip {bench['name']:.<40} {spec['label']:<5} "
                        f"(Erlang prep failed; erlc/erl may crash on this host)",
                        flush=True,
                    )
                continue
            result = run_once(cmd)
            if result is None:
                print(f"  FAIL {bench['name']:.<40} {spec['label']:<5} (run error or timeout)")
                failures += 1
                continue
            out, _, _ = result
            if out != expected:
                print(
                    f"  FAIL {bench['name']:.<40} {spec['label']:<5} "
                    f"expected {expected!r} got {out!r}"
                )
                failures += 1
            else:
                ok_count += 1
                print(f"  ok   {bench['name']:.<40} {spec['label']:<5}")

    print(
        f"\n  Reference conformance: {ok_count} ok, {failures} failed, "
        f"{skipped_no_tool} skipped (no toolchain), "
        f"{skipped_no_golden} benchmarks without .expected",
        flush=True,
    )
    return failures


def fmt_time(ms):
    if ms < 1:
        return f"{ms:.3f}ms"
    if ms < 100:
        return f"{ms:.1f}ms"
    return f"{ms:.0f}ms"


def fmt_rss(kb):
    if kb == 0:
        return "?"
    if kb < 1024:
        return f"{kb}KB"
    return f"{kb / 1024:.1f}MB"


def run_suite(benchmarks, opt_level, iterations, compare_langs=()):
    """Run all benchmarks at one opt level. `compare_langs` is a tuple of
    language keys (from REF_LANGS) to compare against.
    Returns list of result dicts."""
    BUILD_DIR.mkdir(parents=True, exist_ok=True)

    # Probe VM/runtime startup for each comparison language once, so we
    # can report both raw and startup-adjusted times. For compiled
    # natives (C, Haskell, Go) startup is negligible; Erlang/Java/Node/
    # Python have significant cold-start costs that dominate short
    # benchmarks. Yona is always probed so its own numbers are adjusted
    # on the same basis as the references.
    startup_ms = {}
    startup_rss = {}
    y_ms, y_rss = measure_startup("yona", BUILD_DIR, iterations)
    if y_ms > 0:
        startup_ms["yona"] = y_ms
        startup_rss["yona"] = y_rss
    if compare_langs:
        for lang in compare_langs:
            ms, rss = measure_startup(lang, BUILD_DIR, iterations)
            if ms > 0:
                startup_ms[lang] = ms
                startup_rss[lang] = rss
    if startup_ms:
        parts = []
        if "yona" in startup_ms:
            parts.append(f"Yona={fmt_time(startup_ms['yona'])}/{fmt_rss(startup_rss['yona'])}")
        for l in startup_ms:
            if l == "yona":
                continue
            parts.append(f"{REF_LANGS[l]['label']}={fmt_time(startup_ms[l])}/{fmt_rss(startup_rss[l])}")
        print("  Startup (cold-start no-op, time/RSS):", ", ".join(parts))

    results = []
    current_category = None

    for bench in benchmarks:
        if bench["category"] != current_category:
            current_category = bench["category"]
            print(f"\n  [{current_category}]")

        stem = bench["source"].stem
        exe = with_exe_suffix(BUILD_DIR / f"{stem}_O{opt_level}")

        if not compile_yona(bench["source"], opt_level, exe):
            print(f"    {stem:.<28} COMPILE FAILED")
            results.append({"name": bench["name"], "status": "compile_error"})
            continue

        stats = run_benchmark(exe, iterations)
        if stats is None:
            print(f"    {stem:.<28} TIMEOUT")
            results.append({"name": bench["name"], "status": "timeout"})
            continue

        if not check_correctness(bench, stats["output"]):
            expected = bench["expected"].read_text().strip() if bench["expected"] else "?"
            print(f"    {stem:.<28} WRONG (expected {expected}, got {stats['output'][:40]})")
            results.append({"name": bench["name"], "status": "wrong_output"})
            continue

        r = {
            "name": bench["name"],
            "status": "ok",
            "opt_level": opt_level,
            **stats,
        }
        # Yona startup adjustment (time + memory) — same treatment as references.
        ysu = startup_ms.get("yona", 0.0)
        yona_adj = max(stats["avg_ms"] - ysu, 0.01)
        ysu_rss = startup_rss.get("yona", 0)
        yona_rss_adj = max(stats["peak_rss_kb"] - ysu_rss, 0)
        r["avg_ms_adj"] = yona_adj
        r["startup_ms"] = ysu
        r["peak_rss_kb_adj"] = yona_rss_adj
        r["startup_rss_kb"] = ysu_rss

        y_time_disp = (f"{fmt_time(stats['avg_ms'])}→{fmt_time(yona_adj)}"
                       if ysu > stats["avg_ms"] * 0.05
                       else fmt_time(stats["avg_ms"]))
        y_rss_disp = (f"{fmt_rss(stats['peak_rss_kb'])}→{fmt_rss(yona_rss_adj)}"
                      if ysu_rss > stats["peak_rss_kb"] * 0.05
                      else fmt_rss(stats["peak_rss_kb"]))
        line = f"    {stem:.<28} {y_time_disp:>14}  {y_rss_disp:>14}"

        for lang in compare_langs:
            spec = REF_LANGS.get(lang)
            if not spec:
                continue
            cmd = spec["prep"](stem, BUILD_DIR)
            if cmd is None:
                continue
            ref_stats = run_benchmark(cmd, iterations)
            if not ref_stats:
                continue
            # Skip if the reference produced the wrong output — meaningless to time.
            if not check_correctness(bench, ref_stats["output"]):
                continue
            raw = ref_stats["avg_ms"]
            raw_rss = ref_stats["peak_rss_kb"]
            # Startup-adjusted: subtract each runtime's cold-start floor
            # for both time and memory. Clamp to 0.01ms / 0KB so we don't
            # report negative work when the benchmark's real work is
            # below the startup-noise floor.
            su = startup_ms.get(lang, 0.0)
            su_rss = startup_rss.get(lang, 0)
            adj = max(raw - su, 0.01)
            adj_rss = max(raw_rss - su_rss, 0)
            ratio = stats["avg_ms"] / raw if raw > 0 else 0
            adj_ratio = yona_adj / adj if adj > 0 else 0
            r[f"{lang}_avg_ms"] = raw
            r[f"{lang}_avg_ms_adj"] = adj
            r[f"{lang}_startup_ms"] = su
            r[f"{lang}_rss_kb"] = raw_rss
            r[f"{lang}_rss_kb_adj"] = adj_rss
            r[f"{lang}_startup_rss_kb"] = su_rss
            r[f"{lang}_ratio"] = ratio
            r[f"{lang}_ratio_adj"] = adj_ratio
            time_disp = (f"{fmt_time(raw)}→{fmt_time(adj)}"
                         if su > raw * 0.05 else fmt_time(raw))
            rss_disp = (f"{fmt_rss(raw_rss)}→{fmt_rss(adj_rss)}"
                        if su_rss > raw_rss * 0.05 else fmt_rss(raw_rss))
            line += (f"  {spec['label']}: {time_disp:>14} "
                     f"{rss_disp:>14}  {adj_ratio:.1f}x")

        print(line)
        results.append(r)

    return results


def get_git_info():
    """Get current git commit hash and subject."""
    try:
        sha = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            capture_output=True, text=True, cwd=ROOT
        ).stdout.strip()
        subject = subprocess.run(
            ["git", "log", "-1", "--format=%s"],
            capture_output=True, text=True, cwd=ROOT
        ).stdout.strip()
        dirty = subprocess.run(
            ["git", "diff", "--quiet"], cwd=ROOT
        ).returncode != 0
        return {"sha": sha, "subject": subject, "dirty": dirty}
    except Exception:
        return {"sha": "unknown", "subject": "", "dirty": False}


def save_results(all_results, git_info):
    """Append benchmark results to bench/history.jsonl."""
    import datetime
    history_file = BENCH_DIR / "history.jsonl"
    entry = {
        "timestamp": datetime.datetime.now().isoformat(),
        "git": git_info,
        "results": all_results,
    }
    with open(history_file, "a") as f:
        f.write(json.dumps(entry, default=str) + "\n")
    print(f"\n  Results saved to {history_file.relative_to(ROOT)}")


def main():
    parser = argparse.ArgumentParser(description="Yona Benchmark Runner")
    parser.add_argument("filter", nargs="?", help="Filter by benchmark name")
    parser.add_argument("-n", "--iterations", type=int, default=3)
    parser.add_argument("-O", "--opt-level", type=int, default=2)
    parser.add_argument("--compare-c", action="store_true",
                        help="Compare against C reference impls")
    parser.add_argument("--compare-erl", action="store_true",
                        help="Compare against Erlang reference impls (erlc-compiled)")
    parser.add_argument(
        "--skip-erl",
        action="store_true",
        help="Skip all Erlang reference usage (prep, startup probe, verify). "
             "Same as YONA_BENCH_SKIP_ERLANG=1; use when erl/erlc crash on Windows OTP.",
    )
    # `--compare` without a value means "-c" (backwards compat). With a value,
    # it takes a CSV of language keys: `--compare=c,erl`. The `nargs="?"` +
    # `const` trick gives us both forms from a single flag.
    parser.add_argument("--compare",
                        dest="compare_langs_csv",
                        nargs="?", const="c",
                        help="Comma-separated languages to compare against "
                             f"({','.join(REF_LANGS.keys())}). Defaults to 'c' "
                             f"when the flag is present without a value.")
    parser.add_argument(
        "--verify-reference-outputs",
        action="store_true",
        help="Check reference implementations only: each benchmark with a "
             ".expected file is run once per language (see "
             "--reference-verify-langs). Exits 1 on stdout mismatch. Does not "
             "run the Yona timing suite or require yonac.",
    )
    parser.add_argument(
        "--reference-verify-langs",
        default="c",
        metavar="LANGS",
        help="Comma-separated reference keys (c,erl,java,...) or 'all'. "
             "Used only with --verify-reference-outputs. Default: c",
    )
    parser.add_argument("--all-opt-levels", action="store_true", help="Run O0, O1, O2, O3")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--save", action="store_true", help="Save results to bench/history.jsonl")
    parser.add_argument("--yonac", help="Path to yonac executable")
    args = parser.parse_args()

    global _ERLANG_REFERENCE_ENABLED
    if args.skip_erl:
        _ERLANG_REFERENCE_ENABLED = False

    global YONAC
    if args.yonac:
        YONAC = Path(args.yonac)

    # Resolve comparison languages from the various flags.
    compare_langs = []
    if args.compare_c:
        compare_langs.append("c")
    if args.compare_erl:
        compare_langs.append("erl")
    if args.compare_langs_csv:
        for lang in args.compare_langs_csv.split(","):
            lang = lang.strip().lower()
            if lang and lang not in compare_langs:
                if lang not in REF_LANGS:
                    print(f"Error: unknown comparison language '{lang}'. "
                          f"Available: {','.join(REF_LANGS.keys())}")
                    sys.exit(1)
                compare_langs.append(lang)
    compare_langs = tuple(
        x for x in compare_langs if x != "erl" or erlang_reference_enabled()
    )

    ensure_benchmark_data()

    benchmarks = find_benchmarks(args.filter)
    if not benchmarks:
        print("No benchmarks found.")
        sys.exit(1)

    if args.verify_reference_outputs:
        raw = args.reference_verify_langs.strip().lower()
        if raw == "all":
            verify_langs = tuple(REF_LANGS.keys())
        else:
            verify_langs = tuple(
                x.strip().lower()
                for x in raw.split(",")
                if x.strip()
            )
        for lang in verify_langs:
            if lang not in REF_LANGS:
                print(
                    f"Error: unknown language '{lang}' in --reference-verify-langs. "
                    f"Use: {','.join(REF_LANGS.keys())} or all"
                )
                sys.exit(1)
        verify_langs = tuple(
            x for x in verify_langs if x != "erl" or erlang_reference_enabled()
        )
        print(
            "Reference output conformance "
            f"(langs={','.join(verify_langs)}, filter={args.filter or '*'})\n"
            f"{'=' * 60}"
        )
        nfail = verify_reference_outputs(benchmarks, verify_langs, BUILD_DIR)
        sys.exit(1 if nfail > 0 else 0)

    if not YONAC.exists():
        print(f"Error: yonac not found at {YONAC}")
        print("Run: cmake --build --preset build-debug-linux")
        sys.exit(1)

    all_results = {}

    if args.all_opt_levels:
        for level in [0, 1, 2, 3]:
            header = f"O{level}"
            print(f"\n{'=' * 60}")
            print(f"  Optimization Level: -O{level}  ({args.iterations} iterations)")
            print(f"{'=' * 60}")
            all_results[f"O{level}"] = run_suite(benchmarks, level, args.iterations, compare_langs)
    else:
        print(f"Yona Benchmarks — -O{args.opt_level}, {args.iterations} iterations")
        print(f"{'=' * 60}")
        all_results[f"O{args.opt_level}"] = run_suite(benchmarks, args.opt_level, args.iterations, compare_langs)

    # Summary
    print(f"\n{'=' * 60}")
    for key, results in all_results.items():
        ok = sum(1 for r in results if r.get("status") == "ok")
        print(f"  {key}: {ok}/{len(results)} passed")

    if args.json:
        print("\n" + json.dumps(all_results, indent=2, default=str))

    if args.save:
        save_results(all_results, get_git_info())


if __name__ == "__main__":
    main()
