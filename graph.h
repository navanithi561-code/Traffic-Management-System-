#ifndef GRAPH_H
#define GRAPH_H
#define MAX_NODES 16
/* Adjacency-list graph. Each road is stored as two directed edges (undirected road). */
typedef struct Edge { int to, dist, traffic; struct Edge *next; } Edge;
typedef struct {
    int n, edge_count;
    char label[MAX_NODES][32];
    Edge *adj[MAX_NODES];
} Graph;
typedef struct { int found, len, path[MAX_NODES], dist; } Route;

void graph_init(Graph *g, int n);
int  graph_add_edge(Graph *g, int u, int v, int dist, int traffic); /* 0 ok, -1 invalid, -2 oom */
void graph_free(Graph *g);
void graph_build_demo(Graph *g);        /* 8 intersections A..H */
int  graph_node(const Graph *g, const char *id); /* "A".."H" -> index, else -1 */
/* Return: 1 found, 0 unreachable, -1 invalid node, -2 out of memory */
int  graph_bfs(const Graph *g, int s, int t, Route *r);      /* fewest edges (unweighted) */
int  graph_dijkstra(const Graph *g, int s, int t, Route *r); /* minimum total distance   */
#endif
