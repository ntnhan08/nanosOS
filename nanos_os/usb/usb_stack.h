#pragma once
#include "../kernel/kernel.h"
typedef struct __attribute__((packed)){uint8_t bmRT,bReq;uint16_t wVal,wIdx,wLen;} usb_setup_t;
void    usb_stack_init(void);
bool    usb_device_connected(void);
void    usb_device_reset(void);
k_err_t usb_control_transfer(const usb_setup_t *s,uint8_t *d,uint16_t l,uint32_t tms);
int32_t usb_bulk_write(uint8_t ep,const uint8_t *buf,uint32_t len,uint32_t tms);
int32_t usb_bulk_read(uint8_t ep,uint8_t *buf,uint32_t len,uint32_t tms);
void    usb_host_irq(void);
#define ETIMEDOUT K_ERR_TIMEOUT
