with open("bench/data/large_text.txt", "rb") as f:
    data = f.read()
with open("/tmp/py_bench_large_write.txt", "wb") as f:
    f.write(data)
with open("/tmp/py_bench_large_write.txt", "rb") as f:
    data2 = f.read()
print(len(data2))
