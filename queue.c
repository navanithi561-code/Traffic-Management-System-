#include "queue.h"
#include <stdlib.h>

void q_init(Queue *q) { q->head = q->tail = NULL; q->size = 0; }
void q_push(Queue *q, Vehicle *v) {
    v->next = NULL;
    if (q->tail) q->tail->next = v; else q->head = v;
    q->tail = v; q->size++;
}
Vehicle *q_pop(Queue *q) {
    Vehicle *v = q->head;
    if (!v) return NULL;
    q->head = v->next;
    if (!q->head) q->tail = NULL;
    v->next = NULL; q->size--;
    return v;
}
int q_empty(const Queue *q) { return q->size == 0; }
void q_clear(Queue *q) { Vehicle *v; while ((v = q_pop(q))) free(v); }
