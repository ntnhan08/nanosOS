/* NanosOS drivers/flash/flash_driver.c */
#include "../../kernel/kernel.h"
extern int rom_spiflash_read(uint32_t,void*,uint32_t);
extern int rom_spiflash_write(uint32_t,const void*,uint32_t);
extern int rom_spiflash_erase_sector(uint32_t);
static k_mutex_t s_lk;
void flash_driver_init(void){k_mutex_init(&s_lk,"flash");KLOGI("FLASH","Flash driver ready (ROM)");}
k_err_t flash_read(uint32_t a,void *b,uint32_t l){
    k_mutex_lock(&s_lk,K_FOREVER);int r=rom_spiflash_read(a,b,l);k_mutex_unlock(&s_lk);return r?K_ERR_AGAIN:K_OK;}
k_err_t flash_write(uint32_t a,const void *b,uint32_t l){
    k_mutex_lock(&s_lk,K_FOREVER);int r=rom_spiflash_write(a,b,l);k_mutex_unlock(&s_lk);return r?K_ERR_AGAIN:K_OK;}
k_err_t flash_erase_sector(uint32_t a){
    k_mutex_lock(&s_lk,K_FOREVER);int r=rom_spiflash_erase_sector(a/4096);k_mutex_unlock(&s_lk);return r?K_ERR_AGAIN:K_OK;}
