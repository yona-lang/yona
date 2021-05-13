const fs = require('fs');
const readline = require('readline');
const ps = require('promise-socket');
const net = require('net');

const socket = new net.Socket();
const promiseSocket = new ps.PromiseSocket(socket);

async function processLineByLine() {
    const fileStream = fs.createReadStream('../data/big.txt');

    const rl = readline.createInterface({
        input: fileStream,
        crlfDelay: Infinity
    });
    // Note: we use the crlfDelay option to recognize all instances of CR LF
    // ('\r\n') in input.txt as a single line break.

    await socket.connect({port: 5555, host: "localhost"});

    for await (const line of rl) {
        // Each line in input.txt will be successively available here as `line`.
        await promiseSocket.write(await line + '\n', 'utf8');
    }
    await promiseSocket.write('--over--\n', 'utf8');
    await promiseSocket.end();
}


(async function () {
    const startTime = process.hrtime.bigint();
    await processLineByLine();
    console.log((process.hrtime.bigint() - startTime) / 1000n);
}());
