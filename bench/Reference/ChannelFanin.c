/* Fan-in benchmark: two producers (1..2500 and 2501..5000) feed one work
 * channel; a coordinator closes the channel when both report done. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define CAP 32
#define COORD_CAP 4
#define LO_A 1
#define HI_A 2500
#define LO_B 2501
#define HI_B 5000

typedef struct {
    long buf[CAP];
    int head, tail, count, closed, capacity;
    pthread_mutex_t m;
    pthread_cond_t not_full, not_empty;
} chan_t;

static void chan_init(chan_t* c, int cap) {
    c->head = c->tail = c->count = c->closed = 0;
    c->capacity = cap;
    pthread_mutex_init(&c->m, NULL);
    pthread_cond_init(&c->not_full, NULL);
    pthread_cond_init(&c->not_empty, NULL);
}
static void chan_send(chan_t* c, long v) {
    pthread_mutex_lock(&c->m);
    while (c->count == c->capacity) pthread_cond_wait(&c->not_full, &c->m);
    c->buf[c->tail] = v;
    c->tail = (c->tail + 1) % c->capacity;
    c->count++;
    pthread_cond_signal(&c->not_empty);
    pthread_mutex_unlock(&c->m);
}
static int chan_recv(chan_t* c, long* out) {
    pthread_mutex_lock(&c->m);
    while (c->count == 0 && !c->closed) pthread_cond_wait(&c->not_empty, &c->m);
    if (c->count == 0 && c->closed) { pthread_mutex_unlock(&c->m); return 0; }
    *out = c->buf[c->head];
    c->head = (c->head + 1) % c->capacity;
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

typedef struct { chan_t *work, *coord; long lo, hi; } prod_arg_t;

static void* producer(void* arg) {
    prod_arg_t* a = arg;
    for (long n = a->lo; n <= a->hi; n++) chan_send(a->work, n);
    chan_send(a->coord, 1);
    return NULL;
}

typedef struct { chan_t *work, *coord; } wait_arg_t;
static void* waiter(void* arg) {
    wait_arg_t* a = arg;
    long acc = 0, v;
    while (chan_recv(a->coord, &v)) {
        acc++;
        if (acc >= 2) { chan_close(a->work); break; }
    }
    return NULL;
}

int main(void) {
    chan_t work, coord;
    chan_init(&work, CAP);
    chan_init(&coord, COORD_CAP);
    pthread_t t1, t2, t3;
    prod_arg_t a1 = {&work, &coord, LO_A, HI_A};
    prod_arg_t a2 = {&work, &coord, LO_B, HI_B};
    wait_arg_t aw = {&work, &coord};
    pthread_create(&t1, NULL, producer, &a1);
    pthread_create(&t2, NULL, producer, &a2);
    pthread_create(&t3, NULL, waiter, &aw);
    long sum = 0, v;
    while (chan_recv(&work, &v)) sum += v;
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    pthread_join(t3, NULL);
    printf("%ld\n", sum);
    return 0;
}
