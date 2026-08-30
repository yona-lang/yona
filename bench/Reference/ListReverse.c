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
    node_t *rev = NULL;
    for (node_t *p = head; p; ) {
        node_t *next = p->next;
        p->next = rev; rev = p; p = next;
    }
    int count = 0;
    for (node_t *p = rev; p; p = p->next) count++;
    printf("%d\n", count);
    return 0;
}
