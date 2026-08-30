/* C reference: read file line by line, count lines */
#include <stdio.h>
int main(void) {
    FILE* f = fopen("bench/data/bench_text.txt", "r");
    if (!f) return 1;
    int count = 0;
    char buf[4096];
    while (fgets(buf, sizeof(buf), f)) count++;
    fclose(f);
    printf("%d\n", count);
    return 0;
}
