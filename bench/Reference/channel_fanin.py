"""Fan-in benchmark — two producers feed one queue."""

import queue
import threading

CAP = 32


def producer(q, coord, lo, hi):
    for n in range(lo, hi + 1):
        q.put(n)
    coord.put(1)


def waiter(work, coord):
    acc = 0
    while True:
        coord.get()
        acc += 1
        if acc >= 2:
            work.put(None)
            return


def main():
    work = queue.Queue(CAP)
    coord = queue.Queue(4)
    t1 = threading.Thread(target=producer, args=(work, coord, 1, 2500))
    t2 = threading.Thread(target=producer, args=(work, coord, 2501, 5000))
    t3 = threading.Thread(target=waiter, args=(work, coord))
    for t in (t1, t2, t3):
        t.start()
    s = 0
    while True:
        v = work.get()
        if v is None:
            break
        s += v
    for t in (t1, t2, t3):
        t.join()
    print(s)


if __name__ == "__main__":
    main()
