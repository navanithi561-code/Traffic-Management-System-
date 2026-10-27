#include "api.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- growable buffer ---------- */
void buf_init(Buf *b) { b->d = NULL; b->len = b->cap = 0; b->oom = 0; }
void buf_free(Buf *b) { free(b->d); buf_init(b); }
int buf_printf(Buf *b, const char *fmt, ...) {
    if (b->oom) return -1;
    for (;;) {
        size_t room = b->cap - b->len;
        va_list ap; va_start(ap, fmt);
        int n = vsnprintf(b->d ? b->d + b->len : NULL, room, fmt, ap);
        va_end(ap);
        if (n < 0) { b->oom = 1; return -1; }
        if ((size_t)n < room) { b->len += (size_t)n; return 0; }
        size_t nc = b->cap ? b->cap * 2 : 1024;
        while (nc < b->len + (size_t)n + 1) nc *= 2;
        char *p = realloc(b->d, nc);
        if (!p) { b->oom = 1; return -1; }
        b->d = p; b->cap = nc;
    }
}

/* ---------- tiny flat-JSON / query parsing (no external library) ---------- */
static const char *find_val(const char *body, const char *key) {
    char pat[48];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(body, pat);
    if (!p) return NULL;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ':') return NULL;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}
/* 1 ok, 0 missing, -1 invalid */
static int json_str(const char *body, const char *key, char *out, size_t n) {
    const char *p = find_val(body, key);
    if (!p) return 0;
    if (*p != '"') return -1;
    p++;
    size_t i = 0;
    while (*p && *p != '"') { if (i + 1 >= n) return -1; out[i++] = *p++; }
    if (*p != '"') return -1;
    out[i] = 0;
    return 1;
}
static int json_int(const char *body, const char *key, int *out) {
    const char *p = find_val(body, key);
    if (!p) return 0;
    char *e; long v = strtol(p, &e, 10);
    if (e == p || v < -100000 || v > 100000) return -1;
    *out = (int)v;
    return 1;
}
static int qparam(const char *q, const char *key, char *out, size_t n) {
    size_t kl = strlen(key);
    while (*q) {
        if (strncmp(q, key, kl) == 0 && q[kl] == '=') {
            const char *v = q + kl + 1; size_t i = 0;
            while (*v && *v != '&') { if (i + 1 >= n) return 0; out[i++] = *v++; }
            out[i] = 0; return 1;
        }
        const char *amp = strchr(q, '&');
        if (!amp) break;
        q = amp + 1;
    }
    return 0;
}

static int err(Buf *o, int code, const char *msg) {
    buf_printf(o, "{\"ok\":false,\"error\":\"%s\"}", msg);
    return code;
}

/* ---------- emitters ---------- */
static const char *STATUS_NAMES[] = {"stopped", "running", "paused"};
static const char *SIG_NAMES[] = {"red", "yellow", "green"};

static void emit_vehicle(Buf *o, const Vehicle *v, long now) {
    buf_printf(o, "{\"id\":%d,\"type\":\"%s\",\"direction\":\"%s\",\"priority\":%d,\"wait\":%ld}",
               v->id, VTYPE_NAMES[v->type], DIR_NAMES[v->dir], v->priority, now - v->arrival);
}
static int cmp_veh(const void *a, const void *b) {
    const Vehicle *x = *(Vehicle *const *)a, *y = *(Vehicle *const *)b;
    return veh_before(x, y) ? -1 : (veh_before(y, x) ? 1 : 0);
}
static double avg_wait(const Sim *s) { return s->processed ? (double)s->total_wait / (double)s->processed : 0.0; }

static void emit_dashboard(const Sim *s, const Graph *g, Buf *o) {
    buf_printf(o, "\"status\":\"%s\",\"sim_time\":%ld,\"speed\":%d,\"total_vehicles\":%ld,"
               "\"vehicles_processed\":%ld,\"vehicles_waiting\":%d,\"emergency_waiting\":%d,"
               "\"active_intersections\":%d,\"avg_wait\":%.2f",
               STATUS_NAMES[s->status], s->time, s->speed, s->added, s->processed,
               sim_waiting_total(s), sim_emergency_waiting(s),
               s->status == SIM_RUNNING ? g->n : 0, avg_wait(s));
}

