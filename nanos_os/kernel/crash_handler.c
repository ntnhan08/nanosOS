/* NanosOS — kernel/crash_handler.c */
#include "kernel.h"
void _c_exception_handler(uint32_t cause){
    __asm__ volatile("movi a2,0x0004000F\nwsr a2,PS\nrsync":::"a2");
    uint32_t epc,exc; __asm__ volatile("rsr %0,EPC1":"=a"(epc)); __asm__ volatile("rsr %0,EXCVADDR":"=a"(exc));
    volatile uint32_t *u=(volatile uint32_t*)0x60000000;
    #define P(s) do{for(const char *_p=(s);*_p;_p++) *u=*_p;}while(0)
    P("\r\n[PANIC] Exception cause="); 
    char b[12]; uint32_t v=cause; int i=10; b[11]=0;
    do{b[--i]='0'+v%10;v/=10;}while(v); P(b+i); P(" EPC=");
    v=epc; i=10; do{b[--i]="0123456789ABCDEF"[v&15];v>>=4;}while(v); P(b+i);
    task_tcb_t *t=g_kernel.current_task;
    if(t){ P(" task="); P(t->name); if(t->stack_guard!=0xDEADBEEFU) P(" STACK_OVERFLOW"); }
    P("\r\nHalted.\r\n"); (void)exc;
    while(1) __asm__("waiti 0");
}
void _c_nmi_handler(void){ extern void usb_host_irq(void); usb_host_irq(); }
