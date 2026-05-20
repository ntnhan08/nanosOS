#pragma once
#include "../kernel/kernel.h"
void        wifi_init(void);
bool        wifi_connect(const char *ssid, const char *pw);
void        wifi_disconnect(void);
bool        wifi_connected(void);
const char *wifi_ssid(void);
const char *wifi_ip(void);
int8_t      wifi_rssi(void);
