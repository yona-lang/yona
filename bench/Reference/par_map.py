#!/usr/bin/env python3
"""Python reference: parallel cube of 1..20"""

from concurrent.futures import ThreadPoolExecutor

with ThreadPoolExecutor() as ex:
    results = list(ex.map(lambda x: x * x * x, range(1, 21)))
print("[" + ", ".join(str(r) for r in results) + "]")
