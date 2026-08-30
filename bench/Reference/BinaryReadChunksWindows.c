/* Windows C reference: binary chunked read */
#include <stdio.h>

int main(void) {
    FILE* f = fopen("bench/data/bench_binary.bin", "rb");
    if (!f) return 1;
    char buf[65536];
    long total = 0;
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) total += (long)n;
    fclose(f);
    printf("%ld\n", total);
    return 0;
}
