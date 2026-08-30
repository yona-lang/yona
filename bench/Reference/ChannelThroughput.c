/* Bounded MPSC channel throughput benchmark.
 * Producer thread sends 1..10000, consumer (main) sums them. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define CAP 64
#define N 10000

typedef struct {
    long buf[CAP];
    int head, tail, count, closed;
    pthread_mutex_t m;
    pthread_cond_t not_full, not_empty;
} chan_t;

static void chan_init(chan_t* c) {
    c->head = c->tail = c->count = c->closed = 0;
    pthread_mutex_init(&c->m, NULL);
    pthread_cond_init(&c->not_full, NULL);
    pthread_cond_init(&c->not_empty, NULL);
}

static void chan_send(chan_t* c, long v) {
    pthread_mutex_lock(&c->m);
    while (c->count == CAP) pthread_cond_wait(&c->not_full, &c->m);
    c->buf[c->tail] = v;
    c->tail = (c->tail + 1) % CAP;
    c->count++;
    pthread_cond_signal(&c->not_empty);
    pthread_mutex_unlock(&c->m);
}

static int chan_recv(chan_t* c, long* out) {
    pthread_mutex_lock(&c->m);
    while (c->count == 0 && !c->closed) pthread_cond_wait(&c->not_empty, &c->m);
    if (c->count == 0 && c->closed) { pthread_mutex_unlock(&c->m); return 0; }
    *out = c->buf[c->head];
    c->head = (c->head + 1) % CAP;
    c->count--;
    pthread_cond_signal(&c->not_full);
    pthread_mutex_unlock(&c->m);
    return 1;
}

static void chan_close(chan_t* c) {
    pthread_mutex_lock(&c->m);
    c->closed = 1;
    pthread_cond_broadcast(&c->not_empty);
    pthread_mutex_unlock(&c->m);
}

static void* producer(void* arg) {
    chan_t* c = arg;
    for (long i = 1; i <= N; i++) chan_send(c, i);
    chan_close(c);
    return NULL;
}

int main(void) {
    chan_t c;
    chan_init(&c);
    pthread_t t;
    pthread_create(&t, NULL, producer, &c);
    long sum = 0, v;
    while (chan_recv(&c, &v)) sum += v;
    pthread_join(t, NULL);
    printf("%ld\n", sum);
    return 0;
}
