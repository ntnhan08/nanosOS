/*
 * NanosOS — system/main.c
 * Boot orchestration — sử dụng Auto-Detection Framework
 */
#include "../kernel/kernel.h"
#include "../gui/gui.h"
#include "../audio/audio.h"
#include "../usb/usb_stack.h"
#include "../usb/aap_transport.h"
#include "../fs/nfs.h"
#include "../system/devtree/device_tree.h"

static const char *TAG = "MAIN";
int g_aap_audio_stream = -1;

/* External functions */
extern void drivers_autodetect_init(void);
extern void show_splash_screen(void);
extern void asset_manager_init(void);
extern void event_loop_init(void);
extern void event_loop_tick(void);
extern void debug_shell_init(void);
extern void power_manager_init(void);

/* ── AAP callbacks ────────────────────────────────────── */
static void on_aap_conn(void) {
    KLOGI(TAG, "Android Auto CONNECTED");
    gui_widget_t *root = gui_get_root();
    if (root) {
        /* Animate UI into AA mode */
        gui_animate(root, 0, 0, 255, 300);
    }
}

static void on_aap_disc(void) {
    KLOGI(TAG, "Android Auto DISCONNECTED");
    if (g_aap_audio_stream >= 0) {
        audio_stream_close(g_aap_audio_stream);
        g_aap_audio_stream = -1;
    }
}

static void on_video(const uint8_t *d, uint32_t l, uint64_t pts) {
    (void)d; (void)pts;
    if (l > 0) KLOGD(TAG, "Video frame %uB", l);
    /* TODO Phase 3: H.264 decode → framebuffer */
}

static void on_audio(const uint8_t *pcm, uint32_t samples, uint64_t pts) {
    (void)pts;
    if (g_aap_audio_stream < 0) {
        g_aap_audio_stream = audio_stream_open(
            (const int16_t*)pcm, samples, 44100, 200, false);
        KLOGI(TAG, "AAP audio stream opened: id=%d", g_aap_audio_stream);
    }
}

static void on_nav(uint32_t ev, const uint8_t *p, uint32_t l) {
    (void)p; (void)l;
    KLOGI(TAG, "Nav event=%u", ev);
    /* TODO: update nav overlay widget */
}

/* ── System info task ─────────────────────────────────── */
static task_tcb_t s_info_task;
static uint8_t    s_info_stk[2048] ALIGNED(16);

static void sysinfo_task(void *arg) {
    (void)arg;
    while (1) {
        task_sleep_ms(10000);
        KLOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        KLOGI(TAG, "Uptime: %llums  FPS: %u  AAP: %s",
              (unsigned long long)k_time_ms(),
              gui_get_fps(),
              aap_is_running() ? "STREAMING" : "waiting");
        mm_stats_print();
        audio_stats_print();
        nfs_stats_print();
        aap_stats_print();
        devtree_print();
    }
}

/* ── Build UI after autodetect ────────────────────────── */
static void build_main_ui(void) {
    gui_widget_t *root = gui_get_root();
    if (!root) return;

    /* Get detected LCD resolution */
    lcd_caps_t caps = {480, 800, 16, false, true, 40000000, 0};
    hal_lcd_get_caps(&caps);

    /* Background */
    root->bg_color = RGB565(10, 12, 22);

    /* Status bar */
    gui_widget_t *bar = gui_widget_create(WT_PANEL, 0, 0, (int16_t)caps.width, 40);
    bar->bg_color = RGB565(20, 28, 45);
    gui_widget_add_child(root, bar);

    /* Device info label */
    gui_widget_t *dev_info = gui_widget_create(WT_LABEL, 8, 10, (int16_t)(caps.width - 16), 22);
    dev_info->bg_color = bar->bg_color;
    dev_info->fg_color = RGB565(80, 90, 120);
    /* Show detected hardware */
    char info[64];
    snprintf(info, sizeof(info), "NanosOS | %s | %s | %s",
             g_devtree.primary_lcd   ? g_devtree.primary_lcd->name   : "?",
             g_devtree.primary_touch ? g_devtree.primary_touch->name : "?",
             g_devtree.primary_audio_out ? g_devtree.primary_audio_out->name : "?");
    strncpy(dev_info->text, info, sizeof(dev_info->text) - 1);
    gui_widget_add_child(bar, dev_info);

    /* Main content area */
    gui_widget_t *main_card = gui_widget_create(WT_PANEL,
        20, 60, (int16_t)(caps.width - 40), (int16_t)(caps.height - 180));
    main_card->bg_color = RGB565(18, 24, 38);
    main_card->corner_radius = 16;
    gui_widget_add_child(root, main_card);

    /* Connect prompt */
    gui_widget_t *prompt = gui_widget_create(WT_LABEL,
        40, (int16_t)(caps.height/2 - 60), (int16_t)(caps.width - 80), 40);
    prompt->bg_color = main_card->bg_color;
    prompt->fg_color = RGB565(32, 151, 212);
    strncpy(prompt->text, "Connect phone via USB", sizeof(prompt->text) - 1);
    gui_widget_add_child(main_card, prompt);

    /* Detected hardware list */
    const char *rows[] = {
        g_devtree.primary_lcd        ? g_devtree.primary_lcd->name        : "",
        g_devtree.primary_touch      ? g_devtree.primary_touch->name      : "",
        g_devtree.primary_audio_out  ? g_devtree.primary_audio_out->name  : "",
    };
    for (int i = 0; i < 3; i++) {
        if (!rows[i][0]) continue;
        gui_widget_t *hw = gui_widget_create(WT_LABEL,
            40, (int16_t)(caps.height/2 + 20 + i*28),
            (int16_t)(caps.width - 80), 24);
        hw->bg_color = main_card->bg_color;
        hw->fg_color = RGB565(60, 200, 100);
        strncpy(hw->text, rows[i], sizeof(hw->text) - 1);
        gui_widget_add_child(main_card, hw);
    }

    /* Bottom bar */
    gui_widget_t *bot = gui_widget_create(WT_PANEL,
        0, (int16_t)(caps.height - 80), (int16_t)caps.width, 80);
    bot->bg_color = RGB565(20, 28, 45);
    gui_widget_add_child(root, bot);

    gui_widget_t *ver = gui_widget_create(WT_LABEL,
        8, (int16_t)(caps.height - 56), (int16_t)(caps.width/2), 20);
    ver->bg_color = bot->bg_color;
    ver->fg_color = RGB565(80, 90, 120);
    snprintf(ver->text, sizeof(ver->text), "NanosOS v%d.%d.%d",
             NANOS_VERSION_MAJOR, NANOS_VERSION_MINOR, NANOS_VERSION_PATCH);
    gui_widget_add_child(bot, ver);
}

