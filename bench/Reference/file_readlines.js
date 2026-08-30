const fs = require('fs');
const data = fs.readFileSync('bench/data/bench_text.txt', 'utf8');
const lines = data.split('\n');
if (lines.length > 0 && lines[lines.length - 1] === '') {
    lines.pop();
}
const total = lines.reduce((acc, line) => acc + line.replace(/\r$/, '').length, 0);
console.log(total);
