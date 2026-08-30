const a = new Int32Array(10000);
for (let i = 0; i < 10000; i++) a[i] = i + 1;
const doubled = new Int32Array(10000);
for (let i = 0; i < 10000; i++) doubled[i] = a[i] * 2;
let s = 0;
for (let i = 0; i < doubled.length; i++) s += doubled[i];
console.log(s);
