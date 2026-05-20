/*
 * NanosOS — drivers/autodetect/audio_probe.c
 * Audio Codec Auto-Detection + Unified Driver
 *
 * Supported codecs:
 *   ES8388   I2C 0x10/0x11  EverSpins  DAC+ADC  stereo  48kHz
 *   ES8374   I2C 0x10       EverSpins  DAC+ADC  stereo  48kHz
 *   WM8978   I2C 0x1A       Wolfson    DAC+ADC  stereo  96kHz
 *   WM8960   I2C 0x1A       Wolfson    DAC+ADC  stereo  48kHz
 *   AC101    I2C 0x1A       X-Powers   DAC+ADC  stereo  48kHz
 *   TLV320   I2C 0x18/0x19  TI         DAC+ADC  stereo  192kHz
 *   CS4344   I2S only       Cirrus     DAC only stereo  192kHz
 *   PCM5102A I2S only       TI         DAC only stereo  384kHz
 *   MAX98357A I2S only      Maxim      DAC+AMP  mono/stereo
 *   UDA1380  I2C 0x18/0x19  Philips    DAC+ADC  stereo  48kHz
 */

#include "../../kernel/kernel.h"
#include "../../system/devtree/device_tree.h"

static const char *TAG = "AUDIO_PROBE";

/* Reuse I2C helpers from touch_probe.c */
extern k_err_t i2c_read_reg8 (uint8_t addr, uint8_t  reg, uint8_t *buf, uint8_t len);
extern k_err_t i2c_read_reg16(uint8_t addr, uint16_t reg, uint8_t *buf, uint8_t len);

static k_err_t i2c_write_reg8(uint8_t addr, uint8_t reg, uint8_t val) {
    /* I2C0 write: START + addr+W + reg + val + STOP */
    extern uint32_t I2C0_COMD_arr[];
    #define I2C0_BASE   0x60013000UL
    #define I2C0_CTR    (*(volatile uint32_t*)(I2C0_BASE+0x004))
    #define I2C0_DATA   (*(volatile uint32_t*)(I2C0_BASE+0x01C))
    #define I2C0_CLRST  (*(volatile uint32_t*)(I2C0_BASE+0x024))
    #define I2C0_INTST  (*(volatile uint32_t*)(I2C0_BASE+0x02C))
    #define I2C0_COMD(n)(*(volatile uint32_t*)(I2C0_BASE+0x058+(n)*4))
    int c=0;
    I2C0_COMD(c++)=0;  I2C0_DATA=(addr<<1)|0; I2C0_COMD(c++)=0x101;
    I2C0_DATA=reg;     I2C0_COMD(c++)=0x101;
    I2C0_DATA=val;     I2C0_COMD(c++)=0x101;
    I2C0_COMD(c++)=0x400;
    I2C0_CLRST=0xFFFF; I2C0_CTR|=BIT(5);
    uint32_t t=20000; while(!(I2C0_INTST&BIT(4))&&--t){} I2C0_CLRST=BIT(4);
    return t?K_OK:K_ERR_TIMEOUT;
}

/* ── Codec database ──────────────────────────────────── */
typedef struct {
    const char *name;
    uint8_t     i2c_addr[2];
    bool        i2c_only;       /* true if no I2C (I2S-only codec) */
    uint8_t     id_reg;
    uint8_t     id_bytes;
    uint8_t     id_val[2];
    uint8_t     id_mask[2];
    uint32_t    max_rate;
    bool        has_adc;
    k_err_t   (*init_fn)(uint8_t addr, uint32_t sample_rate, uint8_t bits);
    k_err_t   (*set_vol_fn)(uint8_t addr, uint8_t vol);
    k_err_t   (*set_rate_fn)(uint8_t addr, uint32_t rate);
} codec_entry_t;

