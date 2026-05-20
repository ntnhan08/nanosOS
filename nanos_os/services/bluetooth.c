/* NanosOS services/bluetooth.c - Bluetooth A2DP audio sink stub */
#include "../kernel/kernel.h"
static bool   s_bt_conn=false;
static char   s_bt_name[32]="NanosOS HMI";
static int    s_bt_stream=-1;
void bt_init(void){ KLOGI("BT","Bluetooth stub (Phase 4: A2DP)"); }
bool bt_is_connected(void){ return s_bt_conn; }
const char *bt_get_name(void){ return s_bt_name; }
void bt_set_name(const char *n){ strncpy(s_bt_name,n,31); }
void bt_on_a2dp_data(const uint8_t *pcm, uint32_t samples, uint32_t rate){
    if(!s_bt_conn) return;
    extern int audio_stream_open(const int16_t*,uint32_t,uint32_t,uint8_t,bool);
    if(s_bt_stream<0)
        s_bt_stream=audio_stream_open((const int16_t*)pcm,samples,rate,200,false); }
void bt_disconnect(void){
    s_bt_conn=false;
    if(s_bt_stream>=0){ extern void audio_stream_close(int); audio_stream_close(s_bt_stream); s_bt_stream=-1; } }
