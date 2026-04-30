#!/usr/bin/env python3
"""
Extract coarse metadata from Std\\GPU accelerator benches (crossover notes).

Scans bench/accelerators/gpu_*.yona for `build N` list size and optional
`filterGreaterThan T`. Intended to be pasted next to run_gpu_compare.py
--json-report output (see docs/gpu-transparent-lowering.md).

Usage:
  python3 bench/gpu_bench_meta.py
  python3 bench/gpu_bench_meta.py bench/accelerators/gpu_filter_hot.yona
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BENCH_DIR = ROOT / "bench" / "accelerators"

_build_re = re.compile(r"build\s+(\d+)\s+", re.MULTILINE)
_filter_re = re.compile(r"filterGreaterThan\s+(\d+)\s", re.MULTILINE)


def scrape(path: Path) -> dict:
    text = path.read_text(encoding="utf-8")
    builds = [int(m.group(1)) for m in _build_re.finditer(text)]
    filters = [int(m.group(1)) for m in _filter_re.finditer(text)]
    return {
        "file": str(path.relative_to(ROOT)).replace("\\", "/"),
        "build_n": max(builds) if builds else None,
        "filter_thresholds": filters or None,
    }


def main() -> int:
    paths = [Path(a) for a in sys.argv[1:]]
    if not paths:
        paths = sorted(BENCH_DIR.glob("gpu_*.yona"))
    out = [scrape(p) for p in paths if p.suffix == ".yona" and p.is_file()]
    print(json.dumps({"schema": "yona.gpu_bench_meta.v1", "benches": out}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
