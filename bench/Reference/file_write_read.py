with open("bench/data/bench_text.txt") as f:
    data = f.read()
with open("/tmp/yona_bench_write_py.txt", "w") as f:
    f.write(data)
with open("/tmp/yona_bench_write_py.txt") as f:
    data2 = f.read()
print(len(data2))
