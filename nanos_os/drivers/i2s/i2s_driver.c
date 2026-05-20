/* NanosOS drivers/i2s/i2s_driver.c - I2S0 master 48kHz stereo */
#include "../../kernel/kernel.h"
#define I2S0_BASE  0x6002D000UL
#define I2S0_TXCF  (*(volatile uint32_t*)(I2S0_BASE+0x00))
#define I2S0_TXCF1 (*(volatile uint32_t*)(I2S0_BASE+0x04))
#define I2S0_CLKM  (*(volatile uint32_t*)(I2S0_BASE+0x20))
#define I2S0_INTST (*(volatile uint32_t*)(I2S0_BASE+0x48))
#define I2S0_INTENA(*(volatile uint32_t*)(I2S0_BASE+0x4C))
#define I2S0_INTCLR(*(volatile uint32_t*)(I2S0_BASE+0x50))
void i2s0_init(uint32_t sample_rate, uint8_t bits){
    (void)sample_rate; (void)bits;
    I2S0_TXCF=BIT(0); I2S0_TXCF=0; /* reset */
    /* AUDIO_PLL → MCLK 12.288MHz (256*48k), BCLK=3.072MHz */
    I2S0_CLKM=(11UL<<12)|(25UL<<6)|(192UL<<0)|BIT(25);
    I2S0_TXCF1=(32<<0)|(16<<5)|BIT(12); /* 32-bit slot,16-bit data,2ch */
    I2S0_TXCF=BIT(4)|BIT(5); /* master+start */
    I2S0_INTENA=BIT(2); /* TX_DSCR_EMPTY */
    KLOGI("I2S","I2S0 48kHz stereo 16-bit master");}
