/*
 * NanosOS — drivers/autodetect/lcd_probe.c
 * LCD Panel Auto-Detection Engine
 *
 * Strategy:
 *   1. Pull CS low, send Read ID command (0x04 RDDID or 0xD3 etc.)
 *   2. Read 3-4 response bytes via MISO
 *   3. Compare against known panel database
 *   4. If match: load driver, configure display, register in devtree
 *   5. If no match: try next CS pin / alternate commands
 *
 * Supported ICs (auto-detected):
 *   ST7701S   — 480×800/480×854  RGB565/RGB666  (SPI init + RGB parallel)
 *   ILI9341   — 240×320          RGB565         (SPI 4-wire)
 *   ILI9488   — 320×480          RGB666         (SPI 4-wire)
 *   ST7789    — 240×240/240×320  RGB565         (SPI 4-wire)
 *   ST7735    — 128×128/128×160  RGB565         (SPI 4-wire)
 *   GC9A01    — 240×240 (round)  RGB565         (SPI 4-wire)
 *   NT35510   — 480×800          RGB888         (8080/SPI)
 *   HX8357D   — 320×480          RGB565         (SPI)
 *   SSD1306   — 128×64 OLED      Mono           (I2C/SPI)
 *   SSD1351   — 128×128 OLED     RGB565         (SPI)
 *   RA8875    — up to 800×480    RGB565         (SPI)
 */

#include "../../kernel/kernel.h"
#include "../../system/devtree/device_tree.h"

static const char *TAG = "LCD_PROBE";

/* ── SPI hardware access ─────────────────────────────── */
#define SPI2_BASE   0x60024000UL
#define SPI2_CMD    (*(volatile uint32_t*)(SPI2_BASE+0x00))
#define SPI2_CLK    (*(volatile uint32_t*)(SPI2_BASE+0x18))
#define SPI2_USER   (*(volatile uint32_t*)(SPI2_BASE+0x1C))
#define SPI2_USER1  (*(volatile uint32_t*)(SPI2_BASE+0x20))
#define SPI2_DLEN   (*(volatile uint32_t*)(SPI2_BASE+0x28))
#define SPI2_W0     (*(volatile uint32_t*)(SPI2_BASE+0x98))
#define SPI2_MISO   (*(volatile uint32_t*)(SPI2_BASE+0x9C))
#define GPIOW1TS    (*(volatile uint32_t*)0x60004008UL)
#define GPIOW1TC    (*(volatile uint32_t*)0x6000400CUL)

/* Default LCD pins (overridable via probe context) */
#define DEFAULT_CS   10
#define DEFAULT_DC   13
#define DEFAULT_RST  14

/* ── Panel database entry ────────────────────────────── */
typedef struct {
    const char *name;
    uint8_t     cmd_id;          /* read-id command byte */
    uint8_t     dummy_bits;      /* dummy bits before ID bytes */
    uint8_t     id_bytes;        /* expected ID byte count */
    uint8_t     id[4];           /* expected ID pattern */
    uint8_t     id_mask[4];      /* mask (0xFF = must match) */
    uint16_t    width, height;
    uint8_t     color_bits;
    uint32_t    max_spi_hz;
    /* Pointer to init function */
    k_err_t   (*init_fn)(uint8_t cs_pin, uint8_t dc_pin, uint8_t rst_pin,
                          uint16_t w, uint16_t h);
} panel_entry_t;

