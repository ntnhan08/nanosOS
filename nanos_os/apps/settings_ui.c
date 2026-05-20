/* NanosOS — apps/settings_ui.c - Settings screen */
#include "../kernel/kernel.h"
#include "../gui/gui.h"
#include "../system/devtree/device_tree.h"

typedef struct {
    uint8_t  brightness;
    uint8_t  volume;
    bool     bt_enabled;
    bool     wifi_enabled;
    bool     night_mode;
    char     device_name[32];
} settings_t;

static settings_t s_settings = {
    .brightness  = 180,
    .volume      = 200,
    .bt_enabled  = true,
    .wifi_enabled = false,
    .night_mode  = false,
    .device_name = "NanosOS HMI",
};

void settings_save(void) {
    extern int nfs_open(const char*, uint8_t);
    extern int32_t nfs_write(int, const void*, uint32_t);
    extern int nfs_close(int);
    int fd = nfs_open("/settings.bin", 0x02 | 0x10); /* O_WRONLY|O_CREAT */
    if (fd >= 0) {
        nfs_write(fd, &s_settings, sizeof(s_settings));
        nfs_close(fd);
        KLOGI("SETTINGS", "Saved");
    }
}

void settings_load(void) {
    extern int nfs_open(const char*, uint8_t);
    extern int32_t nfs_read(int, void*, uint32_t);
    extern int nfs_close(int);
    int fd = nfs_open("/settings.bin", 0x01); /* O_RDONLY */
    if (fd >= 0) {
        nfs_read(fd, &s_settings, sizeof(s_settings));
        nfs_close(fd);
        KLOGI("SETTINGS", "Loaded: brightness=%d volume=%d",
              s_settings.brightness, s_settings.volume);
    }
}

settings_t *settings_get(void) { return &s_settings; }

void settings_set_brightness(uint8_t v) {
    s_settings.brightness = v;
    extern void hal_lcd_set_backlight(uint8_t);
    hal_lcd_set_backlight(v);
}

void settings_set_volume(uint8_t v) {
    s_settings.volume = v;
    extern void hal_audio_set_volume(uint8_t);
    hal_audio_set_volume(v);
}
