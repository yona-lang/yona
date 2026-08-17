#!/usr/bin/env python3
"""Append YONA_INPROCESS_LLD_* from CMakeCache.txt to the GitHub step summary."""

from __future__ import annotations

import os
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: report-embedded-lld.py <CMakeCache.txt> <title>", file=sys.stderr)
        return 2

    cache = Path(sys.argv[1])
    title = sys.argv[2]
    text = cache.read_text(encoding="utf-8", errors="ignore")
    enabled = ""
    reason = ""
    for line in text.splitlines():
        if line.startswith("YONA_INPROCESS_LLD_AVAILABLE:BOOL="):
            enabled = line.split("=", 1)[1].strip()
        elif line.startswith("YONA_INPROCESS_LLD_REASON:STRING="):
            reason = line.split("=", 1)[1].strip()
    if not enabled:
        print("Missing YONA_INPROCESS_LLD_AVAILABLE in CMakeCache.txt", file=sys.stderr)
        return 1
    if not reason:
        print("Missing YONA_INPROCESS_LLD_REASON in CMakeCache.txt", file=sys.stderr)
        return 1

    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as fh:
            fh.write(f"### Embedded LLD ({title})\n")
            fh.write(f"- active: `{enabled}`\n")
            fh.write(f"- reason: `{reason}`\n\n")
    print(f"Embedded LLD active={enabled}; reason={reason}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
