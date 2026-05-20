#pragma once
#include "../kernel/kernel.h"
typedef struct {
    void(*on_video_frame)(const uint8_t*,uint32_t,uint64_t);
    void(*on_audio_frame)(const uint8_t*,uint32_t,uint64_t);
    void(*on_nav_event)(uint32_t,const uint8_t*,uint32_t);
    void(*on_connected)(void);
    void(*on_disconnected)(void);
} aap_callbacks_t;
typedef enum { AAP_DISCONNECTED,AAP_AOA_PROBE,AAP_CONNECTED,AAP_VERSION_NEG,
               AAP_SERVICE_DISC,AAP_RUNNING,AAP_ERROR } aap_state_t;
void        aap_transport_init(const aap_callbacks_t *cbs);
void        aap_send_touch(int16_t x,int16_t y,uint8_t action);
aap_state_t aap_get_state(void);
bool        aap_is_running(void);
uint64_t    aap_bytes_rx(void);
uint64_t    aap_bytes_tx(void);
void        aap_stats_print(void);
