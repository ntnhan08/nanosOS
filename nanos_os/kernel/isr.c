/*
 * NanosOS — kernel/isr.c
 * Interrupt Service Router — dispatches CPU-level interrupts
 * to registered handlers. Manages the Xtensa interrupt matrix.
 *
 * ESP32-S3 has 32 external interrupts per CPU, level 1..7.
 * We use:
 *   Level 1 : Low priority SW interrupts (yield trigger)
 *   Level 3 : USB, I2S DMA, I2C, SPI DMA
 *   Level 5 : Kernel tick (TIMG0 T0) — preemption
 *   Level 7  : NMI (routed to USB OTG)
 */

#include "kernel.h"

/* ── Xtensa interrupt matrix registers ──────────────────── */
#define INTMTX_BASE             0x600C2000UL
/* For each peripheral (0..255), one 32-bit register holds the
 * CPU interrupt number (0..31) to which it is mapped.
 * CPU interrupt number → level is fixed by hardware. */

/* Peripheral source numbers (ESP32-S3 TRM Table 9-1) */
#define PERIPH_TIMG0_T0         10
#define PERIPH_TIMG1_T0         22
#define PERIPH_USB              33
#define PERIPH_I2S0             15
#define PERIPH_UART0            4
#define PERIPH_SPI2             18
#define PERIPH_GDMA_CH0         55
#define PERIPH_GDMA_CH1         56
#define PERIPH_GDMA_CH2         57

/* CPU interrupt number → interrupt level mapping (fixed by hardware) */
/* Levels 0..7; we use SW-defined mapping:
 *   CPU int  1 → Level 1 (low prio SW)
 *   CPU int  3 → Level 3 (peripheral)
 *   CPU int  5 → Level 5 (kernel tick)
 *   CPU int 29 → NMI
 */
#define CPU_INT_YIELD           1
#define CPU_INT_PERIPH          3
#define CPU_INT_TICK            5
#define CPU_INT_NMI             29

#define INTMTX_REG(periph)      (*(volatile uint32_t*)(INTMTX_BASE + 0x154 + (periph)*4))

/* ── Registered ISR table ────────────────────────────────── */
#define MAX_ISR_HANDLERS        32

typedef struct {
    uint32_t    periph_num;
    uint32_t    cpu_int_num;
    void      (*handler)(void *arg);
    void       *arg;
    bool        registered;
} isr_entry_t;

static DRAM_ATTR isr_entry_t s_isrs[MAX_ISR_HANDLERS];
static DRAM_ATTR uint32_t    s_isr_count = 0;

/* ── Interrupt allocation ────────────────────────────────── */
k_err_t isr_register(uint32_t periph_num, uint32_t level,
                      void (*handler)(void *arg), void *arg) {
    if (s_isr_count >= MAX_ISR_HANDLERS) return K_ERR_OVERFLOW;

    /* Choose CPU interrupt number based on requested level */
    uint32_t cpu_int;
    switch (level) {
    case 1:  cpu_int = CPU_INT_YIELD;  break;
    case 3:  cpu_int = CPU_INT_PERIPH; break;
    case 5:  cpu_int = CPU_INT_TICK;   break;
    default: cpu_int = CPU_INT_PERIPH; break;
    }

    /* Write mapping: peripheral → CPU interrupt */
    INTMTX_REG(periph_num) = cpu_int;

    /* Register in table */
    uint32_t ps = irq_save();
    s_isrs[s_isr_count].periph_num  = periph_num;
    s_isrs[s_isr_count].cpu_int_num = cpu_int;
    s_isrs[s_isr_count].handler     = handler;
    s_isrs[s_isr_count].arg         = arg;
    s_isrs[s_isr_count].registered  = true;
    s_isr_count++;
    irq_restore(ps);

    KLOGD("ISR", "periph=%u → CPU_INT=%u level=%u registered",
          periph_num, cpu_int, level);
    return K_OK;
}

void isr_unregister(uint32_t periph_num) {
    uint32_t ps = irq_save();
    INTMTX_REG(periph_num) = 0;
    for (uint32_t i = 0; i < s_isr_count; i++) {
        if (s_isrs[i].periph_num == periph_num) {
            s_isrs[i].registered = false;
            break;
        }
    }
    irq_restore(ps);
}

/* ── Level-3 dispatcher (peripheral ISR router) ─────────── */
/* Called from _level3_isr (assembly stub in startup.S) */
void IRAM_ATTR _level3_dispatch(void) {
    for (uint32_t i = 0; i < s_isr_count; i++) {
        if (!s_isrs[i].registered) continue;
        if (s_isrs[i].cpu_int_num != CPU_INT_PERIPH) continue;
        /* Check if this peripheral raised an interrupt */
        /* Read peripheral-specific status register */
        /* For simplicity: always call (driver checks its own INT_ST) */
        s_isrs[i].handler(s_isrs[i].arg);
    }
}