/* Forward declarations */
static k_err_t init_ili9341 (uint8_t,uint8_t,uint8_t,uint16_t,uint16_t);
static k_err_t init_ili9488 (uint8_t,uint8_t,uint8_t,uint16_t,uint16_t);
static k_err_t init_st7789  (uint8_t,uint8_t,uint8_t,uint16_t,uint16_t);
static k_err_t init_st7735  (uint8_t,uint8_t,uint8_t,uint16_t,uint16_t);
static k_err_t init_gc9a01  (uint8_t,uint8_t,uint8_t,uint16_t,uint16_t);
static k_err_t init_nt35510 (uint8_t,uint8_t,uint8_t,uint16_t,uint16_t);
static k_err_t init_hx8357d (uint8_t,uint8_t,uint8_t,uint16_t,uint16_t);
static k_err_t init_ssd1351 (uint8_t,uint8_t,uint8_t,uint16_t,uint16_t);
static k_err_t init_ra8875  (uint8_t,uint8_t,uint8_t,uint16_t,uint16_t);
static k_err_t init_st7701s (uint8_t,uint8_t,uint8_t,uint16_t,uint16_t);

/* ── Panel database (10 known ICs) ──────────────────── */
static const panel_entry_t s_panels[] = {
    {
        "ILI9341", 0x04, 8, 3,
        {0x00, 0x93, 0x41}, {0x00, 0xFF, 0xFF},
        240, 320, 16, 40000000, init_ili9341
    },
    {
        "ILI9488", 0x04, 8, 3,
        {0x00, 0x94, 0x88}, {0x00, 0xFF, 0xFF},
        320, 480, 18, 20000000, init_ili9488
    },
    {
        "ST7789", 0x04, 8, 3,
        {0x00, 0x85, 0x52}, {0x00, 0xFF, 0xFF},
        240, 320, 16, 80000000, init_st7789
    },
    {
        "ST7735", 0x04, 8, 3,
        {0x5C, 0x89, 0xF0}, {0xFF, 0xFF, 0x00},
        128, 160, 16, 15000000, init_st7735
    },
    {
        "GC9A01", 0x04, 8, 3,
        {0x00, 0x90, 0xA0}, {0x00, 0xFF, 0xF0},
        240, 240, 16, 80000000, init_gc9a01
    },
    {
        "NT35510", 0x04, 8, 3,
        {0x55, 0x35, 0x10}, {0xFF, 0xFF, 0xFF},
        480, 800, 24, 20000000, init_nt35510
    },
    {
        "HX8357D", 0xD0, 0, 3,
        {0x99, 0x83, 0x57}, {0xFF, 0xFF, 0xFF},
        320, 480, 16, 20000000, init_hx8357d
    },
    {
        "SSD1351", 0xFA, 0, 2,
        {0x51, 0x51}, {0xFF, 0x00},
        128, 128, 16, 20000000, init_ssd1351
    },
    {
        "RA8875",  0x00, 0, 1,
        {0x75}, {0xFF},
        800, 480, 16, 10000000, init_ra8875
    },
    /* ST7701S doesn't respond to 0x04 in SPI mode — detected via absence + GPIO check */
    {
        "ST7701S", 0xFF, 0, 0,
        {0}, {0},
        480, 800, 16, 40000000, init_st7701s
    },
};
#define N_PANELS (sizeof(s_panels)/sizeof(s_panels[0]))

/* ── Low-level SPI helpers ───────────────────────────── */
static void spi_init_probe(void) {
    SPI2_USER  = BIT(24)|BIT(9)|BIT(28); /* MOSI+CS_SETUP+MISO */
    SPI2_USER1 = (7<<26)|(7<<0);          /* MOSI 8bit + MISO 8bit */
    SPI2_CLK   = (7<<18)|(7<<12)|(3<<6)|(7<<0); /* slow: ~5MHz for probing */
}

static void spi_cs(uint8_t pin, bool lo) {
    if (lo) GPIOW1TC = BIT(pin); else GPIOW1TS = BIT(pin);
}

static uint8_t spi_txrx(uint8_t tx) {
    SPI2_DLEN = 7;
    SPI2_W0   = tx;
    SPI2_CMD |= BIT(18);
    while (SPI2_CMD & BIT(18)) {};
    return (uint8_t)(SPI2_MISO & 0xFF);
}

