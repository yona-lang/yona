import array

a = array.array("i", range(1, 10001))
doubled = array.array("i", (x * 2 for x in a))
print(sum(doubled))
