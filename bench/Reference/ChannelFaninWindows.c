#include <stdio.h>

int main(void) {
    long sum = 0;
    for (long n = 1; n <= 5000; n++) sum += n;
    printf("%ld\n", sum);
    return 0;
}
