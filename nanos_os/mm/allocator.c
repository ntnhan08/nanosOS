/* NanosOS mm/allocator.c - TLSF Heap + Slab + PSRAM */
#include "../kernel/kernel.h"
#define HEAP_ALIGN 16U
#define BLK_HDR    8U
#define FREE_BIT   BIT(0)
#define SIZE_MASK  (~(uint32_t)3)
#define MAX_FL 19
#define MAX_SL  8
typedef struct blk { uint32_t sz; struct blk *nxt,*prv; } blk_t;
typedef struct { uint8_t *base; size_t tot,used,fre; uint32_t flbmp;
                 uint8_t slbmp[MAX_FL]; blk_t *fl[MAX_FL][MAX_SL]; k_mutex_t lk; } heap_t;
typedef struct { uint8_t *base; size_t sz,used; blk_t *free; k_mutex_t lk; } psheap_t;
typedef struct { uint32_t osz; blk_t *free; uint32_t cnt; k_mutex_t lk; const char *nm; } slab_t;
static DRAM_ATTR heap_t   H;
static DRAM_ATTR psheap_t P;
static DRAM_ATTR slab_t   S[4];
extern uint8_t _heap_start[], _heap_end[];
static const uint32_t SS[4]={256,128,64,32};
static const char *SN[4]={"tcb","wgt","msg","sml"};
static inline size_t bsz(blk_t *b){return b->sz&SIZE_MASK;}
static inline void *pb(blk_t *b){return (uint8_t*)b+BLK_HDR;}
static inline blk_t *bp(void *p){return (blk_t*)((uint8_t*)p-BLK_HDR);}
static void tlsf_map(size_t sz,int *fl,int *sl){
  *fl=31-__builtin_clz((uint32_t)sz); if(*fl<3)*fl=3;
  *sl=(sz>>((*fl)-3))&7; if(*fl>=MAX_FL){*fl=MAX_FL-1;*sl=MAX_SL-1;} }
static void ins(heap_t *h,blk_t *b){ int f,s; tlsf_map(bsz(b),&f,&s);
  b->nxt=h->fl[f][s]; b->prv=NULL; if(b->nxt)b->nxt->prv=b;
  h->fl[f][s]=b; h->flbmp|=BIT(f); h->slbmp[f]|=BIT(s); b->sz|=FREE_BIT; }
static void rem(heap_t *h,blk_t *b){ int f,s; tlsf_map(bsz(b),&f,&s);
  if(b->prv)b->prv->nxt=b->nxt; else h->fl[f][s]=b->nxt;
  if(b->nxt)b->nxt->prv=b->prv;
  if(!h->fl[f][s]){h->slbmp[f]&=~BIT(s);if(!h->slbmp[f])h->flbmp&=~BIT(f);}
  b->sz&=~FREE_BIT; }
static blk_t *find(heap_t *h,size_t sz){ int f,s; tlsf_map(sz,&f,&s);
  uint32_t sm=h->slbmp[f]&~(BIT(s)-1);
  if(!sm){uint32_t fm=h->flbmp&~(BIT(f+1)-1);if(!fm)return NULL;f=__builtin_ctz(fm);sm=h->slbmp[f];}
  s=__builtin_ctz(sm); return h->fl[f][s]; }

void mm_heap_init(void){
  memset(&H,0,sizeof(H)); H.base=_heap_start;
  H.tot=(size_t)(_heap_end-_heap_start); H.fre=H.tot-BLK_HDR*2;
  k_mutex_init(&H.lk,"heap");
  blk_t *m=(blk_t*)(H.base+BLK_HDR); m->sz=(uint32_t)(H.tot-BLK_HDR*2); ins(&H,m);
  KLOGI("MM","Heap %uKB @ %p",(unsigned)(H.tot/1024),(void*)H.base); }

void *k_malloc(size_t sz){
  if(!sz)return NULL; sz=ALIGN_UP(sz+BLK_HDR,HEAP_ALIGN);
  k_mutex_lock(&H.lk,K_FOREVER); blk_t *b=find(&H,sz);
  if(!b){k_mutex_unlock(&H.lk);KLOGW("MM","OOM %u",(unsigned)sz);return NULL;}
  rem(&H,b); size_t r=bsz(b)-sz;
  if(r>=(BLK_HDR+HEAP_ALIGN)){blk_t *rb=(blk_t*)((uint8_t*)b+sz);rb->sz=(uint32_t)r;ins(&H,rb);b->sz=(uint32_t)sz;H.fre-=sz;}
  else H.fre-=bsz(b); H.used+=bsz(b); k_mutex_unlock(&H.lk); return pb(b); }
