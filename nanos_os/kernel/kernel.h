#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>

/* ── Version ─────────────────────────────────────────── */
#define NANOS_VERSION_MAJOR 1
#define NANOS_VERSION_MINOR 0
#define NANOS_VERSION_PATCH 0

/* ── Hardware constants ──────────────────────────────── */
#define CPU_FREQ_HZ         240000000UL
#define TICK_RATE_HZ        1000U
#define MAX_TASKS           32
#define MAX_PRIORITY_LEVELS 8
#define IDLE_TASK_PRIORITY  0
#define KERNEL_STACK_SIZE   4096
#define DEFAULT_STACK_SIZE  8192
#define ISR_STACK_SIZE      2048

/* Memory map */
#define IRAM_BASE   0x40370000UL
#define IRAM_SIZE   (384*1024)
#define DRAM_BASE   0x3FC88000UL
#define DRAM_SIZE   (512*1024)
#define PSRAM_BASE  0x3C000000UL
#define PSRAM_SIZE  (8*1024*1024)
#define FLASH_BASE  0x42000000UL
#define FLASH_SIZE  (16*1024*1024)
#define DMA_CAPABLE_BASE DRAM_BASE
#define DMA_CAPABLE_END  (DRAM_BASE+DRAM_SIZE)

/* ── Compiler attributes ─────────────────────────────── */
#define IRAM_ATTR    __attribute__((section(".iram.text")))
#define DRAM_ATTR    __attribute__((section(".dram.data")))
#define NORETURN     __attribute__((noreturn))
#define NAKED        __attribute__((naked))
#define PACKED       __attribute__((packed))
#define ALIGNED(n)   __attribute__((aligned(n)))
#define LIKELY(x)    __builtin_expect(!!(x),1)
#define UNLIKELY(x)  __builtin_expect(!!(x),0)
#define BARRIER()    __asm__ volatile(""::: "memory")
#define ARRAY_SIZE(a)(sizeof(a)/sizeof((a)[0]))
#define ALIGN_UP(x,a)   (((x)+(a)-1)&~((a)-1))
#define ALIGN_DOWN(x,a) ((x)&~((a)-1))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define BIT(n)   (1UL<<(n))
#define KERNEL_INTR_LEVEL 5
#define NANOS_ASSERT(c) do{ if(UNLIKELY(!(c))) kernel_panic(#c,__FILE__,__LINE__); }while(0)

/* ── Return codes ────────────────────────────────────── */
typedef int32_t k_err_t;
#define K_OK          0
#define K_ERR_NOMEM  (-1)
#define K_ERR_INVAL  (-2)
#define K_ERR_TIMEOUT(-3)
#define K_ERR_BUSY   (-4)
#define K_ERR_NODEV  (-5)
#define K_ERR_PERM   (-6)
#define K_ERR_OVERFLOW(-7)
#define K_ERR_AGAIN  (-8)
#define K_NO_WAIT    0U
#define K_FOREVER    UINT32_MAX

/* ── CPU context (Xtensa LX7) ────────────────────────── */
typedef struct ALIGNED(16) {
    uint32_t pc,ps,a0,a1,a2,a3,a4,a5,a6,a7;
    uint32_t a8,a9,a10,a11,a12,a13,a14,a15;
    uint32_t sar,lcount,lbeg,lend,excvaddr;
    uint32_t reserved[8];
} cpu_context_t;

/* ── Task states ─────────────────────────────────────── */
typedef enum { TASK_READY=0,TASK_RUNNING,TASK_BLOCKED,
               TASK_SLEEPING,TASK_SUSPENDED,TASK_DEAD } task_state_t;
typedef uint32_t task_id_t;
typedef void (*task_entry_t)(void *arg);

/* ── Task Control Block ──────────────────────────────── */
typedef struct task_tcb {
    cpu_context_t ctx;          /* MUST be first */
    uint8_t      *stack_base;
    uint32_t      stack_size;
    uint32_t      stack_guard;  /* 0xDEADBEEF */
    task_id_t     id;
    char          name[16];
    task_state_t  state;
    uint8_t       priority;
    uint8_t       base_priority;
    uint32_t      flags;
    uint64_t      wake_tick;
    uint32_t      slice_ticks;
    uint32_t      cpu_cycles;
    void         *wait_obj;
    k_err_t       wait_result;
    struct task_tcb *next, *prev;
} task_tcb_t;

/* ── Sync primitives ─────────────────────────────────── */
typedef struct { atomic_int locked; task_tcb_t *owner,*wait_head; uint32_t recursion; char name[12]; } k_mutex_t;
typedef struct { atomic_int count; int32_t max_count; task_tcb_t *wait_head; char name[12]; } k_sem_t;
typedef struct { atomic_uint flags; task_tcb_t *wait_head; } k_event_t;
typedef void (*timer_cb_t)(void *arg);
typedef struct k_timer { uint64_t expire_tick; uint32_t period_ticks; timer_cb_t cb; void *arg; bool active; struct k_timer *next; char name[12]; } k_timer_t;

