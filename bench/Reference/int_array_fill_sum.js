const a = new Int32Array(10000);
a.fill(7);
let s = 0;
for (let i = 0; i < a.length; i++) s += a[i];
console.log(s);
