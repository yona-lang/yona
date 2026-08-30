#!/usr/bin/env python3
"""Python reference: binary file read + write + read-back"""

with open("bench/data/bench_binary.bin", "rb") as f:
    data = f.read()

with open("/tmp/py_bench_binary_wr.bin", "wb") as f:
    f.write(data)

with open("/tmp/py_bench_binary_wr.bin", "rb") as f:
    data2 = f.read()

print(len(data2))
