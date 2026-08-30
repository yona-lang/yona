// Parallel fibonacci: 8 x fib(30) using pthreads
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

static long fib(int n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

static void* fib_thread(void* arg) {
    int n = *(int*)arg;
    long result = fib(n);
    long* r = malloc(sizeof(long));
    *r = result;
    return r;
}

int main(void) {
    pthread_t threads[8];
    int args[8] = {30, 30, 30, 30, 30, 30, 30, 30};
    for (int i = 0; i < 8; i++)
        pthread_create(&threads[i], NULL, fib_thread, &args[i]);

    long total = 0;
    for (int i = 0; i < 8; i++) {
        void* ret;
        pthread_join(threads[i], &ret);
        total += *(long*)ret;
        free(ret);
    }
    printf("%ld\n", total);
    return 0;
}
