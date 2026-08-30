/* C reference: parallel cube of 1..20 via pthreads */
#include <stdio.h>
#include <pthread.h>

#define N 20
static long results[N];

static void* worker(void* arg) {
    long i = (long)arg;
    results[i] = (i + 1) * (i + 1) * (i + 1);
    return NULL;
}

int main(void) {
    pthread_t threads[N];
    for (long i = 0; i < N; i++)
        pthread_create(&threads[i], NULL, worker, (void*)i);
    for (int i = 0; i < N; i++)
        pthread_join(threads[i], NULL);
    printf("[");
    for (int i = 0; i < N; i++) {
        if (i > 0) printf(", ");
        printf("%ld", results[i]);
    }
    printf("]\n");
    return 0;
}
