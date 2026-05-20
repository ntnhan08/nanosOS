/*
 * NanosOS — services/profiler.c
 * Lightweight CPU cycle-accurate profiler
 * Uses Xtensa CCOUNT register (increments every CPU cycle @ 240MHz)
 */
#include "../kernel/kernel.h"

#define MAX_PROF_SLOTS  24

typedef struct {
    const char *tag;
    uint64_t    total_cycles;
    uint32_t    call_count;
    uint32_t    min_cycles;
    uint32_t    max_cycles;
    uint32_t    t_start;
    bool        active;
} prof_slot_t;

static DRAM_ATTR prof_slot_t s_slots[MAX_PROF_SLOTS];
static k_mutex_t             s_prof_lock;

static inline uint32_t ccount(void) {
    uint32_t c;
    __asm__ volatile("rsr %0, CCOUNT" : "=a"(c));
    return c;
}

void profiler_init(void) {
    memset(s_slots, 0, sizeof(s_slots));
    for (int i = 0; i < MAX_PROF_SLOTS; i++)
        s_slots[i].min_cycles = UINT32_MAX;
    k_mutex_init(&s_prof_lock, "profiler");
    KLOGI("PROF", "Profiler ready (%d slots, CCOUNT @ 240MHz)", MAX_PROF_SLOTS);
}

uint32_t profiler_begin(const char *tag) {
    k_mutex_lock(&s_prof_lock, K_FOREVER);
    /* Find or allocate slot for this tag */
    int slot = -1;
    for (int i = 0; i < MAX_PROF_SLOTS; i++) {
        if (s_slots[i].tag == NULL) { if (slot < 0) slot = i; continue; }
        if (s_slots[i].tag == tag || strcmp(s_slots[i].tag, tag) == 0) {
            slot = i; break;
        }
    }
    if (slot < 0) { k_mutex_unlock(&s_prof_lock); return UINT32_MAX; }
    s_slots[slot].tag    = tag;
    s_slots[slot].active = true;
    s_slots[slot].t_start = ccount();
    k_mutex_unlock(&s_prof_lock);
    return (uint32_t)slot;
}

void profiler_end(uint32_t handle) {
    uint32_t t_end = ccount();
    if (handle >= MAX_PROF_SLOTS) return;
    k_mutex_lock(&s_prof_lock, K_FOREVER);
    prof_slot_t *s = &s_slots[handle];
    if (!s->active) { k_mutex_unlock(&s_prof_lock); return; }
    uint32_t elapsed = t_end - s->t_start;  /* wraps correctly */
    s->total_cycles += elapsed;
    s->call_count++;
    if (elapsed < s->min_cycles) s->min_cycles = elapsed;
    if (elapsed > s->max_cycles) s->max_cycles = elapsed;
    s->active = false;
    k_mutex_unlock(&s_prof_lock);
}

void profiler_print_report(void) {
    KLOGI("PROF", "=== Profiler Report (240MHz = 4.17ns/cycle) ===");
    KLOGI("PROF", "%-20s %8s %8s %8s %8s", "Tag","Calls","Avg(us)","Min(us)","Max(us)");
    for (int i = 0; i < MAX_PROF_SLOTS; i++) {
        prof_slot_t *s = &s_slots[i];
        if (!s->tag || s->call_count == 0) continue;
        uint32_t avg_us = (uint32_t)((s->total_cycles / s->call_count) / 240);
        uint32_t min_us = s->min_cycles / 240;
        uint32_t max_us = s->max_cycles / 240;
        KLOGI("PROF", "%-20s %8u %8u %8u %8u",
              s->tag, s->call_count, avg_us, min_us, max_us);
    }
}

/* =========================================================
 * services/bluetooth_audio.c
 * Bluetooth A2DP audio sink stub
 * Full implementation requires BT stack (Phase 2)
 * ========================================================= */
#include "../kernel/kernel.h"

static bool s_bt_connected = false;
static int  s_bt_stream_id = -1;

void bt_audio_init(void) {
    /* Placeholder: BT controller init via ROM BLE stack */
    /* Full A2DP requires:
     *   1. HCI init (UART2 or USB)
     *   2. L2CAP channel manager
     *   3. AVDTP signaling
     *   4. A2DP source/sink negotiation
     *   5. SBC decoder → audio_stream_open()
     */
    KLOGI("BT", "Bluetooth audio stub initialized (full stack: Phase 2)");
}

bool bt_audio_connected(void) { return s_bt_connected; }

void bt_audio_on_pcm(const int16_t *pcm, uint32_t samples, uint32_t src_rate) {
    if (!s_bt_connected) return;
    if (s_bt_stream_id < 0) {
        s_bt_stream_id = audio_stream_open(pcm, samples, src_rate, 220, false);
    }
}

/* =========================================================
 * services/wifi_stack.c
 * Lightweight Wi-Fi status stack (ESP32-S3 built-in)
 * Phase 2: OTA updates + time sync
 * ========================================================= */
#include "../kernel/kernel.h"

/* Wi-Fi uses Espressif's binary blobs for PHY/MAC.
 * We interact via ROM Wi-Fi API calls (minimal footprint).
 * This stub provides the interface layer. */

static bool s_wifi_connected = false;
static char s_wifi_ssid[33] = {0};
static char s_wifi_ip[16]   = {0};

void wifi_init(void) {
    /* ROM: esp_wifi_init, esp_wifi_set_mode, esp_wifi_start */
    KLOGI("WIFI", "Wi-Fi stack stub (OTA/NTP: Phase 2)");
}

