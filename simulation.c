#include "simulation.h"
#include <stdlib.h>
#include <string.h>

static int phase_of(int d) { return d < 2 ? PH_NS : PH_EW; }
static int first_dir(int ph) { return ph == PH_NS ? 0 : 2; }

void sim_init(Sim *s) {
    memset(s, 0, sizeof *s);
    for (int d = 0; d < NDIR; d++) { q_init(&s->normal[d]); pq_init(&s->emerg[d]); }
    s->status = SIM_STOPPED; s->speed = 1; s->phase = PH_NS; s->light = LS_GREEN;
    s->green_len = s->timer = GREEN_MIN; s->next_id = 1;
}
void sim_free(Sim *s) {
    for (int d = 0; d < NDIR; d++) { q_clear(&s->normal[d]); pq_free(&s->emerg[d]); }
}
void sim_reset(Sim *s) { sim_free(s); sim_init(s); }

static int dir_waiting(const Sim *s, int d) { return s->normal[d].size + s->emerg[d].size; }
int sim_waiting_total(const Sim *s) { int n = 0; for (int d = 0; d < NDIR; d++) n += dir_waiting(s, d); return n; }
int sim_emergency_waiting(const Sim *s) { int n = 0; for (int d = 0; d < NDIR; d++) n += s->emerg[d].size; return n; }
static int phase_waiting(const Sim *s, int ph) { int d = first_dir(ph); return dir_waiting(s, d) + dir_waiting(s, d + 1); }
static int phase_emerg(const Sim *s, int ph) { int d = first_dir(ph); return s->emerg[d].size + s->emerg[d + 1].size; }

static void note_queue(Sim *s) {
    for (int d = 0; d < NDIR; d++) if (dir_waiting(s, d) > s->max_queue) s->max_queue = dir_waiting(s, d);
}

int sim_add_vehicle(Sim *s, int type, int dir, int priority, Vehicle *out) {
    if (type < 0 || type >= V_COUNT || dir < 0 || dir >= NDIR) return -1;
    int em = is_emergency((VType)type);
    if (em) { if (priority == 0) priority = default_priority((VType)type); if (priority < 1 || priority > 5) return -1; }
    else priority = 0;
    if (dir_waiting(s, dir) >= MAX_QUEUE) return -2;
    Vehicle *v = calloc(1, sizeof *v);
    if (!v) return -3;
    v->id = s->next_id; v->type = (VType)type; v->dir = dir; v->priority = priority; v->arrival = s->time;
    if (em) { if (pq_push(&s->emerg[dir], v) < 0) { free(v); return -3; } }
    else q_push(&s->normal[dir], v);
    s->next_id++; s->added++; s->added_by_type[type]++;
    note_queue(s);
    if (out) { *out = *v; out->next = NULL; }
    return 0;
}

/* Emergency first (by priority/arrival/id), otherwise FIFO. */
static Vehicle *take_next(Sim *s, int d) {
    Vehicle *v = pq_pop(&s->emerg[d]);
    return v ? v : q_pop(&s->normal[d]);
}

static void finish(Sim *s, Vehicle *v, Vehicle *out) {
    v->passed_at = s->time;
    long w = s->time - v->arrival;
    s->processed++; s->total_wait += w;
    if (is_emergency(v->type)) s->em_processed++;
    s->last[v->dir] = *v; s->last[v->dir].next = NULL; s->has_last[v->dir] = 1;
    if (out) { *out = *v; out->next = NULL; }
    free(v);
}

int sim_signal(const Sim *s, int d) {
    if (phase_of(d) != s->phase) return SIG_RED;
    return s->light == LS_GREEN ? SIG_GREEN : SIG_YELLOW;
}
int sim_conflict_free(const Sim *s) {
    for (int a = 0; a < NDIR; a++) for (int b = a + 1; b < NDIR; b++)
        if (sim_signal(s, a) == SIG_GREEN && sim_signal(s, b) == SIG_GREEN && phase_of(a) != phase_of(b)) return 0;
    return 1;
}

