/* NanosOS services/power_manager.c */
#include "../kernel/kernel.h"
typedef enum{PM_FULL=0,PM_BALANCED,PM_SAVER,PM_OFF}pm_t;
static pm_t s_m=PM_FULL;
extern void lcd_set_backlight(uint8_t);
void power_manager_init(void){s_m=PM_FULL;KLOGI("PWR","Power: FULL mode");}
void power_set_mode(pm_t m){s_m=m;const uint8_t bl[]={255,180,80,0};lcd_set_backlight(bl[m<4?m:0]);}
pm_t power_get_mode(void){return s_m;}
void power_set_backlight(uint8_t l){lcd_set_backlight(l);}