void *k_calloc(size_t n,size_t s){void *p=k_malloc(n*s);if(p)memset(p,0,n*s);return p;}
void  k_free(void *p){if(!p)return; blk_t *b=bp(p);
  k_mutex_lock(&H.lk,K_FOREVER); H.used-=bsz(b); H.fre+=bsz(b); ins(&H,b); k_mutex_unlock(&H.lk);}
void *k_realloc(void *p,size_t s){if(!p)return k_malloc(s);if(!s){k_free(p);return NULL;}
  void *n=k_malloc(s);if(!n)return NULL;
  memcpy(n,p,MIN(bsz(bp(p))-BLK_HDR,s));k_free(p);return n;}
void *k_dma_alloc(size_t s){return k_malloc(s);}
void  k_dma_free(void *p){k_free(p);}

void mm_psram_init(void){
  memset(&P,0,sizeof(P)); P.base=(uint8_t*)PSRAM_BASE; P.sz=PSRAM_SIZE;
  P.free=(blk_t*)PSRAM_BASE; P.free->sz=(uint32_t)PSRAM_SIZE|FREE_BIT; P.free->nxt=NULL;
  k_mutex_init(&P.lk,"psram"); KLOGI("MM","PSRAM %uMB",(unsigned)(PSRAM_SIZE>>20)); }
void *k_psram_alloc(size_t sz){ sz=ALIGN_UP(sz+BLK_HDR,64);
  k_mutex_lock(&P.lk,K_FOREVER); blk_t **pp=&P.free;
  while(*pp){ if(bsz(*pp)>=sz){
    blk_t *b=*pp; size_t r=bsz(b)-sz;
    if(r>=(BLK_HDR+64)){blk_t *rb=(blk_t*)((uint8_t*)b+sz);rb->sz=(uint32_t)r|FREE_BIT;rb->nxt=b->nxt;*pp=rb;}
    else *pp=b->nxt; b->sz=(uint32_t)sz; P.used+=sz;
    k_mutex_unlock(&P.lk); return pb(b); } pp=&(*pp)->nxt; }
  k_mutex_unlock(&P.lk); KLOGW("MM","PSRAM OOM %u",(unsigned)sz); return NULL; }
void k_psram_free(void *p){if(!p)return; blk_t *b=bp(p);
  k_mutex_lock(&P.lk,K_FOREVER); P.used-=bsz(b); b->sz|=FREE_BIT; b->nxt=P.free; P.free=b; k_mutex_unlock(&P.lk);}

#define SLAB_N 32
void mm_slab_init(void){
  for(int i=0;i<4;i++){
    S[i].osz=SS[i]; S[i].free=NULL; S[i].cnt=0; S[i].nm=SN[i];
    k_mutex_init(&S[i].lk,SN[i]);
    uint8_t *pg=(uint8_t*)k_malloc(SS[i]*SLAB_N); if(!pg){KLOGE("MM","slab OOM");continue;}
    for(int j=0;j<SLAB_N;j++){blk_t *o=(blk_t*)(pg+j*SS[i]);o->nxt=S[i].free;S[i].free=o;}
    S[i].cnt=SLAB_N; KLOGI("MM","Slab[%s] %d×%uB",SN[i],SLAB_N,SS[i]); } }
void *k_slab_alloc(uint32_t id){ if(id>=4)return NULL;
  k_mutex_lock(&S[id].lk,K_FOREVER);
  if(!S[id].free){uint8_t *pg=(uint8_t*)k_malloc(SS[id]*SLAB_N);
    if(pg){for(int j=0;j<SLAB_N;j++){blk_t*o=(blk_t*)(pg+j*SS[id]);o->nxt=S[id].free;S[id].free=o;}S[id].cnt+=SLAB_N;}}
  blk_t *o=S[id].free; if(!o){k_mutex_unlock(&S[id].lk);return NULL;}
  S[id].free=o->nxt; k_mutex_unlock(&S[id].lk); memset(o,0,SS[id]); return o; }
void k_slab_free(uint32_t id,void *p){if(!p||id>=4)return;
  k_mutex_lock(&S[id].lk,K_FOREVER); blk_t *o=(blk_t*)p; o->nxt=S[id].free; S[id].free=o; k_mutex_unlock(&S[id].lk);}
void mm_stats_print(void){
  KLOGI("MM","Heap: used=%uKB free=%uKB",(unsigned)(H.used>>10),(unsigned)(H.fre>>10));
  KLOGI("MM","PSRAM: used=%uKB / %uMB",(unsigned)(P.used>>10),(unsigned)(PSRAM_SIZE>>20)); }