static int best_emergency_phase(const Sim *s) {
    const Vehicle *best = NULL;
    for (int d = 0; d < NDIR; d++) {
        const Vehicle *e = pq_peek(&s->emerg[d]);
        if (e && (!best || veh_before(e, best))) best = e;
    }
    return best ? phase_of(best->dir) : -1;
}

/* Policy: emergency vehicles preempt, but at most EM_STREAK_MAX phases in a row.
   After that the phase rotates to whoever waits, so normal traffic cannot starve. */
static void switch_phase(Sim *s) {
    int cur = s->phase, oth = 1 - cur, next, by_em = 0;
    int pe = best_emergency_phase(s);
    if (pe >= 0 && s->em_streak < EM_STREAK_MAX) { next = pe; by_em = 1; }
    else if (phase_waiting(s, oth) > 0) next = oth;
    else if (phase_waiting(s, cur) > 0) next = cur;
    else next = oth;
    s->em_streak = by_em ? s->em_streak + 1 : 0;
    s->phase = next; s->light = LS_GREEN;
    int len = GREEN_MIN + phase_waiting(s, next);
    s->green_len = s->timer = len > GREEN_MAX ? GREEN_MAX : len;
}

void sim_tick(Sim *s) {
    s->time++;
    if (s->light == LS_GREEN) {
        int d0 = first_dir(s->phase);
        for (int d = d0; d < d0 + 2; d++) { Vehicle *v = take_next(s, d); if (v) finish(s, v, NULL); }
        s->timer--;
        int oth = 1 - s->phase;
        int preempt = phase_emerg(s, oth) > 0 && phase_emerg(s, s->phase) == 0 && s->em_streak < EM_STREAK_MAX;
        int idle = phase_waiting(s, s->phase) == 0 && phase_waiting(s, oth) > 0;
        if (s->timer <= 0 || preempt || idle) { s->light = LS_YELLOW; s->timer = YELLOW_LEN; }
    } else if (--s->timer <= 0) switch_phase(s);
    note_queue(s);
    if (s->time % 5 == 0) {
        if (s->hist_n == HIST_MAX) {
            memmove(s->hist_t, s->hist_t + 1, (HIST_MAX - 1) * sizeof(long));
            memmove(s->hist_p, s->hist_p + 1, (HIST_MAX - 1) * sizeof(long));
            s->hist_n--;
        }
        s->hist_t[s->hist_n] = s->time; s->hist_p[s->hist_n] = s->processed; s->hist_n++;
    }
}

void sim_advance(Sim *s, long long now) {
    if (s->status != SIM_RUNNING) { s->last_ms = now; return; }
    long long el = now - s->last_ms;
    if (el < 0) el = 0;
    if (el > 5000) el = 5000;
    s->carry += (double)el * s->speed / 1000.0;
    int n = (int)s->carry;
    s->carry -= n;
    if (n > 200) n = 200;
    for (int i = 0; i < n; i++) sim_tick(s);
    s->last_ms = now;
}
void sim_start(Sim *s, long long now) { s->status = SIM_RUNNING; s->last_ms = now; }
void sim_pause(Sim *s) { if (s->status == SIM_RUNNING) s->status = SIM_PAUSED; }
int  sim_set_speed(Sim *s, int sp) { if (sp < 1 || sp > 10) return -1; s->speed = sp; return 0; }

int sim_process_next(Sim *s, Vehicle *out) {
    if (s->light != LS_GREEN) return 0;
    int d0 = first_dir(s->phase), best = -1;
    const Vehicle *bv = NULL;
    for (int d = d0; d < d0 + 2; d++) {
        const Vehicle *e = pq_peek(&s->emerg[d]);
        if (e && (!bv || veh_before(e, bv))) { bv = e; best = d; }
    }
    if (best < 0) for (int d = d0; d < d0 + 2; d++) {
        const Vehicle *n = s->normal[d].head;
        if (n && (!bv || n->arrival < bv->arrival || (n->arrival == bv->arrival && n->id < bv->id))) { bv = n; best = d; }
    }
    if (best < 0) return 0;
    finish(s, take_next(s, best), out);
    return 1;
}
