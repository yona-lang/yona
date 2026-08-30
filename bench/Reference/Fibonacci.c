#include <stdio.h>
#include <stdint.h>
int64_t fib(int64_t n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}
int main(void) { printf("%ld\n", fib(35)); return 0; }
