/* C analogue: stack arrays, no heap — measures tight-loop overhead for
 * comparison with Yona's multi-binding let + task-group bump arena path. */
#include <stdio.h>
int main(void) {
    long long acc = 0;
    for (int n = 50000; n > 0; n--) {
        int a[2] = {1, 2};
        int b[2] = {3, 4};
        (void)a;
        (void)b;
        acc += 10;
    }
    printf("%ld\n", (long)acc);
    return 0;
}
