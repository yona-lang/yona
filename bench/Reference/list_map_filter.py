nums = []
for n in range(10000, 0, -1):
    nums.append(n)
print(sum(x * 2 for x in nums if (x * 2) % 4 == 0))
