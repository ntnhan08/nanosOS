/* NanosOS system/hal_init.c - Hardware Abstraction Layer */
#include "../kernel/kernel.h"
#define SYSTEM_BASE      0x600C0000UL
#define SYS_PERIP_CLK_EN0 (*(volatile uint32_t*)(SYSTEM_BASE+0x010))
#define SYS_PERIP_RST_EN0 (*(volatile uint32_t*)(SYSTEM_BASE+0x018))
#define SYS_CPU_CONF      (*(volatile uint32_t*)(SYSTEM_BASE+0x06C))
#define RTC_BASE         0x60008000UL
#define RTC_WDT_CFG      (*(volatile uint32_t*)(RTC_BASE+0x090))
#define RTC_WDT_FEED     (*(volatile uint32_t*)(RTC_BASE+0x09C))
#define RTC_WDT_WP       (*(volatile uint32_t*)(RTC_BASE+0x0A0))
#define WDT_KEY          0x50D83AA1UL
#define TIMG0_WDT_CFG    (*(volatile uint32_t*)0x6001F048UL)
#define TIMG0_WDT_WP     (*(volatile uint32_t*)0x6001F064UL)
#define TIMG1_WDT_CFG    (*(volatile uint32_t*)0x60020048UL)
#define TIMG1_WDT_WP     (*(volatile uint32_t*)0x60020064UL)
#define UART0_BASE       0x60000000UL
#define UART0_FIFO       (*(volatile uint32_t*)(UART0_BASE+0x000))
#define UART0_CLKDIV     (*(volatile uint32_t*)(UART0_BASE+0x014))
#define UART0_CONF0      (*(volatile uint32_t*)(UART0_BASE+0x020))
#define UART0_STATUS     (*(volatile uint32_t*)(UART0_BASE+0x01C))
#define IO_MUX_BASE      0x60009000UL
#define IO_MUX_GPIO(n)   (*(volatile uint32_t*)(IO_MUX_BASE+0x004+(n)*4))
#define LEDC_BASE        0x60019000UL
#define LEDC_HSTIMER0    (*(volatile uint32_t*)(LEDC_BASE+0x140))
#define LEDC_HSCH0_CONF0 (*(volatile uint32_t*)(LEDC_BASE+0x000))
#define LEDC_HSCH0_DUTY  (*(volatile uint32_t*)(LEDC_BASE+0x008))
#define LEDC_HSCH0_CONF1 (*(volatile uint32_t*)(LEDC_BASE+0x00C))
#define GPIO_OUT_W1TS    (*(volatile uint32_t*)0x60004008UL)
#define GPIO_OUT_W1TC    (*(volatile uint32_t*)0x6000400CUL)
#define GPIO_ENABLE      (*(volatile uint32_t*)0x60004020UL)
#define PIN_LCD_BL 15

static void wdt_disable(void){
    RTC_WDT_WP=WDT_KEY; RTC_WDT_CFG=0; RTC_WDT_WP=0;
    TIMG0_WDT_WP=WDT_KEY; TIMG0_WDT_CFG=0; TIMG0_WDT_WP=0;
    TIMG1_WDT_WP=WDT_KEY; TIMG1_WDT_CFG=0; TIMG1_WDT_WP=0; }
static void uart0_init(void){
    SYS_PERIP_CLK_EN0|=BIT(2); SYS_PERIP_RST_EN0&=~BIT(2);
    IO_MUX_GPIO(43)=BIT(12)|(2<<10); /* TX func0 */
    IO_MUX_GPIO(44)=BIT(12)|BIT(9)|BIT(8)|(2<<10); /* RX func0, IE, WPU */
    UART0_CLKDIV=694; /* 80MHz/115200 */
    UART0_CONF0=(3<<2); /* 8N1 */ }
static void perip_clocks(void){
    SYS_PERIP_CLK_EN0|=BIT(4)|BIT(7)|BIT(16)|BIT(6)|BIT(11); /* SPI2,I2C0,I2S0,TIMG0,LEDC */
    SYS_PERIP_RST_EN0&=~(BIT(4)|BIT(7)|BIT(16)|BIT(6)|BIT(11)); }
static void backlight_pwm_init(void){
    SYS_PERIP_CLK_EN0|=BIT(11); SYS_PERIP_RST_EN0&=~BIT(11);
    LEDC_HSTIMER0=(10)|(4<<18); /* 10-bit, APB clk */
    LEDC_HSCH0_DUTY=0; LEDC_HSCH0_CONF1=BIT(31); }
void hal_uart_putchar(char c){
    while((UART0_STATUS>>16&0xFF)>=127){}; UART0_FIFO=(uint32_t)c; }
void hal_uart_puts(const char *s){ while(*s) hal_uart_putchar(*s++); }
void hal_set_backlight(uint8_t lv){
    uint32_t d=((uint32_t)lv*1023)/255; LEDC_HSCH0_DUTY=(d<<4); LEDC_HSCH0_CONF1=BIT(31); }
void hal_wdt_kick(void){ RTC_WDT_WP=WDT_KEY; RTC_WDT_FEED=1; RTC_WDT_WP=0; }
void hal_init(void){
    wdt_disable(); SYS_CPU_CONF=BIT(10)|BIT(0);
    perip_clocks(); uart0_init(); backlight_pwm_init();
    KLOGI("HAL","HAL ready: CPU=240MHz APB=80MHz UART=115200"); }
