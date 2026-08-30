// Parallel exec to match Yona's let-parallelism
const { exec } = require('child_process');
const cmds = ['echo hello', 'echo world', 'echo yona'];
Promise.all(cmds.map(cmd => new Promise((resolve, reject) => {
    exec(cmd, (err, stdout) => err ? reject(err) : resolve(stdout.trim().length));
}))).then(lens => console.log(lens.reduce((a, b) => a + b, 0)));
