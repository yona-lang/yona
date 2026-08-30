#include <stdio.h>
#include <stdint.h>
int64_t ack(int64_t m, int64_t n) {
    if (m == 0) return n + 1;
    if (n == 0) return ack(m - 1, 1);
    return ack(m - 1, ack(m, n - 1));
}
int main(void) { printf("%ld\n", ack(3, 11)); return 0; }
