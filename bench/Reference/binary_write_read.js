// Node.js reference: binary file read + write + read-back
const fs = require('fs');

const data = fs.readFileSync('bench/data/bench_binary.bin');
fs.writeFileSync('/tmp/js_bench_binary_wr.bin', data);
const data2 = fs.readFileSync('/tmp/js_bench_binary_wr.bin');
console.log(data2.length);
