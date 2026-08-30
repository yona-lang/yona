/* C reference: sequential cube of 1..20 */
#include <stdio.h>

int main(void) {
    long results[20];
    for (int i = 0; i < 20; i++)
        results[i] = (long)(i + 1) * (i + 1) * (i + 1);
    printf("[");
    for (int i = 0; i < 20; i++) {
        if (i > 0) printf(", ");
        printf("%ld", results[i]);
    }
    printf("]\n");
    return 0;
}