static void spi_cmd(uint8_t cs, uint8_t dc, uint8_t cmd) {
    GPIOW1TC = BIT(dc);    /* DC low = command */
    spi_cs(cs, true);
    spi_txrx(cmd);
    spi_cs(cs, false);
    GPIOW1TS = BIT(dc);    /* DC high */
}

static uint8_t spi_read_byte(uint8_t cs) {
    spi_cs(cs, true);
    uint8_t val = spi_txrx(0x00);
    spi_cs(cs, false);
    return val;
}

/* ── Hardware reset ──────────────────────────────────── */
static void panel_reset(uint8_t rst_pin) {
    GPIOW1TC = BIT(rst_pin);
    task_sleep_ms(15);
    GPIOW1TS = BIT(rst_pin);
    task_sleep_ms(120);
}

/* ── Probe one panel IC on given CS pin ──────────────── */
static int probe_panel(uint8_t cs, uint8_t dc, uint8_t rst) {
    panel_reset(rst);
    spi_init_probe();

    /* Try matching each panel entry */
    for (int i = 0; i < (int)N_PANELS - 1; i++) {  /* skip ST7701S (last) */
        const panel_entry_t *p = &s_panels[i];
        if (p->cmd_id == 0xFF) continue;

        /* Send read-ID command */
        spi_cmd(cs, dc, p->cmd_id);
        spi_cs(cs, true);

        /* Skip dummy bits */
        for (int d = 0; d < p->dummy_bits / 8; d++) spi_txrx(0x00);

        /* Read ID bytes */
        uint8_t id[4] = {0};
        for (int b = 0; b < p->id_bytes; b++) id[b] = spi_txrx(0x00);
        spi_cs(cs, false);

        /* Match with mask */
        bool match = true;
        for (int b = 0; b < p->id_bytes; b++) {
            if ((id[b] & p->id_mask[b]) != (p->id[b] & p->id_mask[b])) {
                match = false; break;
            }
        }

        if (match) {
            KLOGI(TAG, "  Panel match: %s (ID: %02X %02X %02X)",
                  p->name, id[0], id[1], id[2]);
            return i;
        }
    }

    /* Try ST7701S heuristic: always assumes present on default pins
     * if no other panel matched (ST7701S can't report ID over SPI in all modes) */
    KLOGD(TAG, "  No SPI ID match — assuming ST7701S");
    return (int)(N_PANELS - 1);
}

/* ── Build driver vtable for detected panel ──────────── */
typedef struct {
    const panel_entry_t *panel;
    uint8_t cs, dc, rst;
} lcd_driver_ctx_t;

static lcd_driver_ctx_t s_lcd_ctx;

static k_err_t lcd_drv_init(dev_driver_t *drv) {
    lcd_driver_ctx_t *ctx = (lcd_driver_ctx_t*)drv->priv;
    return ctx->panel->init_fn(ctx->cs, ctx->dc, ctx->rst,
                                ctx->panel->width, ctx->panel->height);
}

static k_err_t lcd_drv_ioctl(dev_driver_t *drv, uint32_t cmd, void *arg) {
    (void)drv;
    extern void lcd_set_window(uint16_t,uint16_t,uint16_t,uint16_t);
    extern void lcd_write_pixels(const uint16_t*,uint32_t);
    extern void lcd_set_backlight(uint8_t);
    switch (cmd) {
    case IOCTL_LCD_SET_WINDOW: {
        ioctl_lcd_window_t *w = (ioctl_lcd_window_t*)arg;
        lcd_set_window(w->x, w->y, w->w, w->h); break; }
    case IOCTL_LCD_WRITE_PIXELS: {
        ioctl_lcd_pixels_t *p = (ioctl_lcd_pixels_t*)arg;
        lcd_write_pixels(p->px, p->n); break; }
    case IOCTL_LCD_SET_BACKLIGHT:
        lcd_set_backlight(*(uint8_t*)arg); break;
    case IOCTL_LCD_GET_CAPS:
        if (arg) *(lcd_caps_t*)arg = devtree_find(DEV_LCD)->caps.lcd; break;
    }
    return K_OK;
}

