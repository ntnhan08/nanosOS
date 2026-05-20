/* NanosOS gui/framebuffer.c - Double-buffered RGB565 GUI engine */
#include "../kernel/kernel.h"
#include "gui.h"
#include <math.h>
#define LCD_W 480
#define LCD_H 800
#define LCD_FB (LCD_W*LCD_H*2)
#define FPS_TARGET 30
#define FRAME_MS  (1000/FPS_TARGET)
static uint16_t *s_fb[2]; static uint8_t s_act,s_draw;
static uint32_t s_frame_cnt,s_fps; static uint64_t s_fps_t;
static uint32_t s_dirty[(LCD_H/16)+1];
static k_mutex_t s_lock;
static k_sem_t   s_vsync;
static gui_widget_t s_widgets[128]; static uint32_t s_wcnt;
static gui_widget_t *s_root;
static task_tcb_t s_rtask; static uint8_t s_rstack[16384] ALIGNED(16);
extern void lcd_set_window(uint16_t,uint16_t,uint16_t,uint16_t);
extern void lcd_write_pixels(const uint16_t*,uint32_t);
static inline uint16_t blend(uint16_t d,uint16_t s,uint8_t a){
    if(a==255)return s; if(!a)return d;
    uint32_t inv=256-a,dr=(d>>8)&0xF8,dg=(d>>3)&0xFC,db=(d<<3)&0xF8;
    uint32_t sr=(s>>8)&0xF8,sg=(s>>3)&0xFC,sb=(s<<3)&0xF8;
    return RGB565((sr*a+dr*inv)>>8,(sg*a+dg*inv)>>8,(sb*a+db*inv)>>8); }
static inline uint16_t *fbp(int x,int y){return s_fb[s_draw]+y*LCD_W+x;}
static void mark(int y,int h){int a=y/16,b=(y+h-1)/16; for(int i=a;i<=b&&i<(int)(LCD_H/16+1);i++)s_dirty[i]=1;}
void gui_fill_rect(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t c){
    if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;}
    if(x+w>LCD_W)w=LCD_W-x; if(y+h>LCD_H)h=LCD_H-y; if(w<=0||h<=0)return;
    mark(y,h); uint16_t *d=fbp(x,y);
    for(int r=0;r<h;r++){uint32_t p=((uint32_t)c<<16)|c;uint32_t*dw=(uint32_t*)d;for(int i=0;i<w/2;i++)dw[i]=p;if(w&1)d[w-1]=c;d+=LCD_W;} }
void gui_fill_rounded_rect(int16_t x,int16_t y,int16_t w,int16_t h,int16_t r,uint16_t c){
    if(r<=0){gui_fill_rect(x,y,w,h,c);return;}
    r=MIN(r,MIN(w,h)/2);
    gui_fill_rect(x+r,y,w-2*r,r,c); gui_fill_rect(x,y+r,w,h-2*r,c); gui_fill_rect(x+r,y+h-r,w-2*r,r,c);
    for(int cy=0;cy<=r;cy++){int cx=(int)sqrtf((float)(r*r-cy*cy));
        gui_fill_rect(x+r-cx,y+r-cy,cx,1,c); gui_fill_rect(x+w-1-r,y+r-cy,cx,1,c);
        gui_fill_rect(x+r-cx,y+h-1-r+cy,cx,1,c); gui_fill_rect(x+w-1-r,y+h-1-r+cy,cx,1,c);} }
extern const uint8_t g_font8x16[96][16];
void gui_draw_string(int16_t x,int16_t y,const char *s,uint16_t fg,uint16_t bg,uint8_t sc){
    while(*s){char c=*s++;if(c<0x20||c>0x7E)c='?';
        const uint8_t *g=g_font8x16[(uint8_t)(c-0x20)];
        for(int r=0;r<16;r++){uint8_t b=g[r];for(int col=0;col<8;col++){
            uint16_t cl=(b&BIT(7-col))?fg:bg;
            for(int sy=0;sy<sc;sy++)for(int sx=0;sx<sc;sx++){int px=x+col*sc+sx,py=y+r*sc+sy;
                if(px>=0&&px<LCD_W&&py>=0&&py<LCD_H)*fbp(px,py)=cl;}}}
        x+=8*sc;} }
void gui_draw_string_to_buf(uint16_t *fb,int16_t x,int16_t y,const char *s,uint16_t fg,uint16_t bg,uint8_t sc){
    uint16_t *save=s_fb[s_draw]; s_fb[s_draw]=fb; gui_draw_string(x,y,s,fg,bg,sc); s_fb[s_draw]=save;}
gui_widget_t *gui_widget_create(uint8_t t,int16_t x,int16_t y,int16_t w,int16_t h){
    if(s_wcnt>=128)return NULL; gui_widget_t *gw=&s_widgets[s_wcnt++];
    memset(gw,0,sizeof(*gw)); gw->type=t; gw->x=x; gw->y=y; gw->w=w; gw->h=h;
    gw->alpha=255; gw->visible=true; gw->dirty=true;
    gw->bg=COLOR_CARD; gw->fg=COLOR_WHITE; return gw; }
