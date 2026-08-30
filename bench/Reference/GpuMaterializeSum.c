#include <stdint.h>
#include <stdio.h>

int main(void) {
    int64_t sum = 0;
    for (int64_t x = 1; x <= 20000; x++) sum += x + 3;
    printf("%lld\n", (long long)sum);
    return 0;
}
