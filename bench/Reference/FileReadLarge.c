/* Read a 50MB file — idiomatic C with 64KB buffer */
#include <stdio.h>
#include <string.h>
int main(void) {
    FILE* f = fopen("bench/data/large_text.txt", "r");
    if (!f) return 1;
    char buf[65536];
    long total = 0;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) total += n;
    fclose(f);
    printf("%ld\n", total);
    return 0;
}
