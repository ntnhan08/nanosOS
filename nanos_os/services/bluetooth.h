#pragma once
#include "../kernel/kernel.h"
void        bt_init(void);
bool        bt_is_connected(void);
const char *bt_get_name(void);
void        bt_set_name(const char *name);
void        bt_on_a2dp_data(const uint8_t *pcm, uint32_t samples, uint32_t rate);
void        bt_disconnect(void);
