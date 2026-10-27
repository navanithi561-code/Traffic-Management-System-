#ifndef API_H
#define API_H
#include <stddef.h>
#include "simulation.h"
#include "graph.h"

/* Growable string buffer used to build JSON responses safely. */
typedef struct { char *d; size_t len, cap; int oom; } Buf;
void buf_init(Buf *b);
void buf_free(Buf *b);
int  buf_printf(Buf *b, const char *fmt, ...);

/* Handles one API call. Returns the HTTP status; JSON body is written to out. */
int api_handle(Sim *s, Graph *g, const char *method, const char *path, const char *query,
               const char *body, long long now_ms, Buf *out);
#endif
