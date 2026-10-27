/* Unit tests for queue, priority queue, graph, simulation, statistics and API. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "api.h"

static int checks = 0, fails = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("  FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static Vehicle *mk(int id, int type, int pri, long arr) {
    Vehicle *v = calloc(1, sizeof *v); v->id = id; v->type = (VType)type; v->priority = pri; v->arrival = arr; return v;
}

static void test_queue(void) {
    puts("queue");
    Queue q; q_init(&q);
    CHECK(q_empty(&q)); CHECK(q_pop(&q) == NULL);
    for (int i = 1; i <= 3; i++) q_push(&q, mk(i, V_CAR, 0, i));
    CHECK(q.size == 3);
    for (int i = 1; i <= 3; i++) { Vehicle *v = q_pop(&q); CHECK(v && v->id == i); free(v); }   /* FIFO */
    CHECK(q_empty(&q));
    q_push(&q, mk(9, V_CAR, 0, 0)); q_clear(&q); CHECK(q.size == 0);
}

static void test_pq(void) {
    puts("priority queue");
    PQ p; pq_init(&p);
    CHECK(pq_pop(&p) == NULL && pq_peek(&p) == NULL);
    pq_push(&p, mk(1, V_POLICE, 3, 5)); pq_push(&p, mk(2, V_AMBULANCE, 5, 6)); pq_push(&p, mk(3, V_FIRE, 4, 1));
    int expect[3] = {2, 3, 1};
    for (int i = 0; i < 3; i++) { Vehicle *v = pq_pop(&p); CHECK(v && v->id == expect[i]); free(v); }
    /* tie-break: same priority -> earlier arrival; same arrival -> lower id */
    pq_push(&p, mk(10, V_AMBULANCE, 5, 7)); pq_push(&p, mk(11, V_AMBULANCE, 5, 3));
    pq_push(&p, mk(13, V_AMBULANCE, 5, 3)); pq_push(&p, mk(12, V_AMBULANCE, 5, 3));
    int tie[4] = {11, 12, 13, 10};
    for (int i = 0; i < 4; i++) { Vehicle *v = pq_pop(&p); CHECK(v && v->id == tie[i]); free(v); }
    unsigned x = 12345; int ok = 1;                                   /* 200 pseudo-random inserts, check ordering */
    for (int i = 0; i < 200; i++) { x = x * 1103515245u + 12345u; pq_push(&p, mk(i + 1, V_POLICE, 1 + (int)((x >> 16) % 5), i)); }
    Vehicle *prev = NULL, *v;
    while ((v = pq_pop(&p))) { if (prev && veh_before(v, prev)) ok = 0; free(prev); prev = v; }
    free(prev); CHECK(ok);
    pq_push(&p, mk(99, V_FIRE, 4, 0)); pq_free(&p);                   /* freeing non-empty heap */
}

static void test_graph(void) {
    puts("graph");
    Graph g; graph_build_demo(&g); Route r;
    CHECK(g.n == 8 && g.edge_count == 10);
    CHECK(graph_bfs(&g, 0, 7, &r) == 1 && r.len == 4 && r.dist == 21);        /* A-C-E-H: fewest edges */
    CHECK(graph_dijkstra(&g, 0, 7, &r) == 1 && r.dist == 18 && r.len == 5);   /* A-B-D-G-H: shortest distance */
    CHECK(r.path[0] == 0 && r.path[1] == 1 && r.path[2] == 3 && r.path[3] == 6 && r.path[4] == 7);
    CHECK(graph_dijkstra(&g, 3, 3, &r) == 1 && r.dist == 0 && r.len == 1);
    CHECK(graph_bfs(&g, -1, 2, &r) == -1 && graph_dijkstra(&g, 0, 8, &r) == -1);
    CHECK(graph_node(&g, "h") == 7 && graph_node(&g, "Z") == -1 && graph_node(&g, "AB") == -1 && graph_node(&g, NULL) == -1);
    CHECK(graph_add_edge(&g, 0, 0, 5, 1) == -1 && graph_add_edge(&g, 0, 1, 0, 1) == -1);
    graph_free(&g);
    Graph u; graph_init(&u, 3); graph_add_edge(&u, 0, 1, 2, 1);               /* node 2 isolated */
    CHECK(graph_bfs(&u, 0, 2, &r) == 0 && r.found == 0);
    CHECK(graph_dijkstra(&u, 0, 2, &r) == 0 && r.found == 0);
    graph_free(&u);
}

