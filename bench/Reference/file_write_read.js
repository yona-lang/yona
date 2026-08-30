const fs = require('fs');
const data = fs.readFileSync('bench/data/bench_text.txt', 'utf8');
fs.writeFileSync('/tmp/yona_bench_write_js.txt', data);
const data2 = fs.readFileSync('/tmp/yona_bench_write_js.txt', 'utf8');
console.log(data2.length);