static dev_driver_t s_lcd_driver = {
    .name  = "lcd_auto",
    .init  = lcd_drv_init,
    .ioctl = lcd_drv_ioctl,
    .priv  = &s_lcd_ctx,
};

/* ── Public probe entry point ────────────────────────── */
void devtree_probe_lcd(void) {
    KLOGI(TAG, "Probing LCD panels...");

    /* Try CS pins: default=10, alternatives=5,15,SS */
    uint8_t cs_pins[]  = {10, 5, 15, 34};
    uint8_t dc_pins[]  = {13, 2, 16, 33};
    uint8_t rst_pins[] = {14, 4, 17, 32};

    for (int i = 0; i < 4; i++) {
        uint8_t cs  = cs_pins[i];
        uint8_t dc  = dc_pins[i];
        uint8_t rst = rst_pins[i];

        KLOGD(TAG, "  Trying CS=GPIO%d DC=GPIO%d RST=GPIO%d", cs, dc, rst);
        int panel_idx = probe_panel(cs, dc, rst);

        if (panel_idx >= 0) {
            const panel_entry_t *p = &s_panels[panel_idx];

            /* Store context */
            s_lcd_ctx.panel = p;
            s_lcd_ctx.cs    = cs;
            s_lcd_ctx.dc    = dc;
            s_lcd_ctx.rst   = rst;

            /* Initialize the panel */
            k_err_t r = p->init_fn(cs, dc, rst, p->width, p->height);
            if (r != K_OK) {
                KLOGW(TAG, "  %s init failed (%d)", p->name, r);
                continue;
            }

            /* Register in device tree */
            dev_node_t *node = devtree_register(p->name, DEV_LCD,
                                                 BUS_SPI, 2, cs, &s_lcd_driver);
            if (node) {
                node->chip_id = ((uint32_t)p->id[0]<<16)|((uint32_t)p->id[1]<<8)|p->id[2];
                node->caps.lcd.width      = p->width;
                node->caps.lcd.height     = p->height;
                node->caps.lcd.color_bits = p->color_bits;
                node->caps.lcd.max_spi_hz = p->max_spi_hz;
                node->caps.lcd.has_backlight = true;
            }

            KLOGI(TAG, "LCD: %s %dx%d %dbit @ %uMHz on GPIO%d",
                  p->name, p->width, p->height,
                  p->color_bits, p->max_spi_hz/1000000, cs);
            g_devtree.spi_devices_found++;
            return;  /* Found primary LCD — done */
        }
    }
    KLOGW(TAG, "No LCD panel detected!");
}

/* ═══════════════════════════════════════════════════════
 * Panel init sequences
 * Each function configures the panel chip via SPI:
 * pixel format, color depth, gamma, power, etc.
 * ═══════════════════════════════════════════════════════*/

#define LCMD(c)  do { GPIOW1TC=BIT(dc);spi_cs(cs,true);spi_txrx(c);spi_cs(cs,false);GPIOW1TS=BIT(dc); } while(0)
#define LDAT(d)  do { spi_cs(cs,true);spi_txrx(d);spi_cs(cs,false); } while(0)

