/* Windows C reference: read file, write copy, read length */
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    const char* out_path = "bench/data/yona_bench_write_c.txt";
    FILE* f = fopen("bench/data/bench_text.txt", "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc((size_t)len);
    if (!buf) return 1;
    fread(buf, 1, (size_t)len, f);
    fclose(f);

    FILE* w = fopen(out_path, "wb");
    if (!w) return 1;
    fwrite(buf, 1, (size_t)len, w);
    fclose(w);
    free(buf);

    f = fopen(out_path, "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    long len2 = ftell(f);
    fclose(f);
    printf("%ld\n", len2);
    return 0;
}
