/* NanosOS usb/aap_transport.c - Android Auto Protocol Transport */
#include "../kernel/kernel.h"
#include "usb_stack.h"
#include "aap_transport.h"
#define AOA_GET_PROTO   51
#define AOA_SEND_IDENT  52
#define AOA_START_ACC   53
#define AOA_AUDIO_SUP   58
#define AAP_HDR_SZ      8
#define AAP_CRC_SZ      4
#define AAP_MAX_FRAME   (16*1024)
#define CH_CTRL 0
#define CH_INPUT 1
#define CH_VIDEO 3
#define CH_MEDIA 4
#define CH_NAV   7
#define CH_MAX   8
#define MSG_VERSION_REQ  1
#define MSG_VERSION_RESP 2
#define MSG_SDP_REQ      5
#define MSG_SDP_RESP     6
#define MSG_PING        13
#define MSG_PONG        14
typedef struct __attribute__((packed)){ uint32_t ch; uint16_t flags; uint16_t len; } aap_hdr_t;
typedef struct { bool open; k_msgq_t txq; uint8_t txbuf[8][512]; void(*handler)(uint8_t,const uint8_t*,uint32_t); } aap_ch_t;
static aap_state_t s_state=AAP_DISCONNECTED;
static aap_ch_t    s_ch[CH_MAX];
static uint16_t    s_ver;
static uint8_t     s_ep_in=0x81,s_ep_out=0x01;
static uint64_t    s_rxb,s_txb;
static uint32_t    s_frx,s_ftx,s_err;
static aap_callbacks_t s_cbs;
static k_mutex_t   s_lk;
static task_tcb_t  s_rx_task,s_tx_task,s_conn_task;
static uint8_t     s_rx_stk[16384] ALIGNED(16), s_tx_stk[8192] ALIGNED(16), s_conn_stk[4096] ALIGNED(16);
static uint32_t crc32b(const uint8_t *d,uint32_t n){ uint32_t c=0xFFFFFFFF;
    while(n--){c^=*d++;for(int i=0;i<8;i++)c=(c>>1)^(0xEDB88320&-(c&1));}return c^0xFFFFFFFF;}
static k_err_t ctrl_out(uint8_t req,uint16_t val,uint16_t idx,const uint8_t *d,uint16_t l){
    usb_setup_t s={0x40,req,val,idx,l}; return usb_control_transfer(&s,(uint8_t*)d,l,500); }
static k_err_t ctrl_in(uint8_t req,uint16_t val,uint16_t idx,uint8_t *d,uint16_t l){
    usb_setup_t s={0xC0,req,val,idx,l}; return usb_control_transfer(&s,d,l,500); }
static uint32_t build_frame(uint8_t *out,uint8_t ch,uint16_t flags,const uint8_t *pay,uint32_t plen){
    aap_hdr_t *h=(aap_hdr_t*)out; h->ch=ch; h->flags=flags|0x3; h->len=(uint16_t)plen;
    memcpy(out+AAP_HDR_SZ,pay,plen);
    uint32_t crc=crc32b(out,AAP_HDR_SZ+plen); memcpy(out+AAP_HDR_SZ+plen,&crc,4);
    return AAP_HDR_SZ+plen+AAP_CRC_SZ; }
static void send_ctrl(uint8_t msg_type,const uint8_t *pay,uint32_t plen){
    static uint8_t frame[AAP_HDR_SZ+256+AAP_CRC_SZ];
    uint8_t buf[258]; buf[0]=0; buf[1]=msg_type;
    if(pay&&plen) memcpy(buf+2,pay,plen);
    uint32_t fl=build_frame(frame,CH_CTRL,0x10,buf,plen+2);
    usb_bulk_write(s_ep_out,frame,fl,1000); s_txb+=fl; s_ftx++; }
static void dispatch(uint8_t ch,uint16_t flags,const uint8_t *d,uint32_t l){
    (void)flags;
    if(ch==CH_CTRL&&l>=2){
        uint16_t mt=(uint16_t)((d[0]<<8)|d[1]);
        if(mt==MSG_VERSION_RESP){s_ver=(uint16_t)((d[2]<<8)|d[3]);
            KLOGI("AAP","AAP ver %d.%d",s_ver>>8,s_ver&0xFF);
            uint8_t sdp[]={0x00,MSG_SDP_REQ,CH_INPUT,0,CH_VIDEO,0,CH_MEDIA,0,CH_NAV,0};
            send_ctrl(MSG_SDP_REQ,sdp+2,8);}
        else if(mt==MSG_SDP_RESP){s_state=AAP_RUNNING;
            for(int i=0;i<CH_MAX;i++)s_ch[i].open=true;
            if(s_cbs.on_connected)s_cbs.on_connected(); KLOGI("AAP","RUNNING"); }
        else if(mt==MSG_PING){uint8_t p[]={0,MSG_PONG};send_ctrl(MSG_PONG,NULL,0);(void)p;} }
    else if(ch==CH_VIDEO&&s_cbs.on_video_frame&&l>8){
        uint64_t pts=0; memcpy(&pts,d,8); s_cbs.on_video_frame(d+8,l-8,pts); }
    else if((ch==CH_MEDIA)&&s_cbs.on_audio_frame&&l>8){
        uint64_t pts=0; memcpy(&pts,d,8); s_cbs.on_audio_frame(d+8,(l-8)/4,pts); }
    else if(ch==CH_NAV&&s_cbs.on_nav_event&&l>=2){
        s_cbs.on_nav_event((uint32_t)((d[0]<<8)|d[1]),d+2,l-2); } }
