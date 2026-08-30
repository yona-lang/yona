/* C reference: read a ~1.2MB file, print length */
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    FILE* f = fopen("bench/data/bench_text.txt", "r");
    if (!f) { printf("0\n"); return 1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc((size_t)len + 1);
    fread(buf, 1, (size_t)len, f);
    fclose(f);
    printf("%ld\n", len);
    free(buf);
    return 0;
}