static void emit_traffic(const Sim *s, Buf *o) {
    buf_printf(o, "\"status\":\"%s\",\"sim_time\":%ld,\"phase\":\"%s\",\"light\":\"%s\",\"timer\":%d,"
               "\"green_len\":%d,\"conflict_free\":%s,\"directions\":[",
               STATUS_NAMES[s->status], s->time, s->phase == PH_NS ? "north-south" : "east-west",
               s->light == LS_GREEN ? "green" : "yellow", s->timer, s->green_len,
               sim_conflict_free(s) ? "true" : "false");
    for (int d = 0; d < NDIR; d++) {
        Vehicle *tmp[MAX_QUEUE]; int ne = s->emerg[d].size;
        memcpy(tmp, s->emerg[d].a, (size_t)ne * sizeof *tmp);
        qsort(tmp, (size_t)ne, sizeof *tmp, cmp_veh);
        buf_printf(o, "%s{\"name\":\"%s\",\"signal\":\"%s\",\"waiting_normal\":%d,\"waiting_emergency\":%d,\"queue\":[",
                   d ? "," : "", DIR_NAMES[d], SIG_NAMES[sim_signal(s, d)], s->normal[d].size, ne);
        int shown = 0;
        for (int i = 0; i < ne && shown < 10; i++, shown++) { if (shown) buf_printf(o, ","); emit_vehicle(o, tmp[i], s->time); }
        for (const Vehicle *v = s->normal[d].head; v && shown < 10; v = v->next, shown++) {
            if (shown) { buf_printf(o, ","); } emit_vehicle(o, v, s->time);
        }
        buf_printf(o, "],\"passing\":");
        if (s->has_last[d] && s->time - s->last[d].passed_at <= 1) emit_vehicle(o, &s->last[d], s->last[d].passed_at);
        else buf_printf(o, "null");
        buf_printf(o, "}");
    }
    buf_printf(o, "]");
}

static void emit_emergency(const Sim *s, Buf *o) {
    Vehicle *all[NDIR * MAX_QUEUE]; int n = 0;
    for (int d = 0; d < NDIR; d++) { memcpy(all + n, s->emerg[d].a, (size_t)s->emerg[d].size * sizeof *all); n += s->emerg[d].size; }
    qsort(all, (size_t)n, sizeof *all, cmp_veh);
    buf_printf(o, "\"count\":%d,\"next\":", n);
    if (n) emit_vehicle(o, all[0], s->time); else buf_printf(o, "null");
    buf_printf(o, ",\"vehicles\":[");
    for (int i = 0; i < n; i++) { if (i) buf_printf(o, ","); emit_vehicle(o, all[i], s->time); }
    buf_printf(o, "]");
}

static void emit_analytics(const Sim *s, const Graph *g, Buf *o) {
    long normal_done = s->processed - s->em_processed;
    buf_printf(o, "\"sim_time\":%ld,\"total_added\":%ld,\"processed\":%ld,\"emergency_processed\":%ld,"
               "\"normal_processed\":%ld,\"avg_wait\":%.2f,\"max_queue\":%d,\"throughput_per_min\":%.2f,"
               "\"active_intersections\":%d,\"waiting\":{",
               s->time, s->added, s->processed, s->em_processed, normal_done, avg_wait(s), s->max_queue,
               s->time > 0 ? (double)s->processed * 60.0 / (double)s->time : 0.0,
               s->status == SIM_RUNNING ? g->n : 0);
    for (int d = 0; d < NDIR; d++) buf_printf(o, "%s\"%s\":%d", d ? "," : "", DIR_NAMES[d], s->normal[d].size + s->emerg[d].size);
    buf_printf(o, "},\"added_by_type\":{");
    for (int t = 0; t < V_COUNT; t++) buf_printf(o, "%s\"%s\":%ld", t ? "," : "", VTYPE_NAMES[t], s->added_by_type[t]);
    buf_printf(o, "},\"history\":{\"t\":[");
    for (int i = 0; i < s->hist_n; i++) buf_printf(o, "%s%ld", i ? "," : "", s->hist_t[i]);
    buf_printf(o, "],\"processed\":[");
    for (int i = 0; i < s->hist_n; i++) buf_printf(o, "%s%ld", i ? "," : "", s->hist_p[i]);
    buf_printf(o, "]},\"time_note\":\"All times are simulated ticks (1 tick = 1 simulated second at 1x speed), not real-world measurements.\"");
}

