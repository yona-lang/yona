/* Process exec: run 3 echo commands in parallel, sum output lengths.
 * NOTE: Yona's let-parallelism runs these concurrently, so the C
 * reference must also be parallel for a fair comparison. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static void* exec_thread(void* arg) {
    const char* cmd = (const char*)arg;
    FILE* f = popen(cmd, "r");
    if (!f) return NULL;
    char buf[256];
    int len = 0;
    while (fgets(buf, sizeof(buf), f)) len += strlen(buf);
    pclose(f);
    if (len > 0) len--; /* strip trailing newline */
    int* r = malloc(sizeof(int));
    *r = len;
    return r;
}

int main(void) {
    const char* cmds[3] = {"echo hello", "echo world", "echo yona"};
    pthread_t t[3];
    for (int i = 0; i < 3; i++)
        pthread_create(&t[i], NULL, exec_thread, (void*)cmds[i]);
    int total = 0;
    for (int i = 0; i < 3; i++) {
        void* r;
        pthread_join(t[i], &r);
        if (r) { total += *(int*)r; free(r); }
    }
    printf("%d\n", total);
    return 0;
}
