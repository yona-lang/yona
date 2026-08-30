const fs = require('fs');
const data = fs.readFileSync("bench/data/large_text.txt", "utf8");
let total = 0;
let start = 0;
for (let i = 0; i < data.length; i++) {
    if (data.charCodeAt(i) === 10) {
        total += (i - start);
        start = i + 1;
    }
}
if (start < data.length) total += data.length - start;
console.log(total);
