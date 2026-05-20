/* NanosOS — kernel/logger.c */
#include "kernel.h"
#include <stdarg.h>
#include <stdio.h>
static log_level_t s_level=LOG_DEBUG;
static char s_ring[4096]; static uint32_t s_ring_head;
void hal_uart_putchar(char c);
void klog_init(void){ s_ring_head=0; hal_uart_putchar('\r'); hal_uart_putchar('\n'); }
void klog(log_level_t lv,const char *tag,const char *fmt,...){
    if(lv>s_level) return;
    static const char lc[]={' ','E','W','I','D','V'};
    char buf[256]; int n=snprintf(buf,sizeof(buf),"[%c][%6ums][%-8s] ",lc[lv],(uint32_t)k_time_ms(),tag);
    if(n<0) n=0;
    va_list a; va_start(a,fmt);
    int m=vsnprintf(buf+n,sizeof(buf)-n-3,fmt,a); va_end(a);
    if(m<0) m=0; int t=n+m; buf[t++]='\r'; buf[t++]='\n'; buf[t]=0;
    uint32_t ps=irq_save();
    for(int i=0;i<t;i++){ hal_uart_putchar(buf[i]); s_ring[s_ring_head++%4096]=buf[i]; }
    irq_restore(ps);
}
