#pragma once
#include "../../kernel/kernel.h"
void lcd_driver_init(void);
void lcd_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void lcd_write_pixels(const uint16_t *pixels, uint32_t count);
void lcd_set_backlight(uint8_t level);
