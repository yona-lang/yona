#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
typedef struct node { int64_t val; struct node *next; } node_t;
int main(void) {
    node_t *head = NULL;
    for (int i = 10000; i >= 1; i--) {
        node_t *n = malloc(sizeof(node_t));
        n->val = i; n->next = head; head = n;
    }
    int64_t sum = 0;
    for (node_t *p = head; p; p = p->next) sum += p->val;
    printf("%ld\n", sum);
    return 0;
}
