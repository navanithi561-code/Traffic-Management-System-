#include "graph.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

void graph_init(Graph *g, int n) { memset(g, 0, sizeof *g); g->n = n; }

int graph_add_edge(Graph *g, int u, int v, int dist, int traffic) {
    if (u < 0 || v < 0 || u >= g->n || v >= g->n || u == v || dist <= 0) return -1;
    Edge *a = malloc(sizeof *a), *b = malloc(sizeof *b);
    if (!a || !b) { free(a); free(b); return -2; }
    *a = (Edge){v, dist, traffic, g->adj[u]}; g->adj[u] = a;
    *b = (Edge){u, dist, traffic, g->adj[v]}; g->adj[v] = b;
    g->edge_count++;
    return 0;
}

void graph_free(Graph *g) {
    for (int i = 0; i < g->n; i++) {
        Edge *e = g->adj[i];
        while (e) { Edge *nx = e->next; free(e); e = nx; }
        g->adj[i] = NULL;
    }
    g->edge_count = 0;
}

void graph_build_demo(Graph *g) {
    static const char *names[8] = {"Central Station","Market Square","City Hospital","Tech Park",
                                   "Airport Road","University","Harbor","Stadium"};
    graph_init(g, 8);
    for (int i = 0; i < 8; i++) { strncpy(g->label[i], names[i], 31); g->label[i][31] = 0; }
    /* from, to, distance(km*10), simulated traffic cost */
    static const int r[10][4] = {{0,1,4,2},{0,2,2,1},{1,3,5,3},{2,3,8,4},{2,4,10,2},
                                 {3,5,2,1},{4,5,3,2},{3,6,6,3},{6,7,3,1},{4,7,9,5}};
    for (int i = 0; i < 10; i++) graph_add_edge(g, r[i][0], r[i][1], r[i][2], r[i][3]);
}

int graph_node(const Graph *g, const char *id) {
    if (!id || strlen(id) != 1) return -1;
    int i = toupper((unsigned char)id[0]) - 'A';
    return (i >= 0 && i < g->n) ? i : -1;
}

static void build_path(const int *prev, int t, int dist, Route *r) {
    int tmp[MAX_NODES], n = 0;
    for (int v = t; v != -1 && n < MAX_NODES; v = prev[v]) tmp[n++] = v;
    r->len = n;
    for (int i = 0; i < n; i++) r->path[i] = tmp[n - 1 - i];
    r->dist = dist; r->found = 1;
}

int graph_bfs(const Graph *g, int s, int t, Route *r) {
    memset(r, 0, sizeof *r);
    if (s < 0 || t < 0 || s >= g->n || t >= g->n) return -1;
    int prev[MAX_NODES], seen[MAX_NODES] = {0}, dd[MAX_NODES] = {0}, q[MAX_NODES], h = 0, tl = 0;
    for (int i = 0; i < MAX_NODES; i++) prev[i] = -1;
    seen[s] = 1; q[tl++] = s;
    while (h < tl) {
        int u = q[h++];
        if (u == t) break;
        for (Edge *e = g->adj[u]; e; e = e->next)
            if (!seen[e->to]) { seen[e->to] = 1; prev[e->to] = u; dd[e->to] = dd[u] + e->dist; q[tl++] = e->to; }
    }
    if (!seen[t]) return 0;
    build_path(prev, t, dd[t], r);   /* dist = length of the fewest-edge path, NOT minimal */
    return 1;
}

typedef struct { int d, v; } HN;
static int hless(HN a, HN b) { return a.d < b.d || (a.d == b.d && a.v < b.v); }
static void hpush(HN *h, int *n, HN x) {
    int i = (*n)++;
    while (i > 0) { int p = (i - 1) / 2; if (!hless(x, h[p])) break; h[i] = h[p]; i = p; }
    h[i] = x;
}
static HN hpop(HN *h, int *n) {
    HN top = h[0], x = h[--(*n)];
    int i = 0;
    while (1) {
        int c = 2 * i + 1;
        if (c >= *n) break;
        if (c + 1 < *n && hless(h[c + 1], h[c])) c++;
        if (!hless(h[c], x)) break;
        h[i] = h[c]; i = c;
    }
    if (*n > 0) h[i] = x;
    return top;
}

int graph_dijkstra(const Graph *g, int s, int t, Route *r) {
    memset(r, 0, sizeof *r);
    if (s < 0 || t < 0 || s >= g->n || t >= g->n) return -1;
    int dist[MAX_NODES], prev[MAX_NODES];
    for (int i = 0; i < MAX_NODES; i++) { dist[i] = INT_MAX; prev[i] = -1; }
    HN *heap = malloc((size_t)(2 * g->edge_count + 2) * sizeof *heap);
    if (!heap) return -2;
    int hn = 0;
    dist[s] = 0; hpush(heap, &hn, (HN){0, s});
    while (hn > 0) {
        HN cur = hpop(heap, &hn);
        if (cur.d > dist[cur.v]) continue;       /* stale entry */
        if (cur.v == t) break;
        for (Edge *e = g->adj[cur.v]; e; e = e->next) {
            int nd = cur.d + e->dist;
            if (nd < dist[e->to]) { dist[e->to] = nd; prev[e->to] = cur.v; hpush(heap, &hn, (HN){nd, e->to}); }
        }
    }
    free(heap);
    if (dist[t] == INT_MAX) return 0;
    build_path(prev, t, dist[t], r);
    return 1;
}