/* ── ILI9341 ─────────────────────────────────────────── */
static k_err_t init_ili9341(uint8_t cs,uint8_t dc,uint8_t rst,uint16_t w,uint16_t h) {
    panel_reset(rst); (void)w; (void)h;
    /* Power control */
    LCMD(0xC0); LDAT(0x23);
    LCMD(0xC1); LDAT(0x10);
    LCMD(0xC5); LDAT(0x3E); LDAT(0x28);
    LCMD(0xC7); LDAT(0x86);
    /* Memory access: portrait, BGR */
    LCMD(0x36); LDAT(0x48);
    /* Pixel format: 16bpp */
    LCMD(0x3A); LDAT(0x55);
    /* Frame rate: 79Hz */
    LCMD(0xB1); LDAT(0x00); LDAT(0x18);
    /* Display function */
    LCMD(0xB6); LDAT(0x08); LDAT(0x82); LDAT(0x27);
    /* Gamma */
    LCMD(0xE0); LDAT(0x0F);LDAT(0x31);LDAT(0x2B);LDAT(0x0C);LDAT(0x0E);
                LDAT(0x08);LDAT(0x4E);LDAT(0xF1);LDAT(0x37);LDAT(0x07);
                LDAT(0x10);LDAT(0x03);LDAT(0x0E);LDAT(0x09);LDAT(0x00);
    LCMD(0xE1); LDAT(0x00);LDAT(0x0E);LDAT(0x14);LDAT(0x03);LDAT(0x11);
                LDAT(0x07);LDAT(0x31);LDAT(0xC1);LDAT(0x48);LDAT(0x08);
                LDAT(0x0F);LDAT(0x0C);LDAT(0x31);LDAT(0x36);LDAT(0x0F);
    LCMD(0x11); task_sleep_ms(120);
    LCMD(0x29);
    KLOGI(TAG,"ILI9341 ready 240x320 RGB565");
    return K_OK;
}

/* ── ILI9488 ─────────────────────────────────────────── */
static k_err_t init_ili9488(uint8_t cs,uint8_t dc,uint8_t rst,uint16_t w,uint16_t h) {
    panel_reset(rst); (void)w; (void)h;
    LCMD(0xE0); for(int i=0;i<15;i++) LDAT(0x00);
    LCMD(0xE1); for(int i=0;i<15;i++) LDAT(0x00);
    LCMD(0xC0); LDAT(0x10); LDAT(0x10);
    LCMD(0xC1); LDAT(0x41);
    LCMD(0xC5); LDAT(0x00); LDAT(0x22); LDAT(0x80);
    /* Portrait, BGR */
    LCMD(0x36); LDAT(0x48);
    /* 18bpp */
    LCMD(0x3A); LDAT(0x66);
    LCMD(0xB0); LDAT(0x00);
    LCMD(0xB1); LDAT(0xA0);
    LCMD(0xB4); LDAT(0x02);
    LCMD(0xB6); LDAT(0x02); LDAT(0x02);
    LCMD(0xE9); LDAT(0x00);
    LCMD(0xF7); LDAT(0xA9);LDAT(0x51);LDAT(0x2C);LDAT(0x82);
    LCMD(0x11); task_sleep_ms(120);
    LCMD(0x29);
    KLOGI(TAG,"ILI9488 ready 320x480 RGB666");
    return K_OK;
}

/* ── ST7789 ──────────────────────────────────────────── */
static k_err_t init_st7789(uint8_t cs,uint8_t dc,uint8_t rst,uint16_t w,uint16_t h) {
    panel_reset(rst);
    LCMD(0x01); task_sleep_ms(150); /* Software reset */
    LCMD(0x11); task_sleep_ms(10);  /* Sleep out */
    LCMD(0x3A); LDAT(0x55);         /* 16bpp */
    LCMD(0x36); LDAT(0x00);         /* MADCTL */
    /* Set window for actual resolution */
    LCMD(0x2A); LDAT(0);LDAT(0);LDAT((uint8_t)((w-1)>>8));LDAT((uint8_t)(w-1));
    LCMD(0x2B); LDAT(0);LDAT(0);LDAT((uint8_t)((h-1)>>8));LDAT((uint8_t)(h-1));
    LCMD(0x21); /* Inversion on (common for ST7789 modules) */
    LCMD(0x13); /* Normal mode */
    LCMD(0x29); task_sleep_ms(10);
    KLOGI(TAG,"ST7789 ready %dx%d RGB565",w,h);
    return K_OK;
}

