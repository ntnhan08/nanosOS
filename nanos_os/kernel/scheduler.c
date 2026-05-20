/* NanosOS — kernel/scheduler.c  Preemptive priority scheduler */
#include "kernel.h"
#define TIMG0_BASE    0x6001F000UL
#define TIMG0_T0_ALARMLO (*(volatile uint32_t*)(TIMG0_BASE+0x10))
#define TIMG0_T0_ALARMHI (*(volatile uint32_t*)(TIMG0_BASE+0x14))
#define TIMG0_T0_CONFIG  (*(volatile uint32_t*)(TIMG0_BASE+0x00))
#define TIMG0_INT_ENA    (*(volatile uint32_t*)(TIMG0_BASE+0x9C))
#define TIMG0_INT_CLR    (*(volatile uint32_t*)(TIMG0_BASE+0xA8))
#define APB_CLK_HZ    80000000UL
#define TICKS_PER_MS  (APB_CLK_HZ/1000UL)
#define TIME_SLICE_TICKS 10U

DRAM_ATTR kernel_state_t g_kernel;
static volatile uint32_t s_prio_bitmap;
static volatile uint32_t s_sched_lock;
static DRAM_ATTR task_tcb_t s_idle_tcb;
static DRAM_ATTR uint8_t    s_idle_stack[KERNEL_STACK_SIZE] ALIGNED(16);

static inline void sched_lock_acquire(void) {
    uint32_t old;
    do { __asm__ volatile("movi %0,1\ns32c1i %0,%1,0\n":"=a"(old):"a"(&s_sched_lock):"memory"); } while(old);
}
static inline void sched_lock_release(void) { __asm__ volatile("memw":::"memory"); s_sched_lock=0; }
static inline int prio_highest(void) { return s_prio_bitmap ? 31-__builtin_clz(s_prio_bitmap) : -1; }

static void IRAM_ATTR rq_enqueue(task_tcb_t *t) {
    int p=t->priority;
    if(!g_kernel.run_queue[p]) { t->next=t; t->prev=t; g_kernel.run_queue[p]=t; }
    else { task_tcb_t *h=g_kernel.run_queue[p],*tail=h->prev;
           tail->next=t; t->prev=tail; t->next=h; h->prev=t; }
    s_prio_bitmap|=BIT(p);
}
static void IRAM_ATTR rq_remove(task_tcb_t *t) {
    int p=t->priority;
    if(t->next==t) { g_kernel.run_queue[p]=NULL; s_prio_bitmap&=~BIT(p); }
    else { t->prev->next=t->next; t->next->prev=t->prev;
           if(g_kernel.run_queue[p]==t) g_kernel.run_queue[p]=t->next; }
    t->next=t->prev=NULL;
}
static void sleep_enqueue(task_tcb_t *t) {
    task_tcb_t **pp=&g_kernel.sleep_queue;
    while(*pp&&(*pp)->wake_tick<=t->wake_tick) pp=&(*pp)->next;
    t->next=*pp; *pp=t;
}

static void idle_task(void *arg) { (void)arg; while(1){ g_kernel.idle_ticks++; __asm__ volatile("waiti 0":::"memory"); } }

void _task_trampoline(void) NAKED;
void _task_trampoline(void) { __asm__ volatile("mov a6,a2\nmov a7,a3\ncallx0 a6\ncall0 task_exit_hook\n"); }
void task_exit_hook(void) { uint32_t ps=irq_save(); g_kernel.current_task->state=TASK_DEAD; irq_restore(ps); task_yield(); while(1)__asm__("waiti 0"); }

