#pragma once
#include "../../kernel/kernel.h"
void touch_driver_init(void);
int  touch_read(void *pts, int max_points);
