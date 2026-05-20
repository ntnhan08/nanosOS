#pragma once
#include "../kernel/kernel.h"
typedef struct gw { int16_t x,y,w,h; uint16_t bg,fg; uint8_t alpha,type; bool visible,dirty;
    struct gw *parent,*children[16]; uint8_t nch;
    void(*draw)(struct gw*,uint16_t*); bool(*touch)(struct gw*,int16_t,int16_t,uint8_t);
    struct{int16_t tx,ty;uint8_t ta;uint16_t dur;uint64_t t0;bool run;} anim;
    char text[64]; uint8_t cr,bw; uint16_t bc; } gui_widget_t;
#define RGB565(r,g,b) ((uint16_t)(((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3))
#define COLOR_BG    RGB565(10,12,22)
#define COLOR_BLUE  RGB565(32,151,212)
#define COLOR_WHITE RGB565(240,240,240)
#define COLOR_GRAY  RGB565(80,90,120)
#define COLOR_CARD  RGB565(20,28,45)
#define WT_PANEL 0
#define WT_LABEL 1
#define WT_BTN   2
#define WT_CANVAS 5
void gui_init(void);
gui_widget_t *gui_get_root(void);
gui_widget_t *gui_widget_create(uint8_t t,int16_t x,int16_t y,int16_t w,int16_t h);
bool gui_widget_add_child(gui_widget_t *p,gui_widget_t *c);
void gui_animate(gui_widget_t *w,int16_t tx,int16_t ty,uint8_t ta,uint16_t ms);
void gui_inject_touch(int16_t x,int16_t y,uint8_t ev);
uint32_t gui_get_fps(void);
void gui_fill_rect(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t c);
void gui_fill_rounded_rect(int16_t x,int16_t y,int16_t w,int16_t h,int16_t r,uint16_t c);
void gui_draw_string(int16_t x,int16_t y,const char *s,uint16_t fg,uint16_t bg,uint8_t sc);
void gui_draw_string_to_buf(uint16_t*,int16_t,int16_t,const char*,uint16_t,uint16_t,uint8_t);
