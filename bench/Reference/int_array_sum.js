const a = new Int32Array(10000);
for (let i = 0; i < 10000; i++) a[i] = i + 1;
let s = 0;
for (let i = 0; i < a.length; i++) s += a[i];
console.log(s);
