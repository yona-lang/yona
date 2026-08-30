total = 0
with open("bench/data/large_text.txt") as f:
    for line in f:
        # `line` includes the trailing newline; strip for consistent
        # length accounting with the other runtimes.
        total += len(line.rstrip("\n"))
print(total)
