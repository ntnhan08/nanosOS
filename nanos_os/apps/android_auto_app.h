#pragma once
#include "../kernel/kernel.h"
void android_auto_app_on_connected(void);
void android_auto_app_on_disconnected(void);
void android_auto_app_on_video(const uint8_t *data, uint32_t len, uint64_t pts);
void android_auto_app_on_nav(uint32_t event_type, const uint8_t *payload, uint32_t len);
