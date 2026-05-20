#pragma once
#include "../kernel/kernel.h"
void audio_init(void);
int  audio_stream_open(const int16_t *pcm,uint32_t samples,uint32_t rate,uint8_t vol,bool loop);
void audio_stream_pause(int id);
void audio_stream_resume(int id);
void audio_stream_close(int id);
void audio_set_master_vol(uint8_t v);
uint64_t audio_stream_pts(int id);
void audio_stats_print(void);
