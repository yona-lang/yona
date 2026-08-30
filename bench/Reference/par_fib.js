// Parallel fibonacci: 8 x fib(30) using worker_threads
const { Worker, isMainThread, parentPort, workerData } = require('worker_threads');

function fib(n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

if (!isMainThread) {
    parentPort.postMessage(fib(workerData));
} else {
    const workers = [];
    for (let i = 0; i < 8; i++) {
        workers.push(new Promise((resolve) => {
            const w = new Worker(__filename, { workerData: 30 });
            w.on('message', resolve);
        }));
    }
    Promise.all(workers).then(results => {
        console.log(results.reduce((a, b) => a + b, 0));
    });
}
