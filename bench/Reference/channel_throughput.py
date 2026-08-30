"""Bounded channel throughput benchmark — Python queue.Queue."""

import queue
import threading

CAP = 64
N = 10000


def producer(q):
    for i in range(1, N + 1):
        q.put(i)
    q.put(None)  # sentinel


def main():
    q = queue.Queue(CAP)
    t = threading.Thread(target=producer, args=(q,))
    t.start()
    s = 0
    while True:
        v = q.get()
        if v is None:
            break
        s += v
    t.join()
    print(s)


if __name__ == "__main__":
    main()
