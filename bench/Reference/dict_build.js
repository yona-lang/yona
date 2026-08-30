const d = new Map();
for (let n = 10000; n >= 1; n--) d.set(n, n * n);
console.log(d.size);
