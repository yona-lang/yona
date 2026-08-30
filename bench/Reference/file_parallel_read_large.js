const fs = require('fs').promises;
Promise.all([1,2,3,4].map(i =>
    fs.readFile(`bench/data/chunk_${i}.txt`, 'utf8').then(d => d.length)
)).then(lens => console.log(lens.reduce((a,b) => a+b, 0)));
