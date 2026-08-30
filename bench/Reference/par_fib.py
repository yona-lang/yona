"""Parallel fibonacci: 8 x fib(30) using multiprocessing"""

from multiprocessing import Pool


def fib(n):
    if n <= 1:
        return n
    return fib(n - 1) + fib(n - 2)


if __name__ == "__main__":
    with Pool(8) as pool:
        results = pool.map(fib, [30] * 8)
    print(sum(results))
