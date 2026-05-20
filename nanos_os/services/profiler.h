#pragma once
#include "../kernel/kernel.h"
void     profiler_init(void);
uint32_t profiler_begin(const char *tag);
void     profiler_end(uint32_t handle);
void     profiler_print_report(void);
#define PROF_BEGIN(tag)  uint32_t _ph = profiler_begin(tag)
#define PROF_END()       profiler_end(_ph)