static void test_sim(void) {
    puts("simulation");
    Sim s; sim_init(&s); Vehicle v;
    CHECK(sim_add_vehicle(&s, -1, 0, 0, &v) == -1 && sim_add_vehicle(&s, V_CAR, 9, 0, &v) == -1);
    CHECK(sim_add_vehicle(&s, V_AMBULANCE, 0, 9, &v) == -1);
    CHECK(sim_add_vehicle(&s, V_CAR, 0, 0, &v) == 0 && s.added == 1);
    CHECK(sim_add_vehicle(&s, V_AMBULANCE, 0, 0, &v) == 0 && v.priority == 5);
    CHECK(sim_process_next(&s, &v) == 1 && v.type == V_AMBULANCE);            /* emergency first */
    CHECK(sim_process_next(&s, &v) == 1 && v.type == V_CAR);                  /* then FIFO normal */
    CHECK(sim_process_next(&s, &v) == 0);                                     /* empty */
    sim_add_vehicle(&s, V_CAR, 2, 0, &v);                                     /* east is red in phase NS */
    CHECK(sim_process_next(&s, &v) == 0);
    sim_reset(&s);
    for (int i = 0; i < MAX_QUEUE; i++) sim_add_vehicle(&s, V_CAR, 1, 0, NULL);
    CHECK(sim_add_vehicle(&s, V_CAR, 1, 0, NULL) == -2);                      /* boundary: queue full */
    sim_free(&s);

    puts("signals / fairness / stats");
    sim_init(&s); unsigned x = 7; int conflict = 0;
    for (int t = 0; t < 2000; t++) {
        x = x * 1103515245u + 12345u;
        if ((x >> 16) % 3 == 0) sim_add_vehicle(&s, (int)((x >> 8) % V_COUNT), (int)((x >> 20) % 4), 0, NULL);
        sim_tick(&s);
        if (!sim_conflict_free(&s)) conflict = 1;
        if (sim_signal(&s, 0) != sim_signal(&s, 1) || sim_signal(&s, 2) != sim_signal(&s, 3)) conflict = 1;
    }
    CHECK(!conflict);
    sim_reset(&s);
    sim_add_vehicle(&s, V_CAR, 2, 0, NULL);                                   /* one normal car waits at east */
    int served = 0;
    for (int t = 0; t < 200 && !served; t++) {
        sim_add_vehicle(&s, V_AMBULANCE, 0, 5, NULL);                         /* endless emergency stream at north */
        sim_tick(&s);
        if (s.normal[2].size == 0) served = 1;
    }
    CHECK(served);                                                            /* no starvation */
    sim_reset(&s);
    sim_add_vehicle(&s, V_CAR, 0, 0, NULL); s.time = 5;
    CHECK(sim_process_next(&s, &v) == 1 && s.total_wait == 5 && s.processed == 1);
    sim_start(&s, 1000); sim_set_speed(&s, 2); sim_advance(&s, 2000);
    CHECK(s.time == 7);                                                       /* 1s real * 2x = 2 ticks */
    CHECK(sim_set_speed(&s, 0) == -1);
    sim_free(&s);
}

static int call(Sim *s, Graph *g, const char *m, const char *p, const char *q, const char *b, Buf *o) {
    buf_init(o); return api_handle(s, g, m, p, q, b, 0, o);
}
static void test_api(void) {
    puts("api");
    Sim s; Graph g; Buf o; sim_init(&s); graph_build_demo(&g);
    CHECK(call(&s, &g, "GET", "/api/health", "", "", &o) == 200); buf_free(&o);
    CHECK(call(&s, &g, "GET", "/api/nope", "", "", &o) == 404); buf_free(&o);
    CHECK(call(&s, &g, "GET", "/api/vehicles", "", "", &o) == 405); buf_free(&o);
    CHECK(call(&s, &g, "POST", "/api/vehicles", "", "{}", &o) == 400); buf_free(&o);
    CHECK(call(&s, &g, "POST", "/api/vehicles", "", "{\"type\":\"tank\",\"direction\":\"north\"}", &o) == 400); buf_free(&o);
    CHECK(call(&s, &g, "POST", "/api/vehicles", "", "{\"type\":\"car\",\"direction\":\"up\"}", &o) == 400); buf_free(&o);
    CHECK(call(&s, &g, "POST", "/api/vehicles", "", "{\"type\":\"car\",\"direction\":\"north\"}", &o) == 201); buf_free(&o);
    CHECK(call(&s, &g, "POST", "/api/emergency", "", "{\"type\":\"car\",\"direction\":\"north\"}", &o) == 400); buf_free(&o);
    CHECK(call(&s, &g, "POST", "/api/emergency", "", "{\"type\":\"police\",\"direction\":\"east\",\"priority\":9}", &o) == 400); buf_free(&o);
    CHECK(call(&s, &g, "POST", "/api/emergency", "", "{\"type\":\"ambulance\",\"direction\":\"east\",\"priority\":4}", &o) == 201); buf_free(&o);
    CHECK(call(&s, &g, "GET", "/api/routes/shortest", "from=A&to=H", "", &o) == 200 && strstr(o.d, "\"distance\":18")); buf_free(&o);
    CHECK(call(&s, &g, "GET", "/api/routes/bfs", "from=a&to=h", "", &o) == 200 && strstr(o.d, "\"edges\":3")); buf_free(&o);
    CHECK(call(&s, &g, "GET", "/api/routes/bfs", "from=Z&to=H", "", &o) == 400); buf_free(&o);
    CHECK(call(&s, &g, "GET", "/api/routes/bfs", "from=A", "", &o) == 400); buf_free(&o);
    CHECK(call(&s, &g, "POST", "/api/traffic/process", "", "", &o) == 200); buf_free(&o);
    CHECK(call(&s, &g, "POST", "/api/traffic/process", "", "", &o) == 409); buf_free(&o);
    CHECK(call(&s, &g, "GET", "/api/analytics", "", "", &o) == 200 && strstr(o.d, "\"processed\":1")); buf_free(&o);
    CHECK(call(&s, &g, "POST", "/api/simulation/speed", "", "{\"speed\":99}", &o) == 400); buf_free(&o);
    CHECK(call(&s, &g, "POST", "/api/simulation/pause", "", "", &o) == 409); buf_free(&o);
    sim_free(&s); graph_free(&g);
}

int main(void) {
    test_queue(); test_pq(); test_graph(); test_sim(); test_api();
    printf("\n%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
