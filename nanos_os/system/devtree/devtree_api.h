/* NanosOS — Unified Hardware API (post-autodetect) */
#pragma once
#include "../../kernel/kernel.h"
#include "device_tree.h"

/* LCD */
void hal_lcd_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void hal_lcd_write_pixels(const uint16_t *px, uint32_t count);
void hal_lcd_set_backlight(uint8_t level);
bool hal_lcd_get_caps(lcd_caps_t *caps);

/* Touch */
int  hal_touch_read(ioctl_touch_data_t *out);

/* Audio */
void hal_audio_set_volume(uint8_t vol);

/* Autodetect entry point */
void drivers_autodetect_init(void);
