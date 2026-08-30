function safe(queen, placed, col) {
    if (placed.length === 0) return true;
    if (queen === placed[0] || Math.abs(queen - placed[0]) === col) return false;
    return safe(queen, placed.slice(1), col + 1);
}
function solve(n, row, placed) {
    if (row > n) return 1;
    let count = 0;
    for (let col = 1; col <= n; col++)
        if (safe(col, placed, 1)) count += solve(n, row + 1, [col, ...placed]);
    return count;
}
console.log(solve(10, 1, []));