static int route_api(const Graph *g, const char *query, int dijkstra, Buf *o) {
    char a[8], b[8];
    if (!qparam(query, "from", a, sizeof a) || !qparam(query, "to", b, sizeof b)) return err(o, 400, "query parameters 'from' and 'to' are required");
    int u = graph_node(g, a), v = graph_node(g, b);
    if (u < 0 || v < 0) return err(o, 400, "invalid intersection id (use A-H)");
    Route r;
    int rc = dijkstra ? graph_dijkstra(g, u, v, &r) : graph_bfs(g, u, v, &r);
    if (rc == -2) return err(o, 500, "out of memory");
    if (rc < 0) return err(o, 400, "invalid intersection id");
    if (rc == 0) return err(o, 404, "destination unreachable");
    buf_printf(o, "{\"ok\":true,\"algorithm\":\"%s\",\"from\":\"%c\",\"to\":\"%c\",\"edges\":%d,\"distance\":%d,\"path\":[",
               dijkstra ? "dijkstra" : "bfs", 'A' + u, 'A' + v, r.len - 1, r.dist);
    for (int i = 0; i < r.len; i++) buf_printf(o, "%s\"%c\"", i ? "," : "", 'A' + r.path[i]);
    buf_printf(o, "],\"names\":[");
    for (int i = 0; i < r.len; i++) buf_printf(o, "%s\"%s\"", i ? "," : "", g->label[r.path[i]]);
    buf_printf(o, "]}");
    return 200;
}

static int add_vehicle_api(Sim *s, const char *body, int emergency_only, Buf *o) {
    char ts[32], ds[16]; int pr = 0, rc;
    if (json_str(body, "type", ts, sizeof ts) != 1) return err(o, 400, "field 'type' is required (string)");
    if (json_str(body, "direction", ds, sizeof ds) != 1) return err(o, 400, "field 'direction' is required (string)");
    int type = parse_vtype(ts), dir = parse_dir(ds);
    if (type < 0) return err(o, 400, "invalid vehicle type");
    if (dir < 0) return err(o, 400, "invalid direction (north/south/east/west)");
    if (emergency_only && !is_emergency((VType)type)) return err(o, 400, "type must be ambulance, fire_engine or police");
    if (json_int(body, "priority", &pr) < 0 || pr < 0 || pr > 5) return err(o, 400, "priority must be an integer 1-5");
    Vehicle v;
    rc = sim_add_vehicle(s, type, dir, pr, &v);
    if (rc == -2) return err(o, 409, "queue for this direction is full");
    if (rc == -3) return err(o, 500, "out of memory");
    if (rc < 0) return err(o, 400, "invalid vehicle");
    buf_printf(o, "{\"ok\":true,\"vehicle\":"); emit_vehicle(o, &v, s->time); buf_printf(o, "}");
    return 201;
}

