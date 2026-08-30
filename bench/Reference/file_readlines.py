total = 0
with open("bench/data/bench_text.txt", encoding="utf-8", newline="") as f:
    for line in f:
        total += len(line.rstrip("\r\n"))
print(total)
