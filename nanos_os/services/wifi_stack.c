/* NanosOS services/wifi_stack.c - Lightweight Wi-Fi status interface */
#include "../kernel/kernel.h"
static bool   s_conn=false;
static char   s_ssid[33]={0};
static char   s_ip[16]={0};
static int8_t s_rssi=-100;
void wifi_init(void){ KLOGI("WIFI","Wi-Fi stub (Phase 4: OTA+NTP)"); }
bool wifi_connect(const char *ssid, const char *pw){
    (void)pw; strncpy(s_ssid,ssid,32);
    KLOGI("WIFI","Connecting to '%s'...",ssid); return false; }
void wifi_on_connected_cb(const char *ip, int8_t rssi){
    s_conn=true; strncpy(s_ip,ip,15); s_rssi=rssi;
    KLOGI("WIFI","Connected: IP=%s RSSI=%d",ip,rssi); }
void wifi_disconnect(void){ s_conn=false; memset(s_ip,0,16); }
bool    wifi_connected(void)  { return s_conn; }
const char *wifi_ssid(void)   { return s_ssid; }
const char *wifi_ip(void)     { return s_ip; }
int8_t  wifi_rssi(void)       { return s_rssi; }
