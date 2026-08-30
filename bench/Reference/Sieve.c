#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    int n = 500;
    char *is_prime = calloc(n + 1, 1);
    memset(is_prime, 1, n + 1);
    is_prime[0] = is_prime[1] = 0;
    for (int i = 2; i * i <= n; i++)
        if (is_prime[i])
            for (int j = i * i; j <= n; j += i) is_prime[j] = 0;
    int count = 0;
    for (int i = 2; i <= n; i++) if (is_prime[i]) count++;
    printf("%d\n", count);
    free(is_prime);
    return 0;
}
