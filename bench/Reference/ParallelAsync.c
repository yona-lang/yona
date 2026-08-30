/* Parallel: 4 x 100ms sleep using pthreads */
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

static void* slow_id(void* arg) {
    usleep(100000);
    return arg;
}

int main(void) {
    pthread_t t[4];
    long args[4] = {100, 100, 100, 100};
    for (int i = 0; i < 4; i++)
        pthread_create(&t[i], NULL, slow_id, &args[i]);
    long total = 0;
    for (int i = 0; i < 4; i++) {
        void* r;
        pthread_join(t[i], &r);
        total += *(long*)r;
    }
    printf("%ld\n", total);
    return 0;
}
