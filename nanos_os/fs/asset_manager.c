/* NanosOS fs/asset_manager.c */
#include "../kernel/kernel.h"
#define ASSET_BASE 0xA00000UL
#define ASSET_MAGIC 0x41535354UL
#define CACHE_N 4
typedef struct __attribute__((packed)){char name[48];uint32_t off,sz,comp,crc;}ae_t;
typedef struct __attribute__((packed)){uint32_t magic,cnt,ver,crc;}ahdr_t;
typedef struct{uint32_t idx;uint8_t*data;uint32_t sz;uint64_t lu;bool valid;}ac_t;
static ae_t s_tbl[256]; static uint32_t s_cnt;
static ac_t s_c[CACHE_N]; static k_mutex_t s_lk;
extern k_err_t flash_read(uint32_t,void*,uint32_t);
static int32_t afind(const char*n){for(uint32_t i=0;i<s_cnt;i++)if(!strncmp(s_tbl[i].name,n,48))return(int32_t)i;return -1;}
void asset_manager_init(void){
    k_mutex_init(&s_lk,"asset");
    ahdr_t h; flash_read(ASSET_BASE,&h,sizeof(h));
    if(h.magic!=ASSET_MAGIC){KLOGW("ASSET","No asset partition");return;}
    s_cnt=MIN(h.cnt,256u);flash_read(ASSET_BASE+sizeof(h),s_tbl,s_cnt*sizeof(ae_t));
    KLOGI("ASSET","%u assets",s_cnt);}
const uint8_t *asset_load(const char *name,uint32_t *osz){
    k_mutex_lock(&s_lk,K_FOREVER);
    int32_t idx=afind(name);if(idx<0){k_mutex_unlock(&s_lk);return NULL;}
    for(int i=0;i<CACHE_N;i++)if(s_c[i].valid&&s_c[i].idx==(uint32_t)idx){
        s_c[i].lu=k_time_ms();if(osz)*osz=s_c[i].sz;k_mutex_unlock(&s_lk);return s_c[i].data;}
    int ev=0;uint64_t ot=UINT64_MAX;
    for(int i=0;i<CACHE_N;i++){if(!s_c[i].valid){ev=i;break;}if(s_c[i].lu<ot){ot=s_c[i].lu;ev=i;}}
    if(s_c[ev].data)k_psram_free(s_c[ev].data);
    ae_t*e=&s_tbl[idx];uint8_t*buf=(uint8_t*)k_psram_alloc(e->sz);
    if(!buf){k_mutex_unlock(&s_lk);return NULL;}
    flash_read(ASSET_BASE+e->off,buf,e->sz);
    s_c[ev]=(ac_t){(uint32_t)idx,buf,e->sz,k_time_ms(),true};
    if(osz)*osz=e->sz;k_mutex_unlock(&s_lk);return buf;}
