const fs = require('fs').promises;
const path = 'bench/data/bench_text.txt';
Promise.all([
    fs.readFile(path, 'utf8'),
    fs.readFile(path, 'utf8'),
    fs.readFile(path, 'utf8'),
]).then(results => {
    console.log(results.reduce((a, s) => a + s.length, 0));
});
