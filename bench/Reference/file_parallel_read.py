from concurrent.futures import ThreadPoolExecutor


def read_file(path):
    with open(path) as f:
        return len(f.read())


path = "bench/data/bench_text.txt"
with ThreadPoolExecutor(max_workers=3) as ex:
    futures = [ex.submit(read_file, path) for _ in range(3)]
    print(sum(f.result() for f in futures))
