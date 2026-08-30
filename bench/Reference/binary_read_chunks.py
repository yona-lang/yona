#!/usr/bin/env python3
"""Python reference: binary chunked read"""

total = 0
with open("bench/data/bench_binary.bin", "rb") as f:
    while True:
        chunk = f.read(65536)
        if not chunk:
            break
        total += len(chunk)

print(total)
