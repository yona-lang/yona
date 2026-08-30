let n = 1000000n, acc = 0n;
while (n > 0n) { acc += n * n; n--; }
console.log(acc.toString());
