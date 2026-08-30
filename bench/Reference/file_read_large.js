const fs = require('fs');
console.log(fs.readFileSync('bench/data/large_text.txt', 'utf8').length);