static k_err_t init_es8388   (uint8_t,uint32_t,uint8_t);
static k_err_t init_es8374   (uint8_t,uint32_t,uint8_t);
static k_err_t init_wm8978   (uint8_t,uint32_t,uint8_t);
static k_err_t init_wm8960   (uint8_t,uint32_t,uint8_t);
static k_err_t init_ac101    (uint8_t,uint32_t,uint8_t);
static k_err_t init_tlv320   (uint8_t,uint32_t,uint8_t);
static k_err_t init_uda1380  (uint8_t,uint32_t,uint8_t);
static k_err_t setvol_es8388 (uint8_t,uint8_t);
static k_err_t setvol_wm8978 (uint8_t,uint8_t);
static k_err_t setvol_generic(uint8_t,uint8_t);

static const codec_entry_t s_codecs[] = {
  {"ES8388",  {0x10,0x11},false, 0xFD,2,{0x00,0x88},{0x00,0xFF},
   48000, true,  init_es8388,  setvol_es8388,  NULL},
  {"ES8374",  {0x10,0x00},false, 0xFD,1,{0x74},{0xFF},
   48000, true,  init_es8374,  setvol_es8388,  NULL},
  {"WM8978",  {0x1A,0x00},false, 0x00,0,{0},{0},
   96000, true,  init_wm8978,  setvol_wm8978,  NULL},
  {"WM8960",  {0x1A,0x00},false, 0x00,0,{0},{0},
   48000, true,  init_wm8960,  setvol_wm8978,  NULL},
  {"AC101",   {0x1A,0x00},false, 0x00,2,{0xAC,0x10},{0xFF,0xFF},
   48000, true,  init_ac101,   setvol_generic, NULL},
  {"TLV320",  {0x18,0x19},false, 0x00,1,{0x00},{0x00},
   192000,true,  init_tlv320,  setvol_generic, NULL},
  {"UDA1380", {0x18,0x19},false, 0x00,2,{0x18,0x80},{0xFF,0xF0},
   48000, true,  init_uda1380, setvol_generic, NULL},
  /* I2S-only (always assumed present if no I2C codec found) */
  {"PCM5102A",{0x00,0x00},true,  0,0,{0},{0},
   384000,false, NULL,         NULL,           NULL},
  {"MAX98357A",{0x00,0x00},true, 0,0,{0},{0},
   48000, false, NULL,         NULL,           NULL},
};
#define N_CODECS (sizeof(s_codecs)/sizeof(s_codecs[0]))

/* Active codec state */
static struct {
    const codec_entry_t *codec;
    uint8_t              addr;
    uint32_t             sample_rate;
    uint8_t              bits;
} s_audio_codec;

/* ── Probe I2C codecs ─────────────────────────────────── */
static const codec_entry_t *probe_codec(uint8_t *found_addr) {
    uint8_t probe_addrs[] = {0x10, 0x11, 0x18, 0x19, 0x1A};
    for (uint32_t i = 0; i < sizeof(probe_addrs); i++) {
        uint8_t addr = probe_addrs[i];
        /* Try a simple ACK check first */
        uint8_t dummy = 0;
        k_err_t r = i2c_read_reg8(addr, 0x00, &dummy, 1);
        if (r != K_OK) continue;

        /* Device responded — now identify it */
        for (uint32_t ci = 0; ci < N_CODECS; ci++) {
            const codec_entry_t *c = &s_codecs[ci];
            if (c->i2c_only) continue;
            if (c->i2c_addr[0] != addr && c->i2c_addr[1] != addr) continue;

            /* Read ID register */
            uint8_t id[2] = {0};
            if (c->id_bytes > 0)
                i2c_read_reg8(addr, c->id_reg, id, c->id_bytes);

            bool match = true;
            for (int b = 0; b < c->id_bytes; b++) {
                if (c->id_mask[b] && (id[b] & c->id_mask[b]) != (c->id_val[b] & c->id_mask[b])) {
                    match = false; break;
                }
            }
            if (match || c->id_bytes == 0) {
                KLOGI(TAG, "  Codec: %s @ 0x%02X", c->name, addr);
                *found_addr = addr;
                return c;
            }
        }
        /* Found device but couldn't ID it */
        KLOGD(TAG, "  Unknown I2C device at 0x%02X (audio addr)", addr);
    }
    return NULL;
}

