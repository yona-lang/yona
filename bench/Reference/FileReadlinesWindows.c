/* Windows C reference: sum line lengths without line endings */
#include <stdio.h>

int main(void) {
    FILE* f = fopen("bench/data/bench_text.txt", "r");
    if (!f) return 1;
    long total = 0;
    char buf[4096];
    while (fgets(buf, sizeof(buf), f)) {
        long len = 0;
        while (buf[len] != '\0') len++;
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) len--;
        total += len;
    }
    fclose(f);
    printf("%ld\n", total);
    return 0;
}
