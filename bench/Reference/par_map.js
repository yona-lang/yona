// Node.js reference: parallel cube of 1..20 (JS is single-threaded; use map)
const results = [];
for (let i = 1; i <= 20; i++) results.push(i * i * i);
console.log("[" + results.join(", ") + "]");
