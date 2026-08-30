/* Read 4 x 10MB files in parallel — idiomatic C */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static void* read_file(void* arg) {
    const char* path = (const char*)arg;
    FILE* f = fopen(path, "r");
    if (!f) return NULL;
    char buf[65536];
    long total = 0;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) total += n;
    fclose(f);
    long* r = malloc(sizeof(long));
    *r = total;
    return r;
}

int main(void) {
    const char* paths[] = {
        "bench/data/chunk_1.txt", "bench/data/chunk_2.txt",
        "bench/data/chunk_3.txt", "bench/data/chunk_4.txt"
    };
    pthread_t t[4];
    for (int i = 0; i < 4; i++)
        pthread_create(&t[i], NULL, read_file, (void*)paths[i]);
    long total = 0;
    for (int i = 0; i < 4; i++) {
        void* r;
        pthread_join(t[i], &r);
        if (r) { total += *(long*)r; free(r); }
    }
    printf("%ld\n", total);
    return 0;
}
