#ifndef PRIORITY_QUEUE_H
#define PRIORITY_QUEUE_H
#include "traffic.h"
/* Binary max-heap ordered by veh_before(): priority desc, arrival asc, id asc. */
typedef struct { Vehicle **a; int size, cap; } PQ;
void     pq_init(PQ *p);
int      pq_push(PQ *p, Vehicle *v);  /* O(log n); 0 ok, -1 out of memory */
Vehicle *pq_pop(PQ *p);               /* O(log n); NULL if empty */
Vehicle *pq_peek(const PQ *p);        /* O(1) */
void     pq_free(PQ *p);              /* frees remaining vehicles + array */
#endif
