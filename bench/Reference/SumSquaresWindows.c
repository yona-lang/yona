#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

int main(void) {
    int64_t acc = 0;
    for (int64_t n = 1000000; n > 0; n--) acc += n * n;
    printf("%" PRId64 "\n", acc);
    return 0;
}
