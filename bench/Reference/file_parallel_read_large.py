from concurrent.futures import ThreadPoolExecutor


def read_len(path):
    with open(path) as f:
        return len(f.read())


paths = [f"bench/data/chunk_{i}.txt" for i in range(1, 5)]
with ThreadPoolExecutor(4) as ex:
    print(sum(ex.map(read_len, paths)))
