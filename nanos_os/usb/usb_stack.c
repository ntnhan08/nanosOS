/* NanosOS usb/usb_stack.c - DWC OTG USB2.0 Host Stack */
#include "../kernel/kernel.h"
#include "usb_stack.h"
#define USB_BASE   0x60080000UL
#define GRSTCTL    (*(volatile uint32_t*)(USB_BASE+0x010))
#define GUSBCFG    (*(volatile uint32_t*)(USB_BASE+0x00C))
#define GAHBCFG    (*(volatile uint32_t*)(USB_BASE+0x008))
#define GINTSTS    (*(volatile uint32_t*)(USB_BASE+0x014))
#define GINTMSK    (*(volatile uint32_t*)(USB_BASE+0x018))
#define GCCFG      (*(volatile uint32_t*)(USB_BASE+0x038))
#define HPRT       (*(volatile uint32_t*)(USB_BASE+0x440))
#define HCCHAR(n)  (*(volatile uint32_t*)(USB_BASE+0x500+(n)*0x20))
#define HCINT(n)   (*(volatile uint32_t*)(USB_BASE+0x508+(n)*0x20))
#define HCINTMSK(n)(*(volatile uint32_t*)(USB_BASE+0x50C+(n)*0x20))
#define HCTSIZ(n)  (*(volatile uint32_t*)(USB_BASE+0x510+(n)*0x20))
#define HCDMA(n)   (*(volatile uint32_t*)(USB_BASE+0x514+(n)*0x20))
#define GRXFSIZ    (*(volatile uint32_t*)(USB_BASE+0x024))
#define GNPTXFSIZ  (*(volatile uint32_t*)(USB_BASE+0x028))
#define HCFG       (*(volatile uint32_t*)(USB_BASE+0x400))
#define NUM_CH 8
typedef struct { bool inuse; k_sem_t done; int32_t result; bool ep_in; uint16_t mps; } hc_t;
static DRAM_ATTR hc_t   s_ch[NUM_CH];
static DRAM_ATTR bool   s_connected;
static DRAM_ATTR uint8_t s_dev_addr;
static DRAM_ATTR uint16_t s_vid,s_pid;
static k_mutex_t s_lk; static k_sem_t s_conn;
static task_tcb_t s_task; static uint8_t s_stk[4096] ALIGNED(16);
static k_err_t core_reset(void){
    uint32_t t=5000; while(!(GRSTCTL&BIT(31))&&--t)task_sleep_ms(1);
    GRSTCTL|=BIT(0); t=5000; while((GRSTCTL&BIT(0))&&--t)task_sleep_ms(1);
    task_sleep_ms(3); return t?K_OK:K_ERR_TIMEOUT; }
static int alloc_ch(void){ k_mutex_lock(&s_lk,K_FOREVER);
    for(int i=0;i<NUM_CH;i++){if(!s_ch[i].inuse){s_ch[i].inuse=true;k_mutex_unlock(&s_lk);return i;}}
    k_mutex_unlock(&s_lk); return -1; }
static void free_ch(int n){s_ch[n].inuse=false;}
static int32_t hc_xfer(uint8_t dev,uint8_t ep,bool in,uint8_t type,uint16_t mps,void *buf,uint32_t len,uint32_t pid){
    int ch=alloc_ch(); if(ch<0)return -K_ERR_BUSY;
    s_ch[ch].ep_in=in; s_ch[ch].mps=mps;
    k_sem_init(&s_ch[ch].done,0,1,"hc");
    uint32_t npkt=(len+mps-1)/mps; if(!npkt)npkt=1;
    HCCHAR(ch)=(dev<<22)|((ep&0xF)<<11)|(in?BIT(15):0)|(type<<18)|mps;
    HCINT(ch)=0xFFFFFFFF; HCINTMSK(ch)=BIT(0)|BIT(3)|BIT(7);
    HCTSIZ(ch)=(pid<<29)|(npkt<<19)|len;
    HCDMA(ch)=(uint32_t)buf;
    HCCHAR(ch)|=BIT(31); /* CHENA */
    k_err_t r=k_sem_take(&s_ch[ch].done,2000);
    int32_t res=(r==K_OK)?s_ch[ch].result:-K_ERR_TIMEOUT;
    free_ch(ch); return res; }
