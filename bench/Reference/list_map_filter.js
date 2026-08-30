const nums = [];
for (let n = 10000; n >= 1; n--) nums.push(n);
let s = 0;
for (const x of nums) {
    const d = x * 2;
    if (d % 4 === 0) s += d;
}
console.log(s);