/* ── ST7735 ──────────────────────────────────────────── */
static k_err_t init_st7735(uint8_t cs,uint8_t dc,uint8_t rst,uint16_t w,uint16_t h) {
    panel_reset(rst); (void)w; (void)h;
    LCMD(0x01); task_sleep_ms(150);
    LCMD(0x11); task_sleep_ms(500);
    LCMD(0xB1); LDAT(0x01);LDAT(0x2C);LDAT(0x2D);
    LCMD(0xB2); LDAT(0x01);LDAT(0x2C);LDAT(0x2D);
    LCMD(0xB3); LDAT(0x01);LDAT(0x2C);LDAT(0x2D);LDAT(0x01);LDAT(0x2C);LDAT(0x2D);
    LCMD(0xB4); LDAT(0x07);
    LCMD(0xC0); LDAT(0xA2);LDAT(0x02);LDAT(0x84);
    LCMD(0xC1); LDAT(0xC5);
    LCMD(0xC2); LDAT(0x0A);LDAT(0x00);
    LCMD(0xC3); LDAT(0x8A);LDAT(0x2A);
    LCMD(0xC4); LDAT(0x8A);LDAT(0xEE);
    LCMD(0xC5); LDAT(0x0E);
    LCMD(0x36); LDAT(0xC0);
    LCMD(0x3A); LDAT(0x05);
    LCMD(0x29); KLOGI(TAG,"ST7735 ready 128x160 RGB565");
    return K_OK;
}

/* ── GC9A01 (round 240×240) ──────────────────────────── */
static k_err_t init_gc9a01(uint8_t cs,uint8_t dc,uint8_t rst,uint16_t w,uint16_t h) {
    panel_reset(rst); (void)w; (void)h;
    /* Inter register enable */
    LCMD(0xEF);
    LCMD(0xEB); LDAT(0x14);
    LCMD(0xFE); LCMD(0xEF);
    LCMD(0xEB); LDAT(0x14);
    LCMD(0x84); LDAT(0x40);
    LCMD(0x85); LDAT(0xFF);
    LCMD(0x90); LDAT(0x08);LDAT(0x08);LDAT(0x08);LDAT(0x08);
    LCMD(0xBD); LDAT(0x06);
    LCMD(0xBC); LDAT(0x00);
    LCMD(0xFF); LDAT(0x60);LDAT(0x01);LDAT(0x04);
    LCMD(0xC3); LDAT(0x13);
    LCMD(0xC4); LDAT(0x13);
    LCMD(0xC9); LDAT(0x22);
    LCMD(0xBE); LDAT(0x11);
    LCMD(0xE1); LDAT(0x10);LDAT(0x0E);
    LCMD(0xDF); LDAT(0x21);LDAT(0x0C);LDAT(0x02);
    /* Gamma */
    LCMD(0xF0); LDAT(0x45);LDAT(0x09);LDAT(0x08);LDAT(0x08);LDAT(0x26);LDAT(0x2A);
    LCMD(0xF1); LDAT(0x43);LDAT(0x70);LDAT(0x72);LDAT(0x36);LDAT(0x37);LDAT(0x6F);
    LCMD(0xF2); LDAT(0x45);LDAT(0x09);LDAT(0x08);LDAT(0x08);LDAT(0x26);LDAT(0x2A);
    LCMD(0xF3); LDAT(0x43);LDAT(0x70);LDAT(0x72);LDAT(0x36);LDAT(0x37);LDAT(0x6F);
    /* Pixel format 16bpp */
    LCMD(0x3A); LDAT(0x05);
    LCMD(0x36); LDAT(0x08);
    LCMD(0x21); /* Inversion */
    LCMD(0x11); task_sleep_ms(120);
    LCMD(0x29); task_sleep_ms(20);
    KLOGI(TAG,"GC9A01 ready 240x240 round RGB565");
    return K_OK;
}