/* ── Driver vtable ───────────────────────────────────── */
static k_err_t audio_drv_init(dev_driver_t *drv) { (void)drv; return K_OK; }
static k_err_t audio_drv_ioctl(dev_driver_t *drv, uint32_t cmd, void *arg) {
    (void)drv;
    if (cmd == IOCTL_AUDIO_SET_VOL && s_audio_codec.codec && s_audio_codec.codec->set_vol_fn)
        return s_audio_codec.codec->set_vol_fn(s_audio_codec.addr, *(uint8_t*)arg);
    if (cmd == IOCTL_AUDIO_GET_CAPS && arg) {
        dev_node_t *n = g_devtree.primary_audio_out;
        if (n) *(audio_caps_t*)arg = n->caps.audio;
    }
    return K_OK;
}
static dev_driver_t s_audio_driver = {
    .name  = "audio_auto",
    .init  = audio_drv_init,
    .ioctl = audio_drv_ioctl,
};

/* ── Public probe entry ──────────────────────────────── */
void devtree_probe_audio(void) {
    KLOGI(TAG, "Probing audio codecs...");
    uint8_t addr = 0;
    const codec_entry_t *codec = probe_codec(&addr);

    if (!codec) {
        /* No I2C codec — assume I2S-only (PCM5102A or MAX98357A) */
        KLOGI(TAG, "  No I2C codec found — assuming I2S DAC (PCM5102A/MAX98357A)");
        codec = &s_codecs[N_CODECS - 2]; /* PCM5102A */
        addr  = 0x00;
    }

    /* Initialize codec if it has an init function */
    uint32_t sr = 48000; uint8_t bits = 16;
    if (codec->init_fn) {
        k_err_t r = codec->init_fn(addr, sr, bits);
        if (r != K_OK) {
            KLOGW(TAG, "%s init returned %d", codec->name, r);
        }
    }

    s_audio_codec.codec       = codec;
    s_audio_codec.addr        = addr;
    s_audio_codec.sample_rate = sr;
    s_audio_codec.bits        = bits;

    dev_node_t *node = devtree_register(codec->name,
                                         codec->has_adc ? DEV_AUDIO_OUT|DEV_AUDIO_IN : DEV_AUDIO_OUT,
                                         addr ? BUS_I2C : BUS_I2S, 0, addr, &s_audio_driver);
    if (node) {
        node->caps.audio.max_sample_rate = codec->max_rate;
        node->caps.audio.max_bits        = 32;
        node->caps.audio.max_channels    = 2;
        node->caps.audio.has_dac         = true;
        node->caps.audio.has_adc         = codec->has_adc;
        node->caps.audio.has_speaker     = true;
    }
    KLOGI(TAG, "Audio: %s ready %ukHz %ubit", codec->name, sr/1000, bits);
}

/* ═══════════════════════════════════════════════════════
 * Codec init sequences
 * ═══════════════════════════════════════════════════════ */

/* ── ES8388 ──────────────────────────────────────────── */
static k_err_t init_es8388(uint8_t addr, uint32_t sr, uint8_t bits) {
    (void)sr; (void)bits;
    i2c_write_reg8(addr, 0x08, 0x00); /* master mode off */
    i2c_write_reg8(addr, 0x04, 0xC0); /* power down DAC */
    i2c_write_reg8(addr, 0x00, 0x35); /* chip ctrl: VMID=50k */
    i2c_write_reg8(addr, 0x01, 0x50); /* ref sel */
    i2c_write_reg8(addr, 0x02, 0x72); /* ADC power */
    i2c_write_reg8(addr, 0x03, 0x59); /* DAC power */
    i2c_write_reg8(addr, 0x0D, 0x06); /* DAC OSR */
    i2c_write_reg8(addr, 0x0E, 0x00); /* DAC no mute */
    i2c_write_reg8(addr, 0x2E, 0x1E); /* LOUT1 volume 0dB */
    i2c_write_reg8(addr, 0x2F, 0x1E); /* ROUT1 volume 0dB */
    i2c_write_reg8(addr, 0x30, 0x1E); /* LOUT2 */
    i2c_write_reg8(addr, 0x31, 0x1E); /* ROUT2 */
    i2c_write_reg8(addr, 0x27, 0xB8); /* LDAC volume */
    i2c_write_reg8(addr, 0x28, 0xB8); /* RDAC volume */
    i2c_write_reg8(addr, 0x29, 0xB8);
    i2c_write_reg8(addr, 0x2A, 0xB8);
    /* I2S format, 16-bit */
    i2c_write_reg8(addr, 0x08, 0x80); /* master mode: BCLK/LRCK output */
    i2c_write_reg8(addr, 0x09, 0x00); /* I2S, 16-bit */
    i2c_write_reg8(addr, 0x0C, 0x0C); /* ADC I2S 16-bit */
    i2c_write_reg8(addr, 0x04, 0x00); /* power up DAC */
    KLOGI(TAG,"ES8388 init OK");
    return K_OK;
}
static k_err_t setvol_es8388(uint8_t addr, uint8_t vol) {
    uint8_t v = (uint8_t)(vol / 4); /* 0-33 range */
    i2c_write_reg8(addr, 0x2E, v);
    i2c_write_reg8(addr, 0x2F, v);
    return K_OK;
}