/* =========================================================
 * mm/dma_manager.c
 * DMA descriptor pool manager
 * Provides pre-allocated, cache-aligned DMA descriptors
 * for SPI, I2S, and USB transfers
 * ========================================================= */
#include "kernel.h"

#define DMA_DESC_POOL_SIZE      64  /* descriptors */
#define DMA_DESC_ALIGN          4   /* 4-byte aligned */

/* DMA descriptor (matches ESP32-S3 GDMA format) */
typedef struct ALIGNED(4) PACKED {
    uint32_t size    :12;
    uint32_t length  :12;
    uint32_t rsvd    :5;
    uint32_t eof     :1;
    uint32_t owner   :1;
    uint32_t rsvd2   :1;
    void    *buf;
    void    *next;
} dma_desc_pool_entry_t;

static DRAM_ATTR dma_desc_pool_entry_t s_dma_desc_pool[DMA_DESC_POOL_SIZE];
static DRAM_ATTR uint32_t              s_dma_desc_used = 0;
static k_mutex_t                       s_dma_pool_lock;

void dma_manager_init(void) {
    memset(s_dma_desc_pool, 0, sizeof(s_dma_desc_pool));
    s_dma_desc_used = 0;
    k_mutex_init(&s_dma_pool_lock, "dma_mgr");
    KLOGI("DMA", "DMA descriptor pool: %d descriptors @ %p",
          DMA_DESC_POOL_SIZE, s_dma_desc_pool);
}

void *dma_desc_alloc(uint32_t count) {
    k_mutex_lock(&s_dma_pool_lock, K_FOREVER);
    if (s_dma_desc_used + count > DMA_DESC_POOL_SIZE) {
        k_mutex_unlock(&s_dma_pool_lock);
        KLOGE("DMA", "DMA descriptor pool exhausted!");
        return NULL;
    }
    void *p = &s_dma_desc_pool[s_dma_desc_used];
    s_dma_desc_used += count;
    k_mutex_unlock(&s_dma_pool_lock);
    return p;
}

void dma_desc_reset(void) {
    k_mutex_lock(&s_dma_pool_lock, K_FOREVER);
    s_dma_desc_used = 0;
    k_mutex_unlock(&s_dma_pool_lock);
}

/* Build a linear DMA descriptor chain for a buffer */
uint32_t dma_build_chain(void *desc_arr, void *data_buf,
                          uint32_t total_bytes, uint32_t max_per_desc) {
    dma_desc_pool_entry_t *descs = (dma_desc_pool_entry_t*)desc_arr;
    uint8_t *ptr = (uint8_t*)data_buf;
    uint32_t remaining = total_bytes;
    uint32_t n = 0;

    while (remaining > 0) {
        uint32_t chunk = (remaining > max_per_desc) ? max_per_desc : remaining;
        descs[n].buf    = ptr;
        descs[n].size   = chunk;
        descs[n].length = chunk;
        descs[n].eof    = (remaining <= max_per_desc) ? 1 : 0;
        descs[n].owner  = 1;  /* DMA owns */
        descs[n].next   = (remaining > chunk) ? &descs[n+1] : NULL;
        ptr       += chunk;
        remaining -= chunk;
        n++;
    }
    return n;  /* number of descriptors used */
}

/* =========================================================
 * drivers/i2s/i2s_driver.h
 * I2S abstraction layer header
 * ========================================================= */
#pragma once
#include "../../kernel/kernel.h"

typedef struct {
    uint32_t sample_rate;    /* e.g. 48000 */
    uint8_t  channels;       /* 1 or 2 */
    uint8_t  bits_per_sample;/* 16 or 32 */
    uint8_t  pin_bclk;
    uint8_t  pin_lrck;
    uint8_t  pin_dout;
    uint8_t  pin_din;
    uint8_t  pin_mclk;
    bool     is_master;
    uint32_t dma_chunk_samples;
    void   (*on_dma_done)(void);  /* called from ISR when buffer consumed */
} i2s_config_t;

k_err_t i2s_init(uint8_t port, const i2s_config_t *cfg);
k_err_t i2s_start(uint8_t port);
k_err_t i2s_stop(uint8_t port);
k_err_t i2s_write_dma(uint8_t port, void *buf, uint32_t len);
uint32_t i2s_get_underruns(uint8_t port);
