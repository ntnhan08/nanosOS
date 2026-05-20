/*
 * NanosOS — drivers/autodetect/peripheral_probe.c
 * Generic Peripheral Auto-Detection
 *
 * Detects and registers:
 *   IMU:    MPU6050, MPU9250, BMI160, LSM6DS3, ICM-42688
 *   ENV:    BMP280, BME280, AHT20, SHT31, DHT12
 *   RTC:    DS3231, PCF8563, RV-3028
 *   LED:    IS31FL3741 (matrix), PCA9685 (PWM)
 *   IO_EXP: PCF8574, MCP23017
 *   CAMERA: OV2640, OV5640 (I2C config, CSI stream)
 */

#include "../../kernel/kernel.h"
#include "../../system/devtree/device_tree.h"

static const char *TAG = "PERIPH_PROBE";
extern k_err_t i2c_read_reg8(uint8_t, uint8_t, uint8_t*, uint8_t);

/* ── Peripheral database ─────────────────────────────── */
typedef struct {
    const char    *name;
    uint8_t        addr[3];     /* up to 3 possible I2C addresses */
    uint8_t        who_am_i_reg;
    uint8_t        who_am_i_val;
    uint8_t        who_am_i_mask;
    dev_category_t category;
    const char    *description;
} periph_entry_t;

static const periph_entry_t s_periph_db[] = {
    /* ─── IMU ─── */
    {"MPU6050",  {0x68,0x69,0x00}, 0x75, 0x68, 0xFF, DEV_IMU, "6-axis IMU 1kHz"},
    {"MPU9250",  {0x68,0x69,0x00}, 0x75, 0x71, 0xFF, DEV_IMU, "9-axis IMU 4kHz"},
    {"MPU6886",  {0x68,0x00,0x00}, 0x75, 0x19, 0xFF, DEV_IMU, "6-axis IMU (M5Stack)"},
    {"ICM42688", {0x68,0x69,0x00}, 0x75, 0x47, 0xFF, DEV_IMU, "6-axis IMU 32kHz"},
    {"BMI160",   {0x68,0x69,0x00}, 0x00, 0xD1, 0xFF, DEV_IMU, "6-axis IMU low-power"},
    {"LSM6DS3",  {0x6A,0x6B,0x00}, 0x0F, 0x69, 0xFF, DEV_IMU, "6-axis iNEMO"},
    {"LSM6DSO",  {0x6A,0x6B,0x00}, 0x0F, 0x6C, 0xFF, DEV_IMU, "6-axis iNEMO ultra"},
    {"ADXL345",  {0x53,0x1D,0x00}, 0x00, 0xE5, 0xFF, DEV_IMU, "3-axis accelerometer"},

    /* ─── Environment ─── */
    {"BMP280",   {0x76,0x77,0x00}, 0xD0, 0x58, 0xFF, DEV_ENV, "Pressure+Temp"},
    {"BME280",   {0x76,0x77,0x00}, 0xD0, 0x60, 0xFF, DEV_ENV, "Pressure+Temp+Humidity"},
    {"BME680",   {0x76,0x77,0x00}, 0xD0, 0x61, 0xFF, DEV_ENV, "Pressure+Temp+Hum+Gas"},
    {"AHT20",    {0x38,0x00,0x00}, 0x71, 0x18, 0xF8, DEV_ENV, "Temp+Humidity"},
    {"AHT10",    {0x38,0x00,0x00}, 0x71, 0x08, 0xF8, DEV_ENV, "Temp+Humidity"},
    {"SHT31",    {0x44,0x45,0x00}, 0x00, 0x00, 0x00, DEV_ENV, "Temp+Humidity precise"},
    {"DHT12",    {0x5C,0x00,0x00}, 0x00, 0x00, 0x00, DEV_ENV, "Temp+Humidity basic"},
    {"LPS22HB",  {0x5C,0x5D,0x00}, 0x0F, 0xB1, 0xFF, DEV_ENV, "Pressure 260-1260hPa"},
    {"SHTC3",    {0x70,0x00,0x00}, 0xEF, 0xC8, 0xC8, DEV_ENV, "Temp+Hum low power"},

    /* ─── RTC ─── */
    {"DS3231",   {0x68,0x00,0x00}, 0x00, 0x00, 0x00, DEV_RTC, "RTC ±2ppm TCXO"},
    {"PCF8563",  {0x51,0x00,0x00}, 0x00, 0x00, 0x00, DEV_RTC, "RTC low-power"},
    {"RV3028",   {0x52,0x00,0x00}, 0x28, 0x00, 0x00, DEV_RTC, "RTC ±1ppm extreme"},
    {"DS1307",   {0x68,0x00,0x00}, 0x07, 0x00, 0x00, DEV_RTC, "RTC basic"},

    /* ─── LED / PWM ─── */
    {"PCA9685",  {0x40,0x41,0x70}, 0x00, 0x00, 0x00, DEV_LED, "16-ch PWM 12-bit"},
    {"IS31FL3741",{0x30,0x31,0x32},0x00, 0x00, 0x00, DEV_LED, "LED matrix 39×9"},

    /* ─── IO Expanders ─── */
    {"PCF8574",  {0x20,0x21,0x3F}, 0x00, 0x00, 0x00, DEV_CUSTOM, "8-bit IO expander"},
    {"MCP23017", {0x20,0x21,0x27}, 0x00, 0x40, 0xFE, DEV_CUSTOM, "16-bit IO expander"},

    /* ─── Camera ─── */
    {"OV2640",   {0x30,0x21,0x00}, 0x0A, 0x26, 0xFF, DEV_CAMERA, "2MP 1600x1200"},
    {"OV5640",   {0x3C,0x00,0x00}, 0x300A,0x56,0xFF, DEV_CAMERA, "5MP autofocus"},
};
#define N_PERIPHDB (sizeof(s_periph_db)/sizeof(s_periph_db[0]))

