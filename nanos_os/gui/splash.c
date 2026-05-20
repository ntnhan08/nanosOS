/* NanosOS gui/splash.c - Boot splash screen */
#include "../kernel/kernel.h"
#include "gui.h"
#define LCD_W 480
#define LCD_H 800
extern void lcd_set_window(uint16_t,uint16_t,uint16_t,uint16_t);
extern void lcd_write_pixels(const uint16_t*,uint32_t);
void show_splash_screen(void){
    uint16_t *fb=(uint16_t*)k_psram_alloc(LCD_W*LCD_H*2);
    if(!fb){KLOGW("SPLASH","No mem");return;}
    uint16_t bg=RGB565(10,12,22);
    uint32_t packed=((uint32_t)bg<<16)|bg;
    uint32_t *p=(uint32_t*)fb;
    for(int i=0;i<LCD_W*LCD_H/2;i++)p[i]=packed;
    /* Draw logo text */
    gui_draw_string_to_buf(fb,(LCD_W-8*7*2)/2,LCD_H/2-40,"NanosOS",COLOR_BLUE,bg,2);
    gui_draw_string_to_buf(fb,(LCD_W-8*18)/2,LCD_H/2+8,"ESP32-S3 Head Unit",COLOR_GRAY,bg,1);
    gui_draw_string_to_buf(fb,(LCD_W-8*6)/2,LCD_H/2+24,"v1.0.0",COLOR_GRAY,bg,1);
    /* Progress bar background */
    uint16_t bar_bg=RGB565(30,35,55);
    for(int y=LCD_H/2+60;y<LCD_H/2+70;y++)
        for(int x=(LCD_W-280)/2;x<(LCD_W+280)/2;x++) fb[y*LCD_W+x]=bar_bg;
    lcd_set_window(0,0,LCD_W,LCD_H);
    lcd_write_pixels(fb,LCD_W*LCD_H);
    /* Animate bar */
    uint16_t bar_fg=RGB565(32,151,212);
    int steps[]={20,40,60,80,100};
    int delays[]={60,80,60,50,40};
    for(int s=0;s<5;s++){
        task_sleep_ms(delays[s]);
        int fill=280*steps[s]/100,bx=(LCD_W-280)/2;
        for(int y=LCD_H/2+60;y<LCD_H/2+70;y++)
            for(int x=bx;x<bx+fill;x++) fb[y*LCD_W+x]=bar_fg;
        lcd_set_window(0,LCD_H/2+60,LCD_W,10);
        lcd_write_pixels(fb+(LCD_H/2+60)*LCD_W,LCD_W*10);}
    k_psram_free(fb);
    KLOGI("SPLASH","Boot splash complete");}
