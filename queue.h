#ifndef QUEUE_H
#define QUEUE_H
#include "traffic.h"
/* FIFO queue built on a singly linked list of Vehicle nodes. O(1) push/pop. */
typedef struct { Vehicle *head, *tail; int size; } Queue;
void     q_init(Queue *q);
void     q_push(Queue *q, Vehicle *v);   /* enqueue at tail */
Vehicle *q_pop(Queue *q);                /* dequeue head, NULL if empty */
int      q_empty(const Queue *q);
void     q_clear(Queue *q);              /* frees every vehicle still queued */
#endif
