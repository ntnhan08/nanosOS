/*
 * NanosOS — system/devtree/device_tree.h
 * Universal Device Tree & Auto-Detection Framework
 *
 * Architecture:
 *   1. Boot → probe_all_buses()
 *   2. I2C scan all 128 addr → match known device DB
 *   3. SPI probe LCD ID register → match panel DB
 *   4. Each match → instantiate driver → register node
 *   5. Kernel queries device_tree for any capability
 *
 * Supports:
 *   LCD:   ST7701S, ILI9341, ILI9488, ST7789, GC9A01,
 *          NT35510, HX8357D, SSD1306, SSD1351, RA8875
 *   Touch: GT911, GT1151, FT5x36, FT6336, CST816S,
 *          NS2009, XPT2046, STMPE811, AW5306
 *   Audio: ES8388, ES8374, WM8978, PCM5102A, MAX98357A,
 *          AC101, TLV320AIC3104, CS4344
 *   IMU:   MPU6050, MPU9250, BMI160, LSM6DS3
 *   Env:   BMP280, BME280, AHT20, SHT31
 *   Other: USB hubs, BT modules, custom peripherals
 */

#pragma once
#include "../../kernel/kernel.h"

/* ── Bus types ───────────────────────────────────────── */
typedef enum {
    BUS_NONE  = 0,
    BUS_SPI   = 1,
    BUS_I2C   = 2,
    BUS_I2S   = 3,
    BUS_UART  = 4,
    BUS_USB   = 5,
    BUS_GPIO  = 6,
    BUS_1WIRE = 7,
} bus_type_t;

/* ── Device categories ───────────────────────────────── */
typedef enum {
    DEV_LCD       = 0x01,
    DEV_TOUCH     = 0x02,
    DEV_AUDIO_OUT = 0x04,
    DEV_AUDIO_IN  = 0x08,
    DEV_IMU       = 0x10,
    DEV_ENV       = 0x20,
    DEV_RTC       = 0x40,
    DEV_LED       = 0x80,
    DEV_CAMERA    = 0x100,
    DEV_STORAGE   = 0x200,
    DEV_NETWORK   = 0x400,
    DEV_CUSTOM    = 0x800,
} dev_category_t;

/* ── LCD capabilities ────────────────────────────────── */
typedef struct {
    uint16_t width, height;
    uint8_t  color_bits;     /* 16=RGB565, 18=RGB666, 24=RGB888 */
    bool     has_te;         /* tearing effect signal */
    bool     has_backlight;
    uint32_t max_spi_hz;
    uint8_t  interface;      /* 0=SPI, 1=8080, 2=RGB parallel */
} lcd_caps_t;

/* ── Touch capabilities ──────────────────────────────── */
typedef struct {
    uint8_t  max_points;
    uint16_t x_res, y_res;
    bool     has_gesture;
    bool     has_pressure;
} touch_caps_t;

/* ── Audio capabilities ──────────────────────────────── */
typedef struct {
    uint32_t max_sample_rate;
    uint8_t  max_bits;
    uint8_t  max_channels;
    bool     has_mic;
    bool     has_headphone;
    bool     has_speaker;
    bool     has_dac;
    bool     has_adc;
} audio_caps_t;

/* ── Generic device capabilities ────────────────────── */
typedef union {
    lcd_caps_t   lcd;
    touch_caps_t touch;
    audio_caps_t audio;
    uint8_t      raw[32];
} dev_caps_t;

/* ── Driver interface (vtable) ───────────────────────── */
typedef struct dev_driver {
    const char *name;
    k_err_t   (*probe)(struct dev_driver *drv, void *bus_ctx);
    k_err_t   (*init)(struct dev_driver *drv);
    k_err_t   (*deinit)(struct dev_driver *drv);
    k_err_t   (*ioctl)(struct dev_driver *drv, uint32_t cmd, void *arg);
    void      (*isr)(struct dev_driver *drv);
    void      *priv;     /* driver private context */
} dev_driver_t;

/* ── Device node ─────────────────────────────────────── */
#define DEVNODE_NAME_LEN  32
#define MAX_DEVICE_NODES  32

typedef struct dev_node {
    uint32_t        id;
    char            name[DEVNODE_NAME_LEN];
    dev_category_t  category;
    bus_type_t      bus_type;
    uint8_t         bus_num;
    uint32_t        bus_addr;      /* I2C addr or CS GPIO */
    uint32_t        chip_id;       /* read from hardware */
    bool            probed;
    bool            online;
    bool            primary;       /* is this the primary device of its category? */
    dev_caps_t      caps;
    dev_driver_t   *driver;
    struct dev_node *next;
} dev_node_t;

/* ── Device tree state ───────────────────────────────── */
typedef struct {
    dev_node_t  nodes[MAX_DEVICE_NODES];
    uint32_t    node_count;
    k_mutex_t   lock;
    /* Quick-access pointers to primary devices */
    dev_node_t *primary_lcd;
    dev_node_t *primary_touch;
    dev_node_t *primary_audio_out;
    dev_node_t *primary_audio_in;
    /* Probe statistics */
    uint32_t    i2c_devices_found;
    uint32_t    spi_devices_found;
    uint32_t    probe_time_ms;
} device_tree_t;

extern device_tree_t g_devtree;

/* ── Device tree API ─────────────────────────────────── */
void        devtree_init(void);
void        devtree_probe_all(void);          /* Auto-detect everything */
dev_node_t *devtree_register(const char *name, dev_category_t cat,
                              bus_type_t bus, uint8_t bus_num,
                              uint32_t addr, dev_driver_t *drv);
dev_node_t *devtree_find(dev_category_t cat);           /* first match */
dev_node_t *devtree_find_by_name(const char *name);
dev_node_t *devtree_find_all(dev_category_t cat, dev_node_t **list, uint32_t max);
bool        devtree_has(dev_category_t cat);
void        devtree_print(void);              /* log all registered devices */

/* Probe subsystems */
uint32_t    devtree_scan_i2c(uint8_t bus_num);  /* returns device count */
void        devtree_probe_lcd(void);
void        devtree_probe_touch(void);
void        devtree_probe_audio(void);
void        devtree_probe_sensors(void);

/* ioctl commands */
#define IOCTL_LCD_SET_WINDOW    0x1001
#define IOCTL_LCD_WRITE_PIXELS  0x1002
#define IOCTL_LCD_SET_BACKLIGHT 0x1003
#define IOCTL_LCD_GET_CAPS      0x1004
#define IOCTL_LCD_SET_ROTATION  0x1005

#define IOCTL_TOUCH_READ        0x2001
#define IOCTL_TOUCH_CALIBRATE   0x2002
#define IOCTL_TOUCH_GET_CAPS    0x2003

#define IOCTL_AUDIO_SET_VOL     0x3001
#define IOCTL_AUDIO_SET_RATE    0x3002
#define IOCTL_AUDIO_ENABLE      0x3003
#define IOCTL_AUDIO_GET_CAPS    0x3004

/* Unified LCD ioctl args */
typedef struct { uint16_t x,y,w,h; }           ioctl_lcd_window_t;
typedef struct { const uint16_t *px; uint32_t n;}ioctl_lcd_pixels_t;
typedef struct { int16_t x,y; uint8_t pressure; uint8_t event; } touch_point_t;
typedef struct { touch_point_t pts[5]; int count; } ioctl_touch_data_t;
