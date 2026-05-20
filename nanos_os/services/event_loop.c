/* NanosOS services/event_loop.c */
#include "../kernel/kernel.h"
#define MAX_DEF 16
typedef struct{void(*fn)(void*);void*arg;bool used;}def_t;
static def_t s_d[MAX_DEF]; static k_mutex_t s_lk;
void event_loop_init(void){k_mutex_init(&s_lk,"evtlp");memset(s_d,0,sizeof(s_d));}
void event_loop_post(void(*fn)(void*),void*arg){
    k_mutex_lock(&s_lk,K_FOREVER);
    for(int i=0;i<MAX_DEF;i++)if(!s_d[i].used){s_d[i].fn=fn;s_d[i].arg=arg;s_d[i].used=true;break;}
    k_mutex_unlock(&s_lk);}
void event_loop_tick(void){
    k_mutex_lock(&s_lk,K_FOREVER);
    for(int i=0;i<MAX_DEF;i++){if(!s_d[i].used)continue;
        void(*fn)(void*)=s_d[i].fn;void*arg=s_d[i].arg;s_d[i].used=false;
        k_mutex_unlock(&s_lk);fn(arg);k_mutex_lock(&s_lk,K_FOREVER);}
    k_mutex_unlock(&s_lk);hal_wdt_kick();}