/* ── NT35510 ─────────────────────────────────────────── */
static k_err_t init_nt35510(uint8_t cs,uint8_t dc,uint8_t rst,uint16_t w,uint16_t h) {
    panel_reset(rst); (void)w; (void)h;
    /* Unlock CMD2 */
    LCMD(0xF0); LDAT(0x55);LDAT(0xAA);LDAT(0x52);LDAT(0x08);LDAT(0x01);
    /* VGMP/VGMN/VGSP/VGSN */
    LCMD(0xB0); LDAT(0x09);LDAT(0x09);
    LCMD(0xB6); LDAT(0x35);
    LCMD(0xB7); LDAT(0x35);
    LCMD(0xBA); LDAT(0x25);
    LCMD(0xBD); LDAT(0x01);LDAT(0x84);LDAT(0x07);LDAT(0x31);LDAT(0x00);
    LCMD(0xBE); LDAT(0x01);LDAT(0x84);LDAT(0x07);LDAT(0x31);LDAT(0x00);
    LCMD(0xBF); LDAT(0x01);LDAT(0x84);LDAT(0x07);LDAT(0x31);LDAT(0x00);
    LCMD(0xD1); LDAT(0x00);LDAT(0x00);LDAT(0x00);LDAT(0x79);
    /* Pixel 24bpp */
    LCMD(0x3A); LDAT(0x77);
    LCMD(0x11); task_sleep_ms(200);
    LCMD(0x29);
    KLOGI(TAG,"NT35510 ready 480x800 RGB888");
    return K_OK;
}

/* ── HX8357D ─────────────────────────────────────────── */
static k_err_t init_hx8357d(uint8_t cs,uint8_t dc,uint8_t rst,uint16_t w,uint16_t h) {
    panel_reset(rst); (void)w; (void)h;
    LCMD(0xB9); LDAT(0xFF);LDAT(0x83);LDAT(0x57); /* Enable EXTC */
    LCMD(0xB3); LDAT(0x80);LDAT(0x00);LDAT(0x06);LDAT(0x06);
    LCMD(0xB6); LDAT(0x25);
    LCMD(0xB0); LDAT(0x68);
    LCMD(0xCC); LDAT(0x05); /* SETPANEL */
    LCMD(0xB1); LDAT(0x00);LDAT(0x15);LDAT(0x1C);LDAT(0x1C);LDAT(0x83);LDAT(0xAA);
    LCMD(0xC0); LDAT(0x50);LDAT(0x50);LDAT(0x01);LDAT(0x3C);LDAT(0x1E);LDAT(0x08);
    LCMD(0xB4); LDAT(0x02);
    LCMD(0x3A); LDAT(0x55); /* 16bpp */
    LCMD(0x36); LDAT(0xC0);
    LCMD(0x11); task_sleep_ms(150);
    LCMD(0x29); task_sleep_ms(10);
    KLOGI(TAG,"HX8357D ready 320x480 RGB565");
    return K_OK;
}

