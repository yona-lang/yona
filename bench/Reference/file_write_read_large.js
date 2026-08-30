const fs = require('fs');
const data = fs.readFileSync("bench/data/large_text.txt");
fs.writeFileSync("/tmp/node_bench_large_write.txt", data);
const data2 = fs.readFileSync("/tmp/node_bench_large_write.txt");
console.log(data2.length);
