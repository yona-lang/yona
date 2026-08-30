#include <stdio.h>
#include <stdint.h>
int main(void) {
    int64_t acc = 0;
    for (int64_t n = 1000000; n > 0; n--) acc += n * n;
    printf("%ld\n", acc);
    return 0;
}
