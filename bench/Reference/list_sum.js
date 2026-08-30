// list_sum: build 1..10000 cons-style, sum via reduce.
const xs = [];
for (let n = 10000; n >= 1; n--) xs.push(n);
let s = 0;
for (const x of xs) s += x;
console.log(s);