/* ── Message queue ───────────────────────────────────── */
typedef struct { uint8_t *buf; uint32_t item_size,capacity,head,tail; atomic_uint count; k_sem_t not_empty,not_full; char name[16]; } k_msgq_t;

/* ── Kernel state ────────────────────────────────────── */
typedef struct {
    volatile uint64_t tick_count;
    volatile bool     scheduler_running;
    volatile uint32_t irq_nesting;
    task_tcb_t       *current_task;
    task_tcb_t       *tasks[MAX_TASKS];
    uint32_t          task_count;
    task_tcb_t       *run_queue[MAX_PRIORITY_LEVELS];
    task_tcb_t       *sleep_queue;
    k_timer_t        *timer_list;
    uint32_t          ctx_switches, irq_count;
    uint64_t          idle_ticks;
} kernel_state_t;

extern kernel_state_t g_kernel;

/* ── IRQ helpers ─────────────────────────────────────── */
static inline uint32_t IRAM_ATTR irq_save(void) {
    uint32_t ps;
    __asm__ volatile("rsil %0,%1":  "=a"(ps) : "i"(KERNEL_INTR_LEVEL) : "memory");
    return ps;
}
static inline void IRAM_ATTR irq_restore(uint32_t ps) {
    __asm__ volatile("wsr %0,ps; rsync" :: "a"(ps) : "memory");
}

/* ── Kernel API ──────────────────────────────────────── */
void     kernel_init(void);
void     kernel_start(void) NORETURN;
void     kernel_panic(const char*,const char*,int) NORETURN;
k_err_t  task_create(task_tcb_t*,const char*,task_entry_t,void*,uint8_t*,uint32_t,uint8_t);
void     task_yield(void);
void     task_sleep_ms(uint32_t ms);
void     task_sleep_ticks(uint32_t ticks);
task_tcb_t *task_current(void);
uint64_t k_tick_get(void);
uint64_t k_time_ms(void);
void     IRAM_ATTR sched_tick_isr(void);
void     IRAM_ATTR sched_yield_from_isr(void);
void     context_switch(task_tcb_t*, task_tcb_t*);
k_err_t  k_mutex_init(k_mutex_t*,const char*);
k_err_t  k_mutex_lock(k_mutex_t*,uint32_t);
k_err_t  k_mutex_unlock(k_mutex_t*);
k_err_t  k_sem_init(k_sem_t*,int32_t,int32_t,const char*);
k_err_t  k_sem_take(k_sem_t*,uint32_t);
k_err_t  k_sem_give(k_sem_t*);
k_err_t  k_msgq_init(k_msgq_t*,const char*,void*,uint32_t,uint32_t);
k_err_t  k_msgq_send(k_msgq_t*,const void*,uint32_t);
k_err_t  k_msgq_recv(k_msgq_t*,void*,uint32_t);
k_err_t  k_timer_init(k_timer_t*,const char*,timer_cb_t,void*);
k_err_t  k_timer_start(k_timer_t*,uint32_t,bool);
void    *k_malloc(size_t); void *k_calloc(size_t,size_t); void k_free(void*);
void    *k_dma_alloc(size_t); void k_dma_free(void*);
void    *k_psram_alloc(size_t); void k_psram_free(void*);
void    *k_slab_alloc(uint32_t); void k_slab_free(uint32_t,void*);
void     mm_stats_print(void);
typedef enum {LOG_NONE=0,LOG_ERROR,LOG_WARN,LOG_INFO,LOG_DEBUG,LOG_VERBOSE} log_level_t;
void klog_init(void);
void klog(log_level_t,const char*,const char*,...) __attribute__((format(printf,3,4)));
#define KLOGE(t,...) klog(LOG_ERROR,  t,__VA_ARGS__)
#define KLOGW(t,...) klog(LOG_WARN,   t,__VA_ARGS__)
#define KLOGI(t,...) klog(LOG_INFO,   t,__VA_ARGS__)
#define KLOGD(t,...) klog(LOG_DEBUG,  t,__VA_ARGS__)
/* Forward decls from other modules */
void mm_heap_init(void); void mm_psram_init(void); void mm_slab_init(void);
void hal_init(void); void hal_wdt_kick(void);
void audio_stats_print(void); void nfs_stats_print(void); void aap_stats_print(void);
bool aap_is_running(void);
uint32_t gui_get_fps(void);
#define SLAB_TCB 0
#define SLAB_WIDGET 1
#define SLAB_MSG 2
#define SLAB_SMALL 3