/* ── ES8374 ──────────────────────────────────────────── */
static k_err_t init_es8374(uint8_t addr, uint32_t sr, uint8_t bits) {
    (void)sr; (void)bits;
    i2c_write_reg8(addr, 0x00, 0x3F); /* chip reset */
    task_sleep_ms(10);
    i2c_write_reg8(addr, 0x00, 0x00);
    i2c_write_reg8(addr, 0x01, 0x10); /* VMID enable */
    i2c_write_reg8(addr, 0x06, 0x34); /* CLK div */
    i2c_write_reg8(addr, 0x0A, 0x08); /* I2S 16-bit */
    i2c_write_reg8(addr, 0x10, 0x0D); /* DAC analog power */
    i2c_write_reg8(addr, 0x11, 0x00); /* DAC unmute */
    i2c_write_reg8(addr, 0x12, 0xC0); /* DAC vol 0dB */
    i2c_write_reg8(addr, 0x14, 0x04); /* HP enable */
    KLOGI(TAG,"ES8374 init OK");
    return K_OK;
}

/* ── WM8978 ──────────────────────────────────────────── */
static k_err_t init_wm8978(uint8_t addr, uint32_t sr, uint8_t bits) {
    (void)sr; (void)bits;
    /* WM89xx uses 16-bit register value (9-bit addr, 9-bit data packed) */
    /* Write via I2C: reg = high 7 bits, data = [bit0 of reg][8 bits data] */
    #define WM_WRITE(r,v) do { uint8_t b[2]={(uint8_t)(((r)<<1)|((v)>>8)),(uint8_t)((v)&0xFF)); \
        i2c_write_reg8(addr,(b[0]),b[1]); } while(0)
    WM_WRITE(0x00, 0x000); /* Software reset */
    task_sleep_ms(10);
    WM_WRITE(0x01, 0x1FF); /* Power: everything on */
    WM_WRITE(0x02, 0x1BF);
    WM_WRITE(0x03, 0x06F);
    WM_WRITE(0x04, 0x010); /* Audio format: I2S 16-bit */
    WM_WRITE(0x06, 0x000); /* Clock: slave */
    WM_WRITE(0x0A, 0x1FF); /* DAC volume L */
    WM_WRITE(0x0B, 0x1FF); /* DAC volume R */
    WM_WRITE(0x34, 0x12B); /* SPKL volume */
    WM_WRITE(0x35, 0x12B); /* SPKR volume */
    WM_WRITE(0x32, 0x112); /* LOUT1 volume */
    WM_WRITE(0x33, 0x112); /* ROUT1 volume */
    KLOGI(TAG,"WM8978 init OK");
    return K_OK;
}
static k_err_t setvol_wm8978(uint8_t addr, uint8_t vol) {
    uint8_t v = (uint8_t)(vol * 255 / 255);
    #define WM_WRITE2(r,v2) do { uint8_t b[2]={(uint8_t)(((r)<<1)|((v2)>>8)),(uint8_t)((v2)&0xFF)); \
        i2c_write_reg8(addr,(b[0]),b[1]); } while(0)
    WM_WRITE2(0x0A, (uint16_t)(0x100 | v));
    WM_WRITE2(0x0B, (uint16_t)(0x100 | v));
    return K_OK;
}

