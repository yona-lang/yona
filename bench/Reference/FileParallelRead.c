/* Parallel file read: 3 concurrent reads using pthreads */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static void* read_file(void* arg) {
    const char* path = (const char*)arg;
    FILE* f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char* buf = malloc(sz + 1);
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);
    long* len = malloc(sizeof(long));
    *len = strlen(buf);
    free(buf);
    return len;
}

int main(void) {
    const char* path = "bench/data/bench_text.txt";
    pthread_t t[3];
    for (int i = 0; i < 3; i++)
        pthread_create(&t[i], NULL, read_file, (void*)path);
    long total = 0;
    for (int i = 0; i < 3; i++) {
        void* r;
        pthread_join(t[i], &r);
        if (r) { total += *(long*)r; free(r); }
    }
    printf("%ld\n", total);
    return 0;
}
