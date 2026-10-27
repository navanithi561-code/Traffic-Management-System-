#include "traffic.h"
#include <string.h>

const char *VTYPE_NAMES[V_COUNT] = {"car","bike","bus","truck","ambulance","fire_engine","police"};
const char *DIR_NAMES[NDIR] = {"north","south","east","west"};

int parse_vtype(const char *s) {
    if (!s) return -1;
    for (int i = 0; i < V_COUNT; i++) if (strcmp(s, VTYPE_NAMES[i]) == 0) return i;
    return -1;
}
int parse_dir(const char *s) {
    if (!s) return -1;
    for (int i = 0; i < NDIR; i++) if (strcmp(s, DIR_NAMES[i]) == 0) return i;
    return -1;
}
int is_emergency(VType t) { return t == V_AMBULANCE || t == V_FIRE || t == V_POLICE; }
int default_priority(VType t) {
    switch (t) { case V_AMBULANCE: return 5; case V_FIRE: return 4; case V_POLICE: return 3; default: return 0; }
}
int veh_before(const Vehicle *a, const Vehicle *b) {
    if (a->priority != b->priority) return a->priority > b->priority;
    if (a->arrival != b->arrival) return a->arrival < b->arrival;
    return a->id < b->id;
}