void kernel_init(void) {
    memset(&g_kernel,0,sizeof(g_kernel));
    klog_init();
    KLOGI("KERNEL","NanosOS v%d.%d.%d ESP32-S3",NANOS_VERSION_MAJOR,NANOS_VERSION_MINOR,NANOS_VERSION_PATCH);
    mm_heap_init(); mm_psram_init(); mm_slab_init();
    task_create(&s_idle_tcb,"idle",idle_task,NULL,s_idle_stack,sizeof(s_idle_stack),0);
    g_kernel.current_task=&s_idle_tcb; g_kernel.scheduler_running=false;
    /* Timer: TIMG0 1kHz */
    TIMG0_T0_CONFIG=BIT(31)|BIT(30)|BIT(29)|BIT(28);
    TIMG0_T0_ALARMLO=TICKS_PER_MS; TIMG0_T0_ALARMHI=0;
    TIMG0_INT_ENA|=BIT(0);
    KLOGI("KERNEL","Init complete");
}

void NORETURN kernel_start(void) {
    g_kernel.scheduler_running=true;
    __asm__ volatile("movi a2,0x00040000\nwsr a2,PS\nrsync\n":::"a2","memory");
    task_tcb_t *t=&s_idle_tcb; t->state=TASK_RUNNING;
    __asm__ volatile("l32i a1,%0,4\nl32i a0,%0,0\nret\n"::"a"(&t->ctx):"a0","a1");
    __builtin_unreachable();
}

void IRAM_ATTR sched_tick_isr(void) {
    g_kernel.tick_count++; g_kernel.irq_count++;
    uint64_t now=g_kernel.tick_count;
    task_tcb_t *st=g_kernel.sleep_queue;
    while(st&&st->wake_tick<=now) {
        task_tcb_t *nx=st->next; st->state=TASK_READY; st->next=NULL;
        rq_enqueue(st); g_kernel.sleep_queue=nx; st=nx;
    }
    k_timer_t *tmr=g_kernel.timer_list;
    while(tmr){ k_timer_t *nx=tmr->next;
        if(tmr->active&&now>=tmr->expire_tick){ tmr->cb(tmr->arg);
            if(tmr->period_ticks) tmr->expire_tick=now+tmr->period_ticks; else tmr->active=false; }
        tmr=nx; }
    task_tcb_t *cur=g_kernel.current_task;
    if(cur->slice_ticks>0) cur->slice_ticks--;
    bool preempt=(cur->slice_ticks==0);
    if(!preempt){ int hp=prio_highest(); preempt=(hp>(int)cur->priority); }
    if(preempt&&g_kernel.scheduler_running) sched_yield_from_isr();
    TIMG0_T0_ALARMLO+=TICKS_PER_MS; TIMG0_T0_CONFIG|=BIT(28);
}

void IRAM_ATTR sched_yield_from_isr(void) {
    sched_lock_acquire();
    task_tcb_t *old=g_kernel.current_task, *next;
    if(old->state==TASK_RUNNING){ old->state=TASK_READY; rq_enqueue(old); }
    int hp=prio_highest();
    if(hp<0) { next=&s_idle_tcb; } else { next=g_kernel.run_queue[hp]; rq_remove(next); }
    next->state=TASK_RUNNING; next->slice_ticks=TIME_SLICE_TICKS;
    if(next==old){ sched_lock_release(); return; }
    g_kernel.current_task=next; g_kernel.ctx_switches++;
    sched_lock_release();
    context_switch(old,next);
}

k_err_t task_create(task_tcb_t *tcb,const char *name,task_entry_t entry,void *arg,uint8_t *stack,uint32_t sz,uint8_t prio) {
    if(!tcb||!entry||!stack||sz<512||prio>=MAX_PRIORITY_LEVELS) return K_ERR_INVAL;
    memset(tcb,0,sizeof(*tcb));
    strncpy(tcb->name,name,15);
    tcb->priority=tcb->base_priority=prio; tcb->stack_base=stack; tcb->stack_size=sz;
    tcb->stack_guard=0xDEADBEEFU; tcb->slice_ticks=TIME_SLICE_TICKS; tcb->state=TASK_READY;
    uint32_t *sp=(uint32_t*)(stack+sz); sp=(uint32_t*)ALIGN_DOWN((uint32_t)sp,16); sp-=8;
    sp[0]=(uint32_t)entry; sp[1]=(uint32_t)sp; sp[2]=(uint32_t)arg; sp[3]=(uint32_t)task_exit_hook;
    tcb->ctx.pc=(uint32_t)_task_trampoline; tcb->ctx.a1=(uint32_t)sp;
    tcb->ctx.a2=(uint32_t)entry; tcb->ctx.a3=(uint32_t)arg; tcb->ctx.ps=0x00040000;
    uint32_t ps=irq_save();
    tcb->id=g_kernel.task_count++;
    if(tcb->id<MAX_TASKS) g_kernel.tasks[tcb->id]=tcb;
    if(tcb->state==TASK_READY) rq_enqueue(tcb);
    irq_restore(ps);
    return K_OK;
}

