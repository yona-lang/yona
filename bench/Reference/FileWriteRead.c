/* C reference: read file, write copy, read copy, print length */
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    FILE* f = fopen("bench/data/bench_text.txt", "r");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc((size_t)len);
    fread(buf, 1, (size_t)len, f);
    fclose(f);
    FILE* w = fopen("/tmp/yona_bench_write_c.txt", "w");
    fwrite(buf, 1, (size_t)len, w);
    fclose(w);
    free(buf);
    f = fopen("/tmp/yona_bench_write_c.txt", "r");
    fseek(f, 0, SEEK_END);
    long len2 = ftell(f);
    fclose(f);
    printf("%ld\n", len2);
    return 0;
}
