const fs = require('fs');
const readline = require('readline');

async function processLineByLine() {
    const fileStream = fs.createReadStream('../data/big.txt');

    const rl = readline.createInterface({
        input: fileStream,
        crlfDelay: Infinity
    });
    // Note: we use the crlfDelay option to recognize all instances of CR LF
    // ('\r\n') in input.txt as a single line break.

    let lines = 0;
    for await (const line of rl) {
        // Each line in input.txt will be successively available here as `line`.
        lines += 1;
    }
    return lines;
}


(async function() {
    var startTime = process.hrtime.bigint();
    let lines = await processLineByLine();
    console.log(lines);
    console.log((process.hrtime.bigint() - startTime) / 1000n);
}());