k_err_t usb_control_transfer(const usb_setup_t *s,uint8_t *d,uint16_t l,uint32_t tms){
    (void)tms;
    hc_xfer(s_dev_addr,0,false,0,64,(void*)s,8,3); /* SETUP */
    if(l&&d) hc_xfer(s_dev_addr,0,!!(s->bmRT&0x80),0,64,d,l,1);
    uint8_t dummy=0; hc_xfer(s_dev_addr,0,!(s->bmRT&0x80),0,64,&dummy,0,1);
    return K_OK; }
int32_t usb_bulk_write(uint8_t ep,const uint8_t *buf,uint32_t len,uint32_t tms){
    (void)tms; return hc_xfer(s_dev_addr,ep&0x7F,false,2,512,(void*)buf,len,0); }
int32_t usb_bulk_read(uint8_t ep,uint8_t *buf,uint32_t len,uint32_t tms){
    (void)tms; return hc_xfer(s_dev_addr,ep&0x7F,true,2,512,buf,len,2); }
void IRAM_ATTR usb_host_irq(void){
    uint32_t gi=GINTSTS;
    if(gi&BIT(24)){uint32_t hp=HPRT;if(hp&BIT(1)){s_connected=true;HPRT=hp|BIT(1);k_sem_give(&s_conn);}GINTSTS=BIT(24);}
    if(gi&BIT(25)){uint32_t haint=*(volatile uint32_t*)(USB_BASE+0x414);
        for(int i=0;i<NUM_CH;i++){if(!(haint&BIT(i)))continue;
            uint32_t hi=HCINT(i); HCINT(i)=hi;
            if(hi&BIT(0)){s_ch[i].result=(int32_t)(HCTSIZ(i)&0x7FFFF);k_sem_give(&s_ch[i].done);}
            else if(hi&(BIT(3)|BIT(7))){s_ch[i].result=-K_ERR_AGAIN;k_sem_give(&s_ch[i].done);}}}
    GINTSTS=gi; }
static void usb_host_task(void *arg){ (void)arg;
    KLOGI("USB","Host task started");
    while(1){
        k_sem_take(&s_conn,K_FOREVER);
        HPRT|=BIT(8); task_sleep_ms(50); HPRT&=~BIT(8); task_sleep_ms(20);
        /* enumerate: SET_ADDRESS=1, GET_DESCRIPTOR */
        s_dev_addr=1;
        usb_setup_t sa={0x00,5,1,0,0}; usb_control_transfer(&sa,NULL,0,500);
        KLOGI("USB","Device ready addr=%d",s_dev_addr);
        while(s_connected) task_sleep_ms(100); } }
void usb_stack_init(void){
    memset(&s_ch,0,sizeof(s_ch)); k_mutex_init(&s_lk,"usb"); k_sem_init(&s_conn,0,1,"uconn");
    core_reset();
    GUSBCFG=BIT(29)|(9<<10); /* force host, turn-around=9 */
    GAHBCFG=BIT(5)|BIT(0);   /* DMA en, glob int mask */
    GCCFG=BIT(16)|BIT(19); HCFG=0x01;
    GRXFSIZ=256; GNPTXFSIZ=(96<<16)|256;
    GINTMSK=BIT(24)|BIT(25); GINTSTS=0xFFFFFFFF;
    HPRT|=BIT(12); /* PWR */
    task_create(&s_task,"usb_host",usb_host_task,NULL,s_stk,sizeof(s_stk),5);
    KLOGI("USB","DWC OTG host stack ready"); }
bool usb_device_connected(void){return s_connected;}
void usb_device_reset(void){core_reset();s_connected=false;}
