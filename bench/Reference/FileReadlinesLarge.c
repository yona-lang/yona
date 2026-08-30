/* Stream 50MB file line-by-line, sum line lengths — idiomatic C */
#include <stdio.h>
#include <string.h>
int main(void) {
    FILE* f = fopen("bench/data/large_text.txt", "r");
    if (!f) return 1;
    char buf[8192];
    long total = 0;
    while (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') len--;
        total += len;
    }
    fclose(f);
    printf("%ld\n", total);
    return 0;
}
