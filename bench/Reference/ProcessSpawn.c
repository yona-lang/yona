/* Process spawn: run seq 1 10000, read output, measure length */
#include <stdio.h>
#include <string.h>

int main(void) {
    FILE* f = popen("seq 1 10000", "r");
    if (!f) return 1;
    char buf[256];
    int len = 0;
    while (fgets(buf, sizeof(buf), f)) len += strlen(buf);
    pclose(f);
    printf("%d\n", len);
    return 0;
}