void task_yield(void){ uint32_t ps=irq_save(); sched_yield_from_isr(); irq_restore(ps); }
void task_sleep_ms(uint32_t ms){ task_sleep_ticks(ms); }
void task_sleep_ticks(uint32_t ticks){
    if(!ticks){task_yield();return;}
    uint32_t ps=irq_save();
    task_tcb_t *t=g_kernel.current_task;
    t->wake_tick=g_kernel.tick_count+ticks; t->state=TASK_SLEEPING;
    sleep_enqueue(t); irq_restore(ps); task_yield();
}
task_tcb_t *task_current(void){ return g_kernel.current_task; }
uint64_t k_tick_get(void){ return g_kernel.tick_count; }
uint64_t k_time_ms(void){ return g_kernel.tick_count; }

k_err_t k_mutex_init(k_mutex_t *m,const char *n){ atomic_init(&m->locked,0); m->owner=NULL; m->wait_head=NULL; m->recursion=0; strncpy(m->name,n?n:"mtx",11); return K_OK; }
k_err_t k_mutex_lock(k_mutex_t *m,uint32_t tms){
    uint64_t dl=g_kernel.tick_count+tms;
    while(1){ int e=0;
        if(atomic_compare_exchange_strong(&m->locked,&e,1)){ m->owner=g_kernel.current_task; return K_OK; }
        if(m->owner&&m->owner->priority<g_kernel.current_task->priority) m->owner->priority=g_kernel.current_task->priority;
        if(tms==K_NO_WAIT) return K_ERR_BUSY;
        if(tms!=K_FOREVER&&g_kernel.tick_count>=dl) return K_ERR_TIMEOUT;
        uint32_t ps=irq_save();
        g_kernel.current_task->state=TASK_BLOCKED; g_kernel.current_task->wait_obj=m;
        task_tcb_t **pp=&m->wait_head;
        while(*pp&&(*pp)->priority>=g_kernel.current_task->priority) pp=&(*pp)->next;
        g_kernel.current_task->next=*pp; *pp=g_kernel.current_task;
        irq_restore(ps); task_yield();
    }
}
k_err_t k_mutex_unlock(k_mutex_t *m){
    uint32_t ps=irq_save();
    if(m->owner!=g_kernel.current_task){irq_restore(ps);return K_ERR_PERM;}
    m->owner->priority=m->owner->base_priority; m->owner=NULL; atomic_store(&m->locked,0);
    if(m->wait_head){ task_tcb_t *w=m->wait_head; m->wait_head=w->next; w->state=TASK_READY; w->wait_obj=NULL; rq_enqueue(w); }
    irq_restore(ps); task_yield(); return K_OK;
}
k_err_t k_sem_init(k_sem_t *s,int32_t init,int32_t max,const char *n){ atomic_init(&s->count,init); s->max_count=max; s->wait_head=NULL; strncpy(s->name,n?n:"sem",11); return K_OK; }
k_err_t k_sem_take(k_sem_t *s,uint32_t tms){
    uint64_t dl=g_kernel.tick_count+tms;
    while(1){ int c=atomic_load(&s->count);
        if(c>0&&atomic_compare_exchange_strong(&s->count,&c,c-1)) return K_OK;
        if(tms==K_NO_WAIT) return K_ERR_BUSY;
        if(tms!=K_FOREVER&&g_kernel.tick_count>=dl) return K_ERR_TIMEOUT;
        uint32_t ps=irq_save();
        g_kernel.current_task->state=TASK_BLOCKED;
        task_tcb_t **pp=&s->wait_head; while(*pp) pp=&(*pp)->next;
        g_kernel.current_task->next=NULL; *pp=g_kernel.current_task;
        irq_restore(ps); task_yield();
    }
}
k_err_t k_sem_give(k_sem_t *s){
    uint32_t ps=irq_save();
    if(s->wait_head){ task_tcb_t *w=s->wait_head; s->wait_head=w->next; w->state=TASK_READY; rq_enqueue(w); irq_restore(ps); task_yield(); }
    else { int c=atomic_load(&s->count); if(c<s->max_count) atomic_fetch_add(&s->count,1); irq_restore(ps); }
    return K_OK;
}
k_err_t k_msgq_init(k_msgq_t *q,const char *n,void *buf,uint32_t isz,uint32_t cap){
    q->buf=(uint8_t*)buf; q->item_size=isz; q->capacity=cap; q->head=q->tail=0;
    atomic_init(&q->count,0); k_sem_init(&q->not_empty,0,cap,"qne"); k_sem_init(&q->not_full,cap,cap,"qnf");
    strncpy(q->name,n?n:"msgq",15); return K_OK;
}
k_err_t k_msgq_send(k_msgq_t *q,const void *item,uint32_t tms){
    k_err_t r=k_sem_take(&q->not_full,tms); if(r) return r;
    uint32_t ps=irq_save();
    memcpy(q->buf+q->tail*q->item_size,item,q->item_size);
    q->tail=(q->tail+1)%q->capacity; atomic_fetch_add(&q->count,1);
    irq_restore(ps); k_sem_give(&q->not_empty); return K_OK;
}
k_err_t k_msgq_recv(k_msgq_t *q,void *item,uint32_t tms){
    k_err_t r=k_sem_take(&q->not_empty,tms); if(r) return r;
    uint32_t ps=irq_save();
    memcpy(item,q->buf+q->head*q->item_size,q->item_size);
    q->head=(q->head+1)%q->capacity; atomic_fetch_sub(&q->count,1);
    irq_restore(ps); k_sem_give(&q->not_full); return K_OK;
}
k_err_t k_timer_init(k_timer_t *t,const char *n,timer_cb_t cb,void *arg){ t->cb=cb; t->arg=arg; t->active=false; t->next=NULL; strncpy(t->name,n?n:"tmr",11); return K_OK; }
k_err_t k_timer_start(k_timer_t *t,uint32_t ms,bool periodic){
    uint32_t ps=irq_save();
    t->expire_tick=g_kernel.tick_count+ms; t->period_ticks=periodic?ms:0; t->active=true;
    k_timer_t **pp=&g_kernel.timer_list;
    while(*pp&&(*pp)->expire_tick<=t->expire_tick) pp=&(*pp)->next;
    t->next=*pp; *pp=t; irq_restore(ps); return K_OK;
}
void NORETURN kernel_panic(const char *msg,const char *file,int line){
    __asm__ volatile("movi a2,0x0004000F\nwsr a2,PS\nrsync":::"a2");
    volatile uint32_t *u=(volatile uint32_t*)0x60000000;
    for(const char *s="\r\n[PANIC] ";*s;s++) *u=*s;
    for(;*msg;msg++) *u=*msg;
    for(;*file;file++) *u=*file;
    while(1) __asm__("waiti 0");
    (void)line;
}

/* Called at end of kernel_init() before kernel_start() */
void __attribute__((weak)) kernel_app_entry(void) {
    /* Default: do nothing — overridden by system/main.c */
    KLOGW("KERNEL", "kernel_app_entry not implemented!");
}