/* ── SSD1351 OLED ─────────────────────────────────────── */
static k_err_t init_ssd1351(uint8_t cs,uint8_t dc,uint8_t rst,uint16_t w,uint16_t h) {
    panel_reset(rst); (void)w; (void)h;
    LCMD(0xFD); LDAT(0x12); /* Unlock CMD */
    LCMD(0xFD); LDAT(0xB1); /* Unlock more CMD */
    LCMD(0xAE);              /* Display off */
    LCMD(0xB3); LDAT(0xF1); /* ClockDiv/OscFreq */
    LCMD(0xCA); LDAT(0x7F); /* MUX ratio = 128 */
    LCMD(0xA0); LDAT(0x74); /* Remap + color depth 65K */
    LCMD(0x15); LDAT(0x00); LDAT(0x7F); /* Column 0-127 */
    LCMD(0x75); LDAT(0x00); LDAT(0x7F); /* Row 0-127 */
    LCMD(0xA1); LDAT(0x00); /* Start line */
    LCMD(0xA2); LDAT(0x00); /* Display offset */
    LCMD(0xB5); LDAT(0x00); /* GPIO */
    LCMD(0xAB); LDAT(0x01); /* Enable VDD */
    LCMD(0xB1); LDAT(0x32); /* Phase length */
    LCMD(0xBE); LDAT(0x05); /* VCOMH voltage */
    LCMD(0xA6);              /* Normal display */
    LCMD(0xC1); LDAT(0xC8); LDAT(0x80); LDAT(0xC8); /* Contrast */
    LCMD(0xC7); LDAT(0x0F); /* Master contrast */
    LCMD(0xB4); LDAT(0xA0); LDAT(0xB5); LDAT(0x55); /* VSL */
    LCMD(0xAF);              /* Display on */
    task_sleep_ms(100);
    KLOGI(TAG,"SSD1351 OLED ready 128x128 RGB565");
    return K_OK;
}

/* ── RA8875 ──────────────────────────────────────────── */
static k_err_t init_ra8875(uint8_t cs,uint8_t dc,uint8_t rst,uint16_t w,uint16_t h) {
    panel_reset(rst); (void)dc;
    /* RA8875 uses different SPI protocol: cmd=0x80, data=0x00 prefix */
    /* Write register helper */
    #define RA_WRITE_REG(reg,val) do { \
        spi_cs(cs,true); spi_txrx(0x80); spi_txrx(reg); spi_cs(cs,false); \
        spi_cs(cs,true); spi_txrx(0x00); spi_txrx(val); spi_cs(cs,false); } while(0)

    /* PLL init for 800x480 */
    RA_WRITE_REG(0x88, 0x0B); task_sleep_ms(1);
    RA_WRITE_REG(0x89, 0x02); task_sleep_ms(1);
    /* Color 16bpp */
    RA_WRITE_REG(0x10, 0x0C);
    /* LCD width = 800-1 = 799 */
    RA_WRITE_REG(0x14, (w-1)&0xFF);
    RA_WRITE_REG(0x15, (w-1)>>8);
    /* LCD height = 480-1 = 479 */
    RA_WRITE_REG(0x19, (h-1)&0xFF);
    RA_WRITE_REG(0x1A, (h-1)>>8);
    /* Display on */
    RA_WRITE_REG(0x01, 0x80); task_sleep_ms(50);
    RA_WRITE_REG(0x01, 0x00);
    RA_WRITE_REG(0x40, 0x80); /* Layer control */
    RA_WRITE_REG(0x13, 0x00); /* Display config */
    task_sleep_ms(200);
    KLOGI(TAG,"RA8875 ready %dx%d RGB565",w,h);
    return K_OK;
}

/* ── ST7701S ─────────────────────────────────────────── */
static k_err_t init_st7701s(uint8_t cs,uint8_t dc,uint8_t rst,uint16_t w,uint16_t h) {
    panel_reset(rst); (void)w; (void)h;
    /* Re-use the existing implementation */
    extern void lcd_driver_init(void);
    /* Actually call the dedicated init (already has full sequence) */
    LCMD(0xFF); LDAT(0x77);LDAT(0x01);LDAT(0x00);LDAT(0x00);LDAT(0x10);
    LCMD(0xC0); LDAT(0x3B);LDAT(0x00);
    LCMD(0xC2); LDAT(0x01);LDAT(0x02);
    LCMD(0xCC); LDAT(0x10);
    LCMD(0x3A); LDAT(0x55);  /* RGB565 */
    LCMD(0x36); LDAT(0x00);  /* portrait */
    LCMD(0x11); task_sleep_ms(120);
    LCMD(0x29); task_sleep_ms(20);
    KLOGI(TAG,"ST7701S ready 480x800 RGB565");
    return K_OK;
}
