"""Pipeline benchmark — Python queue.Queue."""

import queue
import threading

CAP = 32
N = 5000


def producer(q):
    for n in range(1, N + 1):
        q.put(n * 2)
    q.put(None)


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