/* ── App main task ────────────────────────────────────── */
static task_tcb_t s_app_task;
static uint8_t    s_app_stk[32768] ALIGNED(16);

static void app_main_task(void *arg) {
    (void)arg;
    uint64_t t0 = k_time_ms();

    KLOGI(TAG, "NanosOS v%d.%d.%d on ESP32-S3 R8P16",
          NANOS_VERSION_MAJOR, NANOS_VERSION_MINOR, NANOS_VERSION_PATCH);
    KLOGI(TAG, "CPU: 240MHz | PSRAM: 8MB OPI | Flash: 16MB SPI");

    /* HAL init first */
    hal_init();
    KLOGI(TAG, "[%4llums] HAL ready", k_time_ms() - t0);

    /* AUTO-DETECTION: LCD + Touch + Audio + Sensors */
    KLOGI(TAG, "[%4llums] Starting hardware auto-detection...", k_time_ms() - t0);
    drivers_autodetect_init();
    KLOGI(TAG, "[%4llums] Auto-detection complete", k_time_ms() - t0);

    /* Show splash using detected LCD */
    show_splash_screen();
    KLOGI(TAG, "[%4llums] Splash shown", k_time_ms() - t0);

    /* Audio pipeline (uses detected codec) */
    audio_init();
    KLOGI(TAG, "[%4llums] Audio ready", k_time_ms() - t0);

    /* USB host stack */
    usb_stack_init();
    KLOGI(TAG, "[%4llums] USB ready", k_time_ms() - t0);

    /* GUI engine (resolution from device tree) */
    gui_init();
    KLOGI(TAG, "[%4llums] GUI ready", k_time_ms() - t0);

    /* Build main UI with detected hardware info */
    build_main_ui();

    /* Filesystem */
    if (nfs_mount() != K_OK) {
        KLOGE(TAG, "NanosFS mount failed!");
    } else {
        KLOGI(TAG, "[%4llums] NanosFS mounted", k_time_ms() - t0);
    }

    /* Asset manager */
    asset_manager_init();

    /* Event loop */
    event_loop_init();

    /* Android Auto transport */
    aap_callbacks_t cbs = {
        .on_video_frame   = on_video,
        .on_audio_frame   = on_audio,
        .on_nav_event     = on_nav,
        .on_connected     = on_aap_conn,
        .on_disconnected  = on_aap_disc,
    };
    aap_transport_init(&cbs);
    KLOGI(TAG, "[%4llums] AAP ready", k_time_ms() - t0);

    /* Power manager */
    power_manager_init();

    /* Debug shell */
    debug_shell_init();

    /* System info task */
    task_create(&s_info_task, "sysinfo", sysinfo_task, NULL,
                s_info_stk, sizeof(s_info_stk), 1);

    uint64_t bt = k_time_ms() - t0;
    KLOGI(TAG, "╔══════════════════════════════════════════════╗");
    KLOGI(TAG, "║  BOOT COMPLETE: %4llums %-20s║", bt,
          bt < 800  ? "[FAST <800ms]"  :
          bt < 1200 ? "[OK <1200ms]"   : "[SLOW >1200ms]");
    KLOGI(TAG, "║  LCD:   %-36s║", g_devtree.primary_lcd   ? g_devtree.primary_lcd->name   : "none");
    KLOGI(TAG, "║  Touch: %-36s║", g_devtree.primary_touch ? g_devtree.primary_touch->name : "none");
    KLOGI(TAG, "║  Audio: %-36s║", g_devtree.primary_audio_out ? g_devtree.primary_audio_out->name : "none");
    KLOGI(TAG, "║  Total: %-2u devices detected                  ║", g_devtree.node_count);
    KLOGI(TAG, "╚══════════════════════════════════════════════╝");

    /* Main event loop */
    while (1) {
        event_loop_tick();
        task_sleep_ms(16);
    }
}

void kernel_app_entry(void) {
    task_create(&s_app_task, "app_main", (task_entry_t)app_main_task, NULL,
                s_app_stk, sizeof(s_app_stk), 3);
}
