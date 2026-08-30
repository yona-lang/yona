/* C reference: sum 10K elements in a contiguous int64_t array */
#include <stdio.h>
#include <stdint.h>

int main(void) {
    int64_t arr[10000];
    for (int i = 0; i < 10000; i++) arr[i] = i + 1;
    int64_t sum = 0;
    for (int i = 0; i < 10000; i++) sum += arr[i];
    printf("%ld\n", sum);
    return 0;
}
