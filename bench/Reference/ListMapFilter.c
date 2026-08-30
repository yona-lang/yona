#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
typedef struct node { int64_t val; struct node *next; } node_t;
node_t* cons(int64_t v, node_t* next) {
    node_t *n = malloc(sizeof(node_t));
    n->val = v; n->next = next; return n;
}
int main(void) {
    node_t *nums = NULL;
    for (int i = 10000; i >= 1; i--) nums = cons(i, nums);
    node_t *doubled = NULL, **dp = &doubled;
    for (node_t *p = nums; p; p = p->next) {
        *dp = cons(p->val * 2, NULL); dp = &(*dp)->next;
    }
    node_t *evens = NULL, **ep = &evens;
    for (node_t *p = doubled; p; p = p->next) {
        if (p->val % 4 == 0) { *ep = cons(p->val, NULL); ep = &(*ep)->next; }
    }
    int64_t sum = 0;
    for (node_t *p = evens; p; p = p->next) sum += p->val;
    printf("%ld\n", sum);
    return 0;
}