/* ── WM8960 ──────────────────────────────────────────── */
static k_err_t init_wm8960(uint8_t addr, uint32_t sr, uint8_t bits) {
    (void)sr; (void)bits;
    /* Reset */
    i2c_write_reg8(addr, 0x1E, 0x00); /* reg=0x0F reset */
    task_sleep_ms(10);
    /* Power: enable everything */
    i2c_write_reg8(addr, 0x19, 0xFF);
    i2c_write_reg8(addr, 0x1A, 0xFF);
    i2c_write_reg8(addr, 0x2F, 0x0F);
    /* Audio format: I2S, 16-bit */
    i2c_write_reg8(addr, 0x07, 0x02);
    /* DAC volume 0dB */
    i2c_write_reg8(addr, 0x0A, 0xFF);
    i2c_write_reg8(addr, 0x0B, 0xFF);
    /* Speaker output */
    i2c_write_reg8(addr, 0x28, 0x37);
    i2c_write_reg8(addr, 0x29, 0x37);
    KLOGI(TAG,"WM8960 init OK");
    return K_OK;
}

/* ── AC101 ───────────────────────────────────────────── */
static k_err_t init_ac101(uint8_t addr, uint32_t sr, uint8_t bits) {
    (void)sr; (void)bits;
    i2c_write_reg8(addr, 0x00, 0x12); /* reset */
    task_sleep_ms(10);
    i2c_write_reg8(addr, 0x01, 0x14); /* system clock */
    i2c_write_reg8(addr, 0x04, 0x60); /* I2S format */
    i2c_write_reg8(addr, 0x07, 0xFD); /* DAC power on */
    i2c_write_reg8(addr, 0xA0, 0x80); /* speaker on */
    i2c_write_reg8(addr, 0xA1, 0x80);
    i2c_write_reg8(addr, 0x58, 0xA0); /* DAC vol */
    KLOGI(TAG,"AC101 init OK");
    return K_OK;
}

/* ── TLV320AIC3104 ───────────────────────────────────── */
static k_err_t init_tlv320(uint8_t addr, uint32_t sr, uint8_t bits) {
    (void)sr; (void)bits;
    /* Page select: page 0 */
    i2c_write_reg8(addr, 0x00, 0x00);
    /* Software reset */
    i2c_write_reg8(addr, 0x01, 0x80);
    task_sleep_ms(10);
    /* PLL disabled, MCLK→CODEC_CLKIN */
    i2c_write_reg8(addr, 0x02, 0x00);
    /* Data path: I2S 16-bit slave */
    i2c_write_reg8(addr, 0x09, 0x01);
    /* DAC power + routing to HP */
    i2c_write_reg8(addr, 0x25, 0xC0);
    i2c_write_reg8(addr, 0x47, 0x80);
    i2c_write_reg8(addr, 0x51, 0x09);
    /* HP volume 0dB */
    i2c_write_reg8(addr, 0x47, 0x00);
    i2c_write_reg8(addr, 0x48, 0x00);
    KLOGI(TAG,"TLV320 init OK");
    return K_OK;
}

/* ── UDA1380 ─────────────────────────────────────────── */
static k_err_t init_uda1380(uint8_t addr, uint32_t sr, uint8_t bits) {
    (void)sr; (void)bits;
    /* System clock, power on */
    i2c_write_reg8(addr, 0x00, 0xA5); /* EVALCLK */
    i2c_write_reg8(addr, 0x01, 0x03); /* I2S */
    i2c_write_reg8(addr, 0x02, 0x05); /* PWR: everything */
    i2c_write_reg8(addr, 0x10, 0x00); /* Master volume */
    i2c_write_reg8(addr, 0x11, 0x00);
    KLOGI(TAG,"UDA1380 init OK");
    return K_OK;
}

static k_err_t setvol_generic(uint8_t addr, uint8_t vol) {
    (void)addr; (void)vol; return K_OK;
}
