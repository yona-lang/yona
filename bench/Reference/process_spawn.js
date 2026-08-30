const { execSync } = require('child_process');
const command = process.platform === 'win32'
    ? 'cmd /c for /l %i in (1,1,10000) do @echo %i'
    : 'seq 1 10000';
const output = execSync(command, { encoding: 'utf8' });
const normalized = output.replace(/\r\n/g, '\n').replace(/\r/g, '\n');
console.log(normalized.replace(/\n$/, '').length);
