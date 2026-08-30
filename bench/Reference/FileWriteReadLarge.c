/* Read 50MB, write to disk, read back */
#include <stdio.h>
#include <string.h>
int main(void) {
    FILE* fi = fopen("bench/data/large_text.txt", "r");
    FILE* fo = fopen("/tmp/yona_bench_large_write.txt", "w");
    if (!fi || !fo) return 1;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fi)) > 0) fwrite(buf, 1, n, fo);
    fclose(fi); fclose(fo);
    fi = fopen("/tmp/yona_bench_large_write.txt", "r");
    long total = 0;
    while ((n = fread(buf, 1, sizeof(buf), fi)) > 0) total += n;
    fclose(fi);
    printf("%ld\n", total);
    return 0;
}