/* ── Scan and match all known peripherals ────────────── */
void devtree_probe_sensors(void) {
    KLOGI(TAG, "Probing sensors and peripherals...");
    uint32_t found = 0;

    for (uint32_t pi = 0; pi < N_PERIPHDB; pi++) {
        const periph_entry_t *pe = &s_periph_db[pi];
        for (int ai = 0; ai < 3; ai++) {
            uint8_t addr = pe->addr[ai];
            if (!addr) break;

            /* Skip addresses that are already claimed (LCD, touch, audio) */
            if (devtree_find_by_name(pe->name)) goto next_entry;

            /* Try to ACK the address */
            uint8_t buf[2] = {0};
            k_err_t r = i2c_read_reg8(addr, pe->who_am_i_reg, buf, 1);
            if (r != K_OK) continue;

            /* Check WHO_AM_I if mask is non-zero */
            bool match = (pe->who_am_i_mask == 0) ||
                         ((buf[0] & pe->who_am_i_mask) ==
                          (pe->who_am_i_val & pe->who_am_i_mask));
            if (!match) continue;

            /* Register in device tree */
            dev_node_t *node = devtree_register(pe->name, pe->category,
                                                 BUS_I2C, 0, addr, NULL);
            if (node) {
                node->chip_id = buf[0];
                KLOGI(TAG, "  Found: %-12s @ 0x%02X — %s",
                      pe->name, addr, pe->description);
                found++;
            }
            goto next_entry;
        }
        next_entry:;
    }
    if (!found) KLOGI(TAG, "  No additional sensors found");
    KLOGI(TAG, "Peripheral scan done: %u extra devices", found);
}

/* ═══════════════════════════════════════════════════════
 * Unified HAL init — replaces all hardcoded drivers
 * Called from system/main.c instead of individual inits
 * ═══════════════════════════════════════════════════════ */

/* Unified LCD write via device tree */
void hal_lcd_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    dev_node_t *n = g_devtree.primary_lcd;
    if (!n || !n->driver) return;
    ioctl_lcd_window_t arg = {x, y, w, h};
    n->driver->ioctl(n->driver, IOCTL_LCD_SET_WINDOW, &arg);
}
void hal_lcd_write_pixels(const uint16_t *px, uint32_t count) {
    dev_node_t *n = g_devtree.primary_lcd;
    if (!n || !n->driver) return;
    ioctl_lcd_pixels_t arg = {px, count};
    n->driver->ioctl(n->driver, IOCTL_LCD_WRITE_PIXELS, &arg);
}
void hal_lcd_set_backlight(uint8_t level) {
    dev_node_t *n = g_devtree.primary_lcd;
    if (!n || !n->driver) return;
    n->driver->ioctl(n->driver, IOCTL_LCD_SET_BACKLIGHT, &level);
}
bool hal_lcd_get_caps(lcd_caps_t *caps) {
    dev_node_t *n = g_devtree.primary_lcd;
    if (!n) return false;
    *caps = n->caps.lcd;
    return true;
}

/* Unified touch read */
int hal_touch_read(ioctl_touch_data_t *out) {
    dev_node_t *n = g_devtree.primary_touch;
    if (!n || !n->driver) { out->count = 0; return 0; }
    n->driver->ioctl(n->driver, IOCTL_TOUCH_READ, out);
    return out->count;
}

/* Unified audio volume */
void hal_audio_set_volume(uint8_t vol) {
    dev_node_t *n = g_devtree.primary_audio_out;
    if (!n || !n->driver) return;
    n->driver->ioctl(n->driver, IOCTL_AUDIO_SET_VOL, &vol);
}

/* ── Autodetect init entry point ─────────────────────── */
void drivers_autodetect_init(void) {
    uint64_t t0 = k_time_ms();
    KLOGI("AUTODET", "Starting hardware auto-detection...");
    KLOGI("AUTODET", "═══════════════════════════════════════════");

    devtree_init();
    devtree_probe_all();  /* probe LCD, touch, audio, sensors */

    uint64_t elapsed = k_time_ms() - t0;
    KLOGI("AUTODET", "═══════════════════════════════════════════");
    KLOGI("AUTODET", "Auto-detection complete in %llums", (unsigned long long)elapsed);
    KLOGI("AUTODET", "  LCD:   %s", devtree_has(DEV_LCD)   ? g_devtree.primary_lcd->name   : "NONE");
    KLOGI("AUTODET", "  Touch: %s", devtree_has(DEV_TOUCH) ? g_devtree.primary_touch->name : "NONE");
    KLOGI("AUTODET", "  Audio: %s", devtree_has(DEV_AUDIO_OUT) ? g_devtree.primary_audio_out->name : "NONE");
    KLOGI("AUTODET", "  Total: %u devices", g_devtree.node_count);

    if (!devtree_has(DEV_LCD)) {
        KLOGE("AUTODET", "NO LCD DETECTED — display will not work!");
    }
}