bool gui_widget_add_child(gui_widget_t *p,gui_widget_t *c){
    if(p->nch>=16)return false; p->children[p->nch++]=c; c->parent=p; return true; }
static void widget_abs(gui_widget_t *w,int16_t *ax,int16_t *ay){
    *ax=w->x;*ay=w->y; gui_widget_t *p=w->parent; while(p){*ax+=p->x;*ay+=p->y;p=p->parent;} }
static void widget_render(gui_widget_t *w){
    if(!w->visible)return;
    int16_t ax,ay; widget_abs(w,&ax,&ay);
    if(w->cr>0)gui_fill_rounded_rect(ax,ay,w->w,w->h,w->cr,w->bg);
    else if(w->alpha==255)gui_fill_rect(ax,ay,w->w,w->h,w->bg);
    if(w->text[0]){int16_t tx=ax+(w->w-(int16_t)strlen(w->text)*8)/2,ty=ay+(w->h-16)/2;
        gui_draw_string(tx,ty,w->text,w->fg,w->bg,1);}
    if(w->draw)w->draw(w,s_fb[s_draw]);
    for(int i=0;i<w->nch;i++)widget_render(w->children[i]);
    w->dirty=false; }
static float ease(float t){return t<.5f?2*t*t:1-(-2*t+2)*(-2*t+2)/2;}
static void anim_update(gui_widget_t *w){
    if(!w->anim.run)return;
    float t=(float)(k_time_ms()-w->anim.t0)/w->anim.dur; if(t>=1.f){w->x=w->anim.tx;w->y=w->anim.ty;w->alpha=w->anim.ta;w->anim.run=false;}
    else{float e=ease(t); w->x+=(int16_t)((w->anim.tx-w->x)*e); w->y+=(int16_t)((w->anim.ty-w->y)*e);} w->dirty=true;}
void gui_animate(gui_widget_t *w,int16_t tx,int16_t ty,uint8_t ta,uint16_t ms){
    w->anim.tx=tx;w->anim.ty=ty;w->anim.ta=ta;w->anim.dur=ms;w->anim.t0=k_time_ms();w->anim.run=true;}
void gui_inject_touch(int16_t tx,int16_t ty,uint8_t ev){ (void)tx;(void)ty;(void)ev; }
static void render_task(void *arg){ (void)arg;
    uint64_t nxt=k_time_ms();
    KLOGI("GUI","Render task: %dFPS",FPS_TARGET);
    while(1){
        uint64_t now=k_time_ms(); if(now<nxt)task_sleep_ms((uint32_t)(nxt-now)); nxt+=FRAME_MS;
        k_mutex_lock(&s_lock,K_FOREVER);
        for(uint32_t i=0;i<s_wcnt;i++)anim_update(&s_widgets[i]);
        if(s_root)widget_render(s_root);
        for(int b=0;b<(int)(LCD_H/16+1);b++){
            if(!s_dirty[b])continue;
            int ys=b*16,ye=MIN(ys+16,LCD_H);
            lcd_set_window(0,ys,LCD_W,ye-ys);
            lcd_write_pixels(s_fb[s_draw]+ys*LCD_W,LCD_W*(ye-ys)); }
        s_act^=1; s_draw^=1; s_frame_cnt++;
        memset(s_dirty,0,sizeof(s_dirty));
        if((s_frame_cnt%FPS_TARGET)==0){s_fps=(FPS_TARGET*1000)/(uint32_t)(k_time_ms()-s_fps_t);s_fps_t=k_time_ms();}
        k_mutex_unlock(&s_lock); } }
void gui_init(void){
    s_fb[0]=(uint16_t*)k_psram_alloc(LCD_FB); s_fb[1]=(uint16_t*)k_psram_alloc(LCD_FB);
    NANOS_ASSERT(s_fb[0]&&s_fb[1]);
    memset(s_fb[0],0,LCD_FB); memset(s_fb[1],0,LCD_FB);
    k_mutex_init(&s_lock,"gui"); k_sem_init(&s_vsync,0,1,"vsync");
    memset(s_dirty,0xFF,sizeof(s_dirty));
    s_root=gui_widget_create(WT_PANEL,0,0,LCD_W,LCD_H); s_root->bg=COLOR_BG;
    task_create(&s_rtask,"gui_render",render_task,NULL,s_rstack,sizeof(s_rstack),6);
    KLOGI("GUI","Init: %dx%d RGB565 double-buf PSRAM",LCD_W,LCD_H); }
gui_widget_t *gui_get_root(void){return s_root;}
uint32_t gui_get_fps(void){return s_fps;}
