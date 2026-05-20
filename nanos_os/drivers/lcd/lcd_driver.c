/* NanosOS drivers/lcd/lcd_driver.c - ST7701S 480x800 via SPI2 */
#include "../../kernel/kernel.h"
#define SPI2_BASE  0x60024000UL
#define SPI2_CMD   (*(volatile uint32_t*)(SPI2_BASE+0x00))
#define SPI2_CLK   (*(volatile uint32_t*)(SPI2_BASE+0x18))
#define SPI2_USER  (*(volatile uint32_t*)(SPI2_BASE+0x1C))
#define SPI2_DLEN  (*(volatile uint32_t*)(SPI2_BASE+0x28))
#define SPI2_W0    (*(volatile uint32_t*)(SPI2_BASE+0x98))
#define GPIOW1TS   (*(volatile uint32_t*)0x60004008UL)
#define GPIOW1TC   (*(volatile uint32_t*)0x6000400CUL)
#define PIN_CS 10
#define PIN_DC 13
#define PIN_RST 14
#define CS_LO() GPIOW1TC=BIT(PIN_CS)
#define CS_HI() GPIOW1TS=BIT(PIN_CS)
#define DC_CMD()GPIOW1TC=BIT(PIN_DC)
#define DC_DAT()GPIOW1TS=BIT(PIN_DC)
#define RST_LO()GPIOW1TC=BIT(PIN_RST)
#define RST_HI()GPIOW1TS=BIT(PIN_RST)
extern void hal_set_backlight(uint8_t lv);
static void spi_byte(uint8_t d){
    SPI2_DLEN=7; SPI2_W0=d; SPI2_CMD|=BIT(18);
    while(SPI2_CMD&BIT(18)){}; }
static void lcmd(uint8_t c){CS_LO();DC_CMD();spi_byte(c);CS_HI();}
static void ldat(uint8_t d){CS_LO();DC_DAT();spi_byte(d);CS_HI();}
static void ldat16(uint16_t d){ldat(d>>8);ldat(d&0xFF);}
static void spi2_init(void){
    SPI2_USER=BIT(24)|BIT(9);
    SPI2_CLK=(1<<18)|(1<<12)|(0<<6)|(1<<0); /* 40MHz */}
static void st7701s_init(void){
    RST_LO(); task_sleep_ms(10); RST_HI(); task_sleep_ms(120);
    lcmd(0xFF);ldat(0x77);ldat(0x01);ldat(0x00);ldat(0x00);ldat(0x10);
    lcmd(0xC0);ldat(0x3B);ldat(0x00);
    lcmd(0xC2);ldat(0x01);ldat(0x02);
    lcmd(0xCC);ldat(0x10);
    lcmd(0x3A);ldat(0x55);  /* 16bpp RGB565 */
    lcmd(0x36);ldat(0x00);  /* MADCTL: portrait */
    lcmd(0x35);ldat(0x00);  /* TE on */
    lcmd(0x11); task_sleep_ms(120); /* Sleep out */
    lcmd(0x29); task_sleep_ms(20);  /* Display on */
    KLOGI("LCD","ST7701S ready 480x800 RGB565");}
void lcd_driver_init(void){spi2_init();st7701s_init();hal_set_backlight(180);}
void lcd_set_window(uint16_t x,uint16_t y,uint16_t w,uint16_t h){
    lcmd(0x2A);ldat16(x);ldat16((uint16_t)(x+w-1));
    lcmd(0x2B);ldat16(y);ldat16((uint16_t)(y+h-1));
    lcmd(0x2C);}
void lcd_write_pixels(const uint16_t *px,uint32_t n){
    CS_LO(); DC_DAT();
    uint32_t b=n*2; const uint8_t *p=(const uint8_t*)px;
    while(b>0){
        uint32_t ch=b>32?32:b; SPI2_DLEN=ch*8-1;
        volatile uint32_t *wr=&SPI2_W0;
        for(uint32_t i=0;i<(ch+3)/4;i++){
            uint32_t word=0;
            for(int j=0;j<4&&(i*4+j)<ch;j++) word|=(uint32_t)p[i*4+j]<<(j*8);
            wr[i]=word;}
        SPI2_CMD|=BIT(18); while(SPI2_CMD&BIT(18)){};
        p+=ch; b-=ch;}
    CS_HI();}
void lcd_set_backlight(uint8_t l){hal_set_backlight(l);}