static void rx_task(void *arg){ (void)arg;
    uint8_t *buf=(uint8_t*)k_psram_alloc(AAP_MAX_FRAME+64); NANOS_ASSERT(buf);
    while(1){
        if(s_state<AAP_RUNNING){task_sleep_ms(50);continue;}
        int32_t n=usb_bulk_read(s_ep_in,buf,AAP_HDR_SZ,5000);
        if(n<(int32_t)AAP_HDR_SZ)continue;
        aap_hdr_t *h=(aap_hdr_t*)buf; uint8_t ch=(uint8_t)(h->ch&0xFF); uint32_t plen=h->len;
        if(plen>AAP_MAX_FRAME||ch>=CH_MAX){s_err++;continue;}
        if(plen>0){n=usb_bulk_read(s_ep_in,buf+AAP_HDR_SZ,plen,2000);if(n<(int32_t)plen){s_err++;continue;}}
        uint32_t rxc,calcc=crc32b(buf,AAP_HDR_SZ+plen);
        usb_bulk_read(s_ep_in,(uint8_t*)&rxc,4,500);
        if(calcc!=rxc){s_err++;continue;}
        s_rxb+=AAP_HDR_SZ+plen+AAP_CRC_SZ; s_frx++;
        dispatch(ch,h->flags,buf+AAP_HDR_SZ,plen); } }
static void tx_task(void *arg){ (void)arg;
    static uint8_t frame[AAP_HDR_SZ+512+AAP_CRC_SZ];
    while(1){
        bool sent=false;
        for(int i=0;i<CH_MAX;i++){ if(!s_ch[i].open)continue;
            uint8_t item[512]; if(k_msgq_recv(&s_ch[i].txq,item,K_NO_WAIT)!=K_OK)continue;
            uint32_t plen=*(uint32_t*)item;
            uint32_t fl=build_frame(frame,(uint8_t)i,0,item+4,plen);
            usb_bulk_write(s_ep_out,frame,fl,1000); s_txb+=fl; s_ftx++; sent=true; }
        if(!sent)task_sleep_ms(1); } }
static void conn_task(void *arg){ (void)arg;
    while(1){
        switch(s_state){
        case AAP_DISCONNECTED:
            if(usb_device_connected()){s_state=AAP_AOA_PROBE;}
            else task_sleep_ms(500); break;
        case AAP_AOA_PROBE: {
            uint8_t pb[2]={0}; ctrl_in(AOA_GET_PROTO,0,0,pb,2);
            uint16_t proto=(uint16_t)((pb[1]<<8)|pb[0]);
            KLOGI("AAP","AOA proto=%d",proto);
            if(proto>=1){
                const char *ids[]={"NanosOS","ESP32S3 Head Unit","Car Infotainment","1.0","https://nanos.dev","0001"};
                for(int i=0;i<6;i++) ctrl_out(AOA_SEND_IDENT,0,i,(const uint8_t*)ids[i],strlen(ids[i])+1);
                if(proto>=2){uint8_t a=1;ctrl_out(AOA_AUDIO_SUP,1,0,&a,1);}
                ctrl_out(AOA_START_ACC,0,0,NULL,0);
                task_sleep_ms(2000); s_state=AAP_CONNECTED;
            } else { s_state=AAP_DISCONNECTED; task_sleep_ms(2000); } break; }
        case AAP_CONNECTED:
            task_sleep_ms(500);
            if(usb_device_connected()){uint8_t vp[]={0x02,0x00};send_ctrl(MSG_VERSION_REQ,vp,2);s_state=AAP_VERSION_NEG;}
            break;
        case AAP_VERSION_NEG: case AAP_SERVICE_DISC:
            task_sleep_ms(100); break;
        case AAP_RUNNING:
            task_sleep_ms(5000);
            send_ctrl(MSG_PING,NULL,0); break;
        case AAP_ERROR:
            usb_device_reset(); s_state=AAP_DISCONNECTED;
            if(s_cbs.on_disconnected)s_cbs.on_disconnected();
            task_sleep_ms(3000); break; } } }
void aap_transport_init(const aap_callbacks_t *cbs){
    memset(&s_ch,0,sizeof(s_ch)); k_mutex_init(&s_lk,"aap");
    if(cbs) s_cbs=*cbs;
    for(int i=0;i<CH_MAX;i++) k_msgq_init(&s_ch[i].txq,"atx",s_ch[i].txbuf,512,8);
    s_state=AAP_DISCONNECTED;
    task_create(&s_conn_task,"aap_conn",conn_task,NULL,s_conn_stk,sizeof(s_conn_stk),4);
    task_create(&s_rx_task,"aap_rx",rx_task,NULL,s_rx_stk,sizeof(s_rx_stk),5);
    task_create(&s_tx_task,"aap_tx",tx_task,NULL,s_tx_stk,sizeof(s_tx_stk),4);
    KLOGI("AAP","Android Auto transport initialized"); }
void aap_send_touch(int16_t x,int16_t y,uint8_t act){
    if(s_state!=AAP_RUNNING||!s_ch[CH_INPUT].open)return;
    uint8_t item[4+16]; uint32_t plen=16; memcpy(item,&plen,4);
    uint64_t ts=k_time_ms()*1000; memcpy(item+4,&ts,8);
    item[12]=act; item[13]=0; memcpy(item+14,&x,2); memcpy(item+16,&y,2);
    k_msgq_send(&s_ch[CH_INPUT].txq,item,100); }
aap_state_t aap_get_state(void){return s_state;}
bool aap_is_running(void){return s_state==AAP_RUNNING;}
uint64_t aap_bytes_rx(void){return s_rxb;}
uint64_t aap_bytes_tx(void){return s_txb;}
void aap_stats_print(void){
    KLOGI("AAP","state=%d rx=%lluB tx=%lluB frx=%u ftx=%u err=%u",
          s_state,(unsigned long long)s_rxb,(unsigned long long)s_txb,s_frx,s_ftx,s_err); }
