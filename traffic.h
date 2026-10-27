#ifndef TRAFFIC_H
#define TRAFFIC_H
/* Vehicle record. Doubles as a singly-linked-list node (used by Queue). */
typedef enum { V_CAR, V_BIKE, V_BUS, V_TRUCK, V_AMBULANCE, V_FIRE, V_POLICE, V_COUNT } VType;
#define NDIR 4 /* 0=north 1=south 2=east 3=west */

typedef struct Vehicle {
    int id; VType type; int dir;
    int priority;          /* 0 for normal, 1..5 for emergency (5 = highest) */
    long arrival;          /* simulated tick when added */
    long passed_at;        /* simulated tick when it crossed */
    struct Vehicle *next;
} Vehicle;

extern const char *VTYPE_NAMES[V_COUNT];
extern const char *DIR_NAMES[NDIR];
int parse_vtype(const char *s);   /* -1 if invalid */
int parse_dir(const char *s);     /* -1 if invalid */
int is_emergency(VType t);
int default_priority(VType t);
/* Strict total order: higher priority, then earlier arrival, then lower id. */
int veh_before(const Vehicle *a, const Vehicle *b);
#endif
