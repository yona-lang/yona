/* C reference: build a hash set of 10K elements, return size.
 * Uses open-addressing with linear probing. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CAP 16384

typedef struct { int64_t key; int used; } entry_t;

static uint64_t hash64(int64_t k) {
    uint64_t x = (uint64_t)k;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

int main(void) {
    entry_t *table = calloc(CAP, sizeof(entry_t));
    int64_t size = 0;
    for (int64_t n = 10000; n >= 1; n--) {
        uint64_t h = hash64(n);
        int idx = (int)(h & (CAP - 1));
        while (table[idx].used) {
            if (table[idx].key == n) break;
            idx = (idx + 1) & (CAP - 1);
        }
        if (!table[idx].used) {
            table[idx].key = n;
            table[idx].used = 1;
            size++;
        }
    }
    printf("%ld\n", size);
    free(table);
    return 0;
}
