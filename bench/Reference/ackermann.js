function ackIter(m, n) {
    const stack = [m];
    let value = n;
    while (stack.length > 0) {
        const mm = stack.pop();
        if (mm === 0) {
            value = value + 1;
        } else if (value === 0) {
            stack.push(mm - 1);
            value = 1;
        } else {
            stack.push(mm - 1);
            stack.push(mm);
            value = value - 1;
        }
    }
    return value;
}

console.log(ackIter(3, 11));
