#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
int safe(int queen, int *placed, int n, int col) {
    /* Walk placed in reverse — placed[n-1] is the most recently placed
     * queen, one column back from the candidate; col starts at 1 and
     * increments as we walk further back. */
    for (int i = n - 1; i >= 0; i--) {
        if (queen == placed[i]) return 0;
        if (abs(queen - placed[i]) == col) return 0;
        col++;
    }
    return 1;
}
int64_t solve(int n, int row, int *placed, int nplaced, int col) {
    if (row > n) return 1;
    if (col > n) return 0;
    int64_t count = 0;
    if (safe(col, placed, nplaced, 1)) {
        placed[nplaced] = col;
        count += solve(n, row + 1, placed, nplaced + 1, 1);
    }
    count += solve(n, row, placed, nplaced, col + 1);
    return count;
}
int main(void) {
    int placed[20];
    printf("%ld\n", solve(10, 1, placed, 0, 1));
    return 0;
}