bool wifi_connect(const char *ssid, const char *password) {
    strncpy(s_wifi_ssid, ssid, 32);
    (void)password;
    /* ROM call would go here */
    KLOGI("WIFI", "Connecting to '%s'...", ssid);
    return false; /* stub */
}

bool wifi_connected(void)    { return s_wifi_connected; }
const char *wifi_ssid(void)  { return s_wifi_ssid; }
const char *wifi_ip(void)    { return s_wifi_ip; }

/* =========================================================
 * fs/asset_manager.c
 * Asset manager — loads binary resources from flash partition
 * Assets partition: 6MB @ 0xA00000 (fonts, icons, sounds)
 * =========================================================*/
#include "../kernel/kernel.h"
#include "nfs.h"

#define ASSET_PARTITION_OFFSET  0xA00000UL
#define ASSET_MAGIC             0x41535354UL  /* 'ASST' */
#define ASSET_MAX_ENTRIES       256

typedef struct PACKED {
    uint32_t magic;
    uint32_t count;
    uint32_t version;
    uint32_t checksum;
} asset_header_t;

typedef struct PACKED {
    char     name[48];
    uint32_t offset;      /* from partition base */
    uint32_t size;
    uint32_t compressed;  /* 0=raw, 1=zlib */
    uint32_t crc32;
} asset_entry_t;

static asset_entry_t s_asset_table[ASSET_MAX_ENTRIES];
static uint32_t      s_asset_count = 0;

/* LRU cache: 4 assets in PSRAM */
#define ASSET_CACHE_SLOTS 4

typedef struct {
    uint32_t  asset_idx;
    uint8_t  *data;
    uint32_t  size;
    uint64_t  last_used;
    bool      valid;
} asset_cache_slot_t;

static asset_cache_slot_t s_cache[ASSET_CACHE_SLOTS];
static k_mutex_t          s_asset_lock;

extern k_err_t flash_read(uint32_t addr, void *buf, uint32_t len);

void asset_manager_init(void) {
    k_mutex_init(&s_asset_lock, "assets");
    memset(s_cache, 0, sizeof(s_cache));

    /* Read asset table from flash */
    asset_header_t hdr;
    flash_read(ASSET_PARTITION_OFFSET, &hdr, sizeof(hdr));
    if (hdr.magic != ASSET_MAGIC) {
        KLOGW("ASSET", "No asset partition found (magic=%08X)", hdr.magic);
        return;
    }
    s_asset_count = MIN(hdr.count, ASSET_MAX_ENTRIES);
    flash_read(ASSET_PARTITION_OFFSET + sizeof(hdr),
               s_asset_table, s_asset_count * sizeof(asset_entry_t));
    KLOGI("ASSET", "Asset manager: %u assets loaded from flash", s_asset_count);
}

/* Find asset index by name */
static int32_t asset_find(const char *name) {
    for (uint32_t i = 0; i < s_asset_count; i++) {
        if (strncmp(s_asset_table[i].name, name, 48) == 0)
            return (int32_t)i;
    }
    return -1;
}

/* Load asset into PSRAM cache */
const uint8_t *asset_load(const char *name, uint32_t *out_size) {
    k_mutex_lock(&s_asset_lock, K_FOREVER);

    int32_t idx = asset_find(name);
    if (idx < 0) {
        KLOGW("ASSET", "Asset '%s' not found", name);
        k_mutex_unlock(&s_asset_lock);
        return NULL;
    }

    /* Check cache */
    for (int i = 0; i < ASSET_CACHE_SLOTS; i++) {
        if (s_cache[i].valid && s_cache[i].asset_idx == (uint32_t)idx) {
            s_cache[i].last_used = k_time_ms();
            if (out_size) *out_size = s_cache[i].size;
            k_mutex_unlock(&s_asset_lock);
            return s_cache[i].data;
        }
    }

    /* Evict LRU slot */
    int evict = 0;
    uint64_t oldest = UINT64_MAX;
    for (int i = 0; i < ASSET_CACHE_SLOTS; i++) {
        if (!s_cache[i].valid) { evict = i; break; }
        if (s_cache[i].last_used < oldest) {
            oldest = s_cache[i].last_used;
            evict  = i;
        }
    }
    if (s_cache[evict].data) {
        k_psram_free(s_cache[evict].data);
        s_cache[evict].data  = NULL;
        s_cache[evict].valid = false;
    }

    /* Load from flash into PSRAM */
    asset_entry_t *e = &s_asset_table[idx];
    uint8_t *buf = (uint8_t*)k_psram_alloc(e->size);
    if (!buf) {
        k_mutex_unlock(&s_asset_lock);
        return NULL;
    }
    flash_read(ASSET_PARTITION_OFFSET + e->offset, buf, e->size);

    s_cache[evict].asset_idx = (uint32_t)idx;
    s_cache[evict].data      = buf;
    s_cache[evict].size      = e->size;
    s_cache[evict].last_used = k_time_ms();
    s_cache[evict].valid     = true;

    if (out_size) *out_size = e->size;
    k_mutex_unlock(&s_asset_lock);
    KLOGI("ASSET", "Loaded '%s' (%uKB) from flash", name, e->size >> 10);
    return buf;
}

void asset_evict(const char *name) {
    k_mutex_lock(&s_asset_lock, K_FOREVER);
    int32_t idx = asset_find(name);
    if (idx >= 0) {
        for (int i = 0; i < ASSET_CACHE_SLOTS; i++) {
            if (s_cache[i].valid && s_cache[i].asset_idx == (uint32_t)idx) {
                k_psram_free(s_cache[i].data);
                s_cache[i].valid = false;
                s_cache[i].data  = NULL;
                break;
            }
        }
    }
    k_mutex_unlock(&s_asset_lock);
}
