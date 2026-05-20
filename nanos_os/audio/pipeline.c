/* NanosOS audio/pipeline.c - I2S DMA Audio Engine 48kHz Stereo */
#include "../kernel/kernel.h"
#include "audio.h"
#define AUDIO_SR  48000U
#define AUDIO_CH  2U
#define DMA_CHUNK 1024U
#define DMA_BUFS  4U
#define MAX_STREAMS 4U
#define I2S0_BASE 0x6002D000UL
#define I2S0_TX_CONF  (*(volatile uint32_t*)(I2S0_BASE+0x00))
#define I2S0_TX_CONF1 (*(volatile uint32_t*)(I2S0_BASE+0x04))
#define I2S0_INT_CLR  (*(volatile uint32_t*)(I2S0_BASE+0x50))
#define I2S0_INT_ENA  (*(volatile uint32_t*)(I2S0_BASE+0x4C))
typedef struct { bool active,paused,loop; const int16_t *pcm; uint32_t total,pos; uint32_t rate; uint8_t vol; float phase,ratio; uint64_t pts; } stream_t;
static DRAM_ATTR int16_t   s_dma[DMA_BUFS][DMA_CHUNK*AUDIO_CH];
static DRAM_ATTR int32_t   s_mix[DMA_CHUNK*AUDIO_CH];
static DRAM_ATTR stream_t  s_st[MAX_STREAMS];
static DRAM_ATTR uint8_t   s_mvol=200;
static DRAM_ATTR uint32_t  s_underruns,s_frames;
static k_sem_t    s_rdy; static k_mutex_t s_lk;
static task_tcb_t s_task; static uint8_t s_stk[8192] ALIGNED(16);
static uint8_t s_play_idx;
static void mix_fill(uint8_t idx){
    memset(s_mix,0,sizeof(s_mix));
    k_mutex_lock(&s_lk,K_FOREVER);
    for(uint32_t si=0;si<MAX_STREAMS;si++){
        stream_t *s=&s_st[si]; if(!s->active||s->paused||!s->pcm)continue;
        for(uint32_t i=0;i<DMA_CHUNK;i++){
            uint32_t i0=(uint32_t)s->phase,i1=MIN(i0+1,s->total-1);
            float fr=s->phase-(float)i0;
            if(i0>=s->total){if(s->loop){s->phase=0;i0=0;i1=1;}else{s->active=false;break;}}
            int32_t l=(int32_t)((s->pcm[i0*2  ]+(s->pcm[i1*2  ]-s->pcm[i0*2  ])*fr)*s->vol/255);
            int32_t r=(int32_t)((s->pcm[i0*2+1]+(s->pcm[i1*2+1]-s->pcm[i0*2+1])*fr)*s->vol/255);
            s_mix[i*2]+=l; s_mix[i*2+1]+=r; s->phase+=s->ratio; } }
    k_mutex_unlock(&s_lk);
    for(uint32_t i=0;i<DMA_CHUNK*AUDIO_CH;i++){
        int32_t v=(s_mix[i]*s_mvol)>>8;
        s_dma[idx][i]=(int16_t)(v>32767?32767:v<-32768?-32768:v); }
    s_frames+=DMA_CHUNK; }
static void i2s_hw_init(void){
    I2S0_TX_CONF=BIT(0); I2S0_TX_CONF=0; /* reset */
    I2S0_TX_CONF1=(32<<0)|(16<<5)|BIT(12); /* 32-bit slot, 16-bit data, 2ch */
    I2S0_TX_CONF=BIT(4)|BIT(5); /* master + start */
    I2S0_INT_ENA=BIT(2); KLOGI("AUDIO","I2S0 48kHz stereo 16-bit"); }
static void audio_task(void *arg){ (void)arg;
    i2s_hw_init();
    for(uint32_t i=0;i<DMA_BUFS;i++) mix_fill(i);
    KLOGI("AUDIO","Audio pipeline running");
    while(1){
        if(k_sem_take(&s_rdy,100)!=K_OK){s_underruns++;KLOGW("AUDIO","underrun %u",s_underruns);}
        uint8_t nxt=(s_play_idx+1)%DMA_BUFS;
        mix_fill(nxt); s_play_idx=nxt; } }
void audio_dma_isr(void){ I2S0_INT_CLR=BIT(2); k_sem_give(&s_rdy); }
void audio_init(void){
    memset(&s_st,0,sizeof(s_st)); k_sem_init(&s_rdy,0,DMA_BUFS,"adma"); k_mutex_init(&s_lk,"ast");
    task_create(&s_task,"audio",audio_task,NULL,s_stk,sizeof(s_stk),7);
    KLOGI("AUDIO","Engine ready: %dHz stereo",AUDIO_SR); }
int audio_stream_open(const int16_t *pcm,uint32_t n,uint32_t rate,uint8_t vol,bool loop){
    k_mutex_lock(&s_lk,K_FOREVER);
    for(int i=0;i<(int)MAX_STREAMS;i++){
        if(!s_st[i].active){
            s_st[i].pcm=pcm; s_st[i].total=n; s_st[i].rate=rate; s_st[i].vol=vol;
            s_st[i].loop=loop; s_st[i].phase=0; s_st[i].ratio=(float)rate/AUDIO_SR;
            s_st[i].active=true; s_st[i].paused=false;
            k_mutex_unlock(&s_lk); KLOGI("AUDIO","Stream %d open %dHz",i,rate); return i; } }
    k_mutex_unlock(&s_lk); return -1; }
void audio_stream_pause(int id) {if(id>=0&&id<(int)MAX_STREAMS)s_st[id].paused=true;}
void audio_stream_resume(int id){if(id>=0&&id<(int)MAX_STREAMS)s_st[id].paused=false;}
void audio_stream_close(int id) {if(id>=0&&id<(int)MAX_STREAMS)s_st[id].active=false;}
void audio_set_master_vol(uint8_t v){s_mvol=v;}
uint64_t audio_stream_pts(int id){return (id>=0&&id<(int)MAX_STREAMS)?s_st[id].pts:0;}
void audio_stats_print(void){
    KLOGI("AUDIO","frames=%u underruns=%u lat~%ums",s_frames,s_underruns,(DMA_CHUNK*1000)/AUDIO_SR); }