int api_handle(Sim *s, Graph *g, const char *method, const char *path, const char *query,
               const char *body, long long now, Buf *o) {
    sim_advance(s, now);
    int GET = strcmp(method, "GET") == 0, POST = strcmp(method, "POST") == 0;
    int code = 200;
    if (strcmp(path, "/api/health") == 0) {
        if (!GET) return err(o, 405, "method not allowed");
        buf_printf(o, "{\"ok\":true,\"service\":\"traffic-simulator-c\",\"status\":\"up\"}");
    } else if (strcmp(path, "/api/dashboard") == 0) {
        if (!GET) return err(o, 405, "method not allowed");
        buf_printf(o, "{\"ok\":true,"); emit_dashboard(s, g, o); buf_printf(o, "}");
    } else if (strcmp(path, "/api/traffic") == 0) {
        if (!GET) return err(o, 405, "method not allowed");
        buf_printf(o, "{\"ok\":true,"); emit_traffic(s, o); buf_printf(o, "}");
    } else if (strcmp(path, "/api/network") == 0) {
        if (!GET) return err(o, 405, "method not allowed");
        buf_printf(o, "{\"ok\":true,\"nodes\":[");
        for (int i = 0; i < g->n; i++) buf_printf(o, "%s{\"id\":\"%c\",\"label\":\"%s\"}", i ? "," : "", 'A' + i, g->label[i]);
        buf_printf(o, "],\"edges\":[");
        int first = 1;
        for (int u = 0; u < g->n; u++) for (const Edge *e = g->adj[u]; e; e = e->next) if (u < e->to) {
            buf_printf(o, "%s{\"from\":\"%c\",\"to\":\"%c\",\"distance\":%d,\"traffic\":%d}", first ? "" : ",", 'A' + u, 'A' + e->to, e->dist, e->traffic);
            first = 0;
        }
        buf_printf(o, "]}");
    } else if (strcmp(path, "/api/vehicles") == 0) {
        if (!POST) return err(o, 405, "method not allowed");
        code = add_vehicle_api(s, body, 0, o);
    } else if (strcmp(path, "/api/emergency") == 0) {
        if (GET) { buf_printf(o, "{\"ok\":true,"); emit_emergency(s, o); buf_printf(o, "}"); }
        else if (POST) code = add_vehicle_api(s, body, 1, o);
        else return err(o, 405, "method not allowed");
    } else if (strcmp(path, "/api/simulation/start") == 0) {
        if (!POST) return err(o, 405, "method not allowed");
        sim_start(s, now); buf_printf(o, "{\"ok\":true,\"status\":\"running\"}");
    } else if (strcmp(path, "/api/simulation/pause") == 0) {
        if (!POST) return err(o, 405, "method not allowed");
        if (s->status != SIM_RUNNING) return err(o, 409, "simulation is not running");
        sim_pause(s); buf_printf(o, "{\"ok\":true,\"status\":\"paused\"}");
    } else if (strcmp(path, "/api/simulation/reset") == 0) {
        if (!POST) return err(o, 405, "method not allowed");
        sim_reset(s); buf_printf(o, "{\"ok\":true,\"status\":\"stopped\"}");
    } else if (strcmp(path, "/api/simulation/speed") == 0) {
        if (!POST) return err(o, 405, "method not allowed");
        int sp = 0;
        if (json_int(body, "speed", &sp) != 1 || sim_set_speed(s, sp) < 0) return err(o, 400, "speed must be an integer 1-10");
        buf_printf(o, "{\"ok\":true,\"speed\":%d}", s->speed);
    } else if (strcmp(path, "/api/traffic/process") == 0) {
        if (!POST) return err(o, 405, "method not allowed");
        Vehicle v;
        if (!sim_process_next(s, &v)) return err(o, 409, "no eligible vehicle (signal not green or green queues empty)");
        buf_printf(o, "{\"ok\":true,\"processed\":"); emit_vehicle(o, &v, v.passed_at); buf_printf(o, "}");
    } else if (strcmp(path, "/api/routes/bfs") == 0) {
        if (!GET) return err(o, 405, "method not allowed");
        code = route_api(g, query, 0, o);
    } else if (strcmp(path, "/api/routes/shortest") == 0) {
        if (!GET) return err(o, 405, "method not allowed");
        code = route_api(g, query, 1, o);
    } else if (strcmp(path, "/api/analytics") == 0) {
        if (!GET) return err(o, 405, "method not allowed");
        buf_printf(o, "{\"ok\":true,"); emit_analytics(s, g, o); buf_printf(o, "}");
    } else return err(o, 404, "unknown endpoint");
    if (o->oom) { buf_free(o); buf_init(o); buf_printf(o, "{\"ok\":false,\"error\":\"out of memory\"}"); return 500; }
    return code;
}
