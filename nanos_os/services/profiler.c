/* NanosOS services/profiler.c - CPU cycle-accurate performance profiler */
#include "../kernel/kernel.h"
#define MAX_SLOTS 24
typedef struct {
    const char *tag; uint64_t total; uint32_t cnt, min, max, t0; bool active;
} pslot_t;
static DRAM_ATTR pslot_t s_sl[MAX_SLOTS];
static k_mutex_t s_lk;
static inline uint32_t ccount(void) {
    uint32_t c; __asm__ volatile("rsr %0,CCOUNT":"=a"(c)); return c; }
void profiler_init(void) {
    memset(s_sl,0,sizeof(s_sl));
    for(int i=0;i<MAX_SLOTS;i++) s_sl[i].min=UINT32_MAX;
    k_mutex_init(&s_lk,"prof"); KLOGI("PROF","Profiler ready (%d slots @ 240MHz)",MAX_SLOTS); }
uint32_t profiler_begin(const char *tag) {
    k_mutex_lock(&s_lk,K_FOREVER);
    int slot=-1;
    for(int i=0;i<MAX_SLOTS;i++) {
        if(!s_sl[i].tag){if(slot<0)slot=i;continue;}
        if(s_sl[i].tag==tag||!strcmp(s_sl[i].tag,tag)){slot=i;break;}}
    if(slot<0){k_mutex_unlock(&s_lk);return UINT32_MAX;}
    s_sl[slot].tag=tag; s_sl[slot].active=true; s_sl[slot].t0=ccount();
    k_mutex_unlock(&s_lk); return (uint32_t)slot; }
void profiler_end(uint32_t h) {
    uint32_t te=ccount(); if(h>=MAX_SLOTS)return;
    k_mutex_lock(&s_lk,K_FOREVER);
    pslot_t *s=&s_sl[h]; if(!s->active){k_mutex_unlock(&s_lk);return;}
    uint32_t e=te-s->t0; s->total+=e; s->cnt++;
    if(e<s->min)s->min=e; if(e>s->max)s->max=e; s->active=false;
    k_mutex_unlock(&s_lk); }
void profiler_print_report(void) {
    KLOGI("PROF","═══ Performance Report (1 cycle=4.17ns @ 240MHz) ═══");
    KLOGI("PROF","%-20s %7s %8s %8s %8s","Tag","Calls","Avg(us)","Min(us)","Max(us)");
    for(int i=0;i<MAX_SLOTS;i++) {
        pslot_t *s=&s_sl[i]; if(!s->tag||!s->cnt) continue;
        uint32_t avg=(uint32_t)((s->total/s->cnt)/240);
        KLOGI("PROF","%-20s %7u %8u %8u %8u",s->tag,s->cnt,avg,s->min/240,s->max/240); } }
