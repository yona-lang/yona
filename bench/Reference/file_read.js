const fs = require('fs');
const data = fs.readFileSync('bench/data/bench_text.txt', 'utf8');
console.log(data.length);
