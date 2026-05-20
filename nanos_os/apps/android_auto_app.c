/* NanosOS — apps/android_auto_app.c - Android Auto Application Layer */
#include "../kernel/kernel.h"
#include "../gui/gui.h"
#include "../audio/audio.h"
#include "../usb/aap_transport.h"
#include "../system/devtree/device_tree.h"

static const char *TAG = "AA_APP";
static gui_widget_t *s_status_lbl = NULL;
static gui_widget_t *s_nav_overlay = NULL;
static gui_widget_t *s_media_bar  = NULL;

void android_auto_app_on_connected(void) {
    KLOGI(TAG, "Phone connected — Android Auto active");
    if (s_status_lbl) {
        strncpy(s_status_lbl->text, "Android Auto — Connected",
                sizeof(s_status_lbl->text) - 1);
        s_status_lbl->fg_color = RGB565(32, 200, 80);
        s_status_lbl->dirty = true;
    }
    if (s_media_bar) { s_media_bar->visible = true; s_media_bar->dirty = true; }
}

void android_auto_app_on_disconnected(void) {
    KLOGI(TAG, "Android Auto disconnected");
    if (s_status_lbl) {
        strncpy(s_status_lbl->text, "Connect phone via USB",
                sizeof(s_status_lbl->text) - 1);
        s_status_lbl->fg_color = RGB565(32, 151, 212);
        s_status_lbl->dirty = true;
    }
    if (s_nav_overlay)  { s_nav_overlay->visible  = false; }
    if (s_media_bar)    { s_media_bar->visible     = false; }
}

void android_auto_app_on_video(const uint8_t *data, uint32_t len, uint64_t pts) {
    (void)data; (void)pts;
    /* Phase 3: H.264 decode → framebuffer */
    if (len > 0) KLOGD(TAG, "Video NAL %uB pts=%llu", len, (unsigned long long)pts);
}

void android_auto_app_on_nav(uint32_t event_type, const uint8_t *payload, uint32_t len) {
    (void)payload; (void)len;
    if (!s_nav_overlay) return;
    const char *nav_texts[] = {
        "", "← Turn Left", "Turn Right →",
        "↑ Continue", "↓ U-Turn", "★ Destination"
    };
    const char *txt = (event_type < 6) ? nav_texts[event_type] : "Navigation";
    strncpy(s_nav_overlay->text, txt, sizeof(s_nav_overlay->text) - 1);
    s_nav_overlay->visible = (event_type > 0);
    s_nav_overlay->dirty   = true;
}
