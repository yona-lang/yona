/* C reference: 4 sequential 100ms sleeps */
#include <stdio.h>
#include <unistd.h>

static long slow_identity(long ms) {
    usleep((useconds_t)(ms * 1000));
    return ms;
}

int main(void) {
    long a = slow_identity(100);
    long b = slow_identity(a + 0);
    long c = slow_identity(b + 0);
    long d = slow_identity(c + 0);
    printf("%ld\n", d);
    return 0;
}
