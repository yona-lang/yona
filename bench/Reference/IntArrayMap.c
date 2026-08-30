/* C reference: map (*2) then sum 10K elements */
#include <stdio.h>
#include <stdint.h>

int main(void) {
    int64_t arr[10000], doubled[10000];
    for (int i = 0; i < 10000; i++) arr[i] = i + 1;
    for (int i = 0; i < 10000; i++) doubled[i] = arr[i] * 2;
    int64_t sum = 0;
    for (int i = 0; i < 10000; i++) sum += doubled[i];
    printf("%ld\n", sum);
    return 0;
}
