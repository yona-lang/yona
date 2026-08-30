// Node.js reference: binary chunked read
const fs = require('fs');

const fd = fs.openSync('bench/data/bench_binary.bin', 'r');
const buf = Buffer.alloc(65536);
let total = 0;
let n;
while ((n = fs.readSync(fd, buf, 0, buf.length, null)) > 0)
    total += n;
fs.closeSync(fd);
console.log(total);
