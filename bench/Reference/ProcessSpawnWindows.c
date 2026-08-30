/* Windows C reference: spawn sequence and measure normalized length */
#include <stdio.h>
#include <string.h>

int main(void) {
    FILE* f = _popen("cmd /c for /l %i in (1,1,10000) do @echo %i", "r");
    if (!f) return 1;
    char buf[256];
    int len = 0;
    while (fgets(buf, sizeof(buf), f)) {
        int n = (int)strlen(buf);
        for (int i = 0; i < n; i++) {
            if (buf[i] != '\r') len++;
        }
    }
    _pclose(f);
    if (len > 0 && len >= 1) len--; /* trim final newline */
    printf("%d\n", len);
    return 0;
}
