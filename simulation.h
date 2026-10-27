#ifndef SIMULATION_H
#define SIMULATION_H
#include "traffic.h"
#include "queue.h"
#include "priority_queue.h"

#define MAX_QUEUE 60      /* max vehicles waiting per direction */
#define GREEN_MIN 4
#define GREEN_MAX 12
#define YELLOW_LEN 2
#define EM_STREAK_MAX 2   /* max consecutive emergency-driven phases before normal traffic is served */
#define HIST_MAX 120

typedef enum { SIM_STOPPED, SIM_RUNNING, SIM_PAUSED } SimStatus;
typedef enum { SIG_RED, SIG_YELLOW, SIG_GREEN } Signal;
typedef enum { PH_NS, PH_EW } Phase;           /* compatible groups: N+S together, E+W together */
typedef enum { LS_GREEN, LS_YELLOW } LightState;

typedef struct {
    SimStatus status; int speed; long time;     /* time = simulated ticks (1 tick = 1 simulated second) */
    Queue normal[NDIR];                         /* FIFO for normal vehicles */
    PQ    emerg[NDIR];                          /* binary heap for emergency vehicles */
    int phase, light, timer, green_len, em_streak, next_id;
    long added, processed, em_processed, total_wait;
    int max_queue;
    long added_by_type[V_COUNT];
    Vehicle last[NDIR]; int has_last[NDIR];
    long hist_t[HIST_MAX], hist_p[HIST_MAX]; int hist_n;
    long long last_ms; double carry;
} Sim;

void sim_init(Sim *s);
void sim_free(Sim *s);
void sim_reset(Sim *s);
/* 0 ok, -1 invalid input, -2 queue full, -3 out of memory. priority 0 = default for type. */
int  sim_add_vehicle(Sim *s, int type, int dir, int priority, Vehicle *out);
void sim_tick(Sim *s);
void sim_advance(Sim *s, long long now_ms);     /* converts wall-clock time into ticks while running */
void sim_start(Sim *s, long long now_ms);
void sim_pause(Sim *s);
int  sim_set_speed(Sim *s, int speed);          /* 1..10 */
int  sim_process_next(Sim *s, Vehicle *out);    /* 1 processed, 0 none eligible */
int  sim_signal(const Sim *s, int dir);
int  sim_conflict_free(const Sim *s);
int  sim_waiting_total(const Sim *s);
int  sim_emergency_waiting(const Sim *s);
#endif
