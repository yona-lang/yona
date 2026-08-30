import sys

sys.setrecursionlimit(100000)


def safe(queen, placed, col):
    if not placed:
        return True
    q = placed[0]
    if queen == q or abs(queen - q) == col:
        return False
    return safe(queen, placed[1:], col + 1)


def solve(n, row, placed):
    if row > n:
        return 1
    count = 0
    for col in range(1, n + 1):
        if safe(col, placed, 1):
            count += solve(n, row + 1, [col] + placed)
    return count


print(solve(10, 1, []))
