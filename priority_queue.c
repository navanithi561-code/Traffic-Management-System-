#include "priority_queue.h"
#include <stdlib.h>

void pq_init(PQ *p) { p->a = NULL; p->size = p->cap = 0; }

int pq_push(PQ *p, Vehicle *v) {
    if (p->size == p->cap) {
        int nc = p->cap ? p->cap * 2 : 8;
        Vehicle **na = realloc(p->a, (size_t)nc * sizeof *na);
        if (!na) return -1;
        p->a = na; p->cap = nc;
    }
    int i = p->size++;
    while (i > 0) {                     /* sift up */
        int parent = (i - 1) / 2;
        if (!veh_before(v, p->a[parent])) break;
        p->a[i] = p->a[parent]; i = parent;
    }
    p->a[i] = v;
    return 0;
}

Vehicle *pq_pop(PQ *p) {
    if (p->size == 0) return NULL;
    Vehicle *top = p->a[0];
    Vehicle *x = p->a[--p->size];
    int i = 0;
    while (1) {                         /* sift down */
        int c = 2 * i + 1;
        if (c >= p->size) break;
        if (c + 1 < p->size && veh_before(p->a[c + 1], p->a[c])) c++;
        if (!veh_before(p->a[c], x)) break;
        p->a[i] = p->a[c]; i = c;
    }
    if (p->size > 0) p->a[i] = x;
    return top;
}

Vehicle *pq_peek(const PQ *p) { return p->size ? p->a[0] : NULL; }

void pq_free(PQ *p) {
    for (int i = 0; i < p->size; i++) free(p->a[i]);
    free(p->a); p->a = NULL; p->size = p->cap = 0;
}
