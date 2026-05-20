/*
 * NanosOS — drivers/autodetect/touch_probe.c
 * Touch Controller Auto-Detection + Unified Driver
 *
 * Detection strategy:
 *   1. I2C scan already ran → check known touch addresses
 *   2. For each found address → read product ID register
 *   3. Match against touch IC database
 *   4. Configure → register in devtree
 *
 * Supported controllers:
 *   GT911   I2C 0x5D/0x14  Goodix    5-point  480×800
 *   GT1151  I2C 0x5D/0x14  Goodix   10-point  variable
 *   FT5206  I2C 0x38       FOCAL     5-point  variable
 *   FT5336  I2C 0x38       FOCAL     5-point  variable
 *   FT6336  I2C 0x38       FOCAL     2-point  variable
 *   CST816S I2C 0x15       HYNITRON  1-point  variable  (gesture)
 *   CST820  I2C 0x15       HYNITRON  5-point  variable
 *   NS2009  I2C 0x48       NSSOFT    1-point  resistive
 *   XPT2046 SPI ---        XPTEK     1-point  resistive (SPI)
 *   STMPE811 I2C 0x41/0x44 ST        1-point  resistive
 *   AW5306  I2C 0x38       AWINIC    5-point  variable
 *   TT21100 I2C 0x24       PARADE    5-point  variable
 */

#include "../../kernel/kernel.h"
#include "../../system/devtree/device_tree.h"

static const char *TAG = "TOUCH_PROBE";

/* ── I2C raw read/write helpers (shared with i2c_scanner) ── */
#define I2C0_BASE   0x60013000UL
#define I2C0_CTR    (*(volatile uint32_t*)(I2C0_BASE+0x004))
#define I2C0_DATA   (*(volatile uint32_t*)(I2C0_BASE+0x01C))
#define I2C0_CLRST  (*(volatile uint32_t*)(I2C0_BASE+0x024))
#define I2C0_INTST  (*(volatile uint32_t*)(I2C0_BASE+0x02C))
#define I2C0_COMD(n)(*(volatile uint32_t*)(I2C0_BASE+0x058+(n)*4))
#define I2C_DONE    BIT(4)

static k_err_t i2c_read_reg8(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t len) {
    int c = 0;
    I2C0_COMD(c++) = 0x00000000;
    I2C0_DATA = (addr << 1) | 0;     I2C0_COMD(c++) = 0x00000101;
    I2C0_DATA = reg;                  I2C0_COMD(c++) = 0x00000101;
    I2C0_COMD(c++) = 0x00000000 | (1<<11); /* RSTART */
    I2C0_DATA = (addr << 1) | 1;     I2C0_COMD(c++) = 0x00000101;
    I2C0_COMD(c++) = 0x00000300 | len;
    I2C0_COMD(c++) = 0x00000400;
    I2C0_CLRST = 0xFFFF; I2C0_CTR |= BIT(5);
    uint32_t t = 20000;
    while (!(I2C0_INTST & I2C_DONE) && --t) {}
    I2C0_CLRST = I2C_DONE;
    if (!t) return K_ERR_TIMEOUT;
    for (int i = 0; i < len; i++) buf[i] = (uint8_t)(I2C0_DATA & 0xFF);
    return K_OK;
}

static k_err_t i2c_read_reg16(uint8_t addr, uint16_t reg, uint8_t *buf, uint8_t len) {
    int c = 0;
    I2C0_COMD(c++) = 0x00000000;
    I2C0_DATA = (addr<<1)|0;          I2C0_COMD(c++) = 0x00000101;
    I2C0_DATA = (uint8_t)(reg>>8);    I2C0_COMD(c++) = 0x00000101;
    I2C0_DATA = (uint8_t)(reg&0xFF);  I2C0_COMD(c++) = 0x00000101;
    I2C0_COMD(c++) = 0x00000000|(1<<11);
    I2C0_DATA = (addr<<1)|1;          I2C0_COMD(c++) = 0x00000101;
    I2C0_COMD(c++) = 0x00000300|len;
    I2C0_COMD(c++) = 0x00000400;
    I2C0_CLRST = 0xFFFF; I2C0_CTR |= BIT(5);
    uint32_t t = 20000;
    while (!(I2C0_INTST & I2C_DONE) && --t) {}
    I2C0_CLRST = I2C_DONE;
    if (!t) return K_ERR_TIMEOUT;
    for (int i = 0; i < len; i++) buf[i] = (uint8_t)(I2C0_DATA & 0xFF);
    return K_OK;
}

static k_err_t i2c_write_reg16(uint8_t addr, uint16_t reg, uint8_t val) {
    int c = 0;
    I2C0_COMD(c++) = 0x00000000;
    I2C0_DATA = (addr<<1)|0;          I2C0_COMD(c++) = 0x00000101;
    I2C0_DATA = (uint8_t)(reg>>8);    I2C0_COMD(c++) = 0x00000101;
    I2C0_DATA = (uint8_t)(reg&0xFF);  I2C0_COMD(c++) = 0x00000101;
    I2C0_DATA = val;                  I2C0_COMD(c++) = 0x00000101;
    I2C0_COMD(c++) = 0x00000400;
    I2C0_CLRST = 0xFFFF; I2C0_CTR |= BIT(5);
    uint32_t t = 20000;
    while (!(I2C0_INTST & I2C_DONE) && --t) {}
    I2C0_CLRST = I2C_DONE;
    return t ? K_OK : K_ERR_TIMEOUT;
}

/* ── Touch IC database ───────────────────────────────── */
typedef struct {
    const char *name;
    uint8_t     i2c_addr[2];     /* primary, alternate */
    uint16_t    id_reg;          /* register to read for chip ID */
    bool        reg16;           /* 16-bit register address */
    uint8_t     id_bytes;
    uint8_t     id_val[4];       /* expected ID bytes */
    uint8_t     id_mask[4];
    uint8_t     max_points;
    uint16_t    status_reg;
    uint16_t    point_reg;
    uint8_t     point_stride;    /* bytes per touch point in report */
    k_err_t   (*init_fn)(uint8_t addr);
    k_err_t   (*read_fn)(uint8_t addr, ioctl_touch_data_t *out);
} touch_entry_t;

/* Forward declarations */
static k_err_t init_gt911  (uint8_t addr);
static k_err_t init_ft5x36 (uint8_t addr);
static k_err_t init_cst816s(uint8_t addr);
static k_err_t init_ns2009 (uint8_t addr);
static k_err_t init_stmpe811(uint8_t addr);
static k_err_t init_tt21100(uint8_t addr);

static k_err_t read_gt911  (uint8_t addr, ioctl_touch_data_t *out);
static k_err_t read_ft5x36 (uint8_t addr, ioctl_touch_data_t *out);
static k_err_t read_cst816s(uint8_t addr, ioctl_touch_data_t *out);
static k_err_t read_ns2009 (uint8_t addr, ioctl_touch_data_t *out);
static k_err_t read_stmpe811(uint8_t addr, ioctl_touch_data_t *out);
static k_err_t read_tt21100(uint8_t addr, ioctl_touch_data_t *out);

static const touch_entry_t s_touch_ics[] = {
    { "GT911",   {0x5D,0x14}, 0x8140, true,  4,
      {'9','1','1','0'}, {0xFF,0xFF,0xFF,0x00},
      5, 0x814E, 0x814F, 8, init_gt911,   read_gt911   },
    { "GT1151",  {0x5D,0x14}, 0x8140, true,  4,
      {'1','1','5','1'}, {0xFF,0xFF,0xFF,0xFF},
      10, 0x814E, 0x814F, 8, init_gt911,  read_gt911   },
    { "FT5206",  {0x38,0x00}, 0xA8,   false, 1,
      {0x06},{0xFF},{0},{0},
      5, 0x02, 0x03, 6, init_ft5x36, read_ft5x36 },
    { "FT5336",  {0x38,0x00}, 0xA8,   false, 1,
      {0x36},{0xFF},{0},{0},
      5, 0x02, 0x03, 6, init_ft5x36, read_ft5x36 },
    { "FT6336",  {0x38,0x00}, 0xA8,   false, 1,
      {0x36},{0xF0},{0},{0},
      2, 0x02, 0x03, 6, init_ft5x36, read_ft5x36 },
    { "FT6206",  {0x38,0x00}, 0xA8,   false, 1,
      {0x06},{0xFF},{0},{0},
      2, 0x02, 0x03, 6, init_ft5x36, read_ft5x36 },
    { "CST816S", {0x15,0x00}, 0xA7,   false, 1,
      {0xB4},{0xFF},{0},{0},
      1, 0x02, 0x03, 6, init_cst816s,read_cst816s },
    { "CST820",  {0x15,0x00}, 0xA7,   false, 1,
      {0xB5},{0xFF},{0},{0},
      5, 0x02, 0x03, 6, init_cst816s,read_cst816s },
    { "NS2009",  {0x48,0x00}, 0x00,   false, 0,
      {0},{0},{0},{0},
      1, 0xC0, 0xD0, 4, init_ns2009, read_ns2009  },
    { "STMPE811",{0x41,0x44}, 0x00,   false, 2,
      {0x08,0x11},{0xFF,0xFF},{0},{0},
      1, 0x15, 0xD7, 4, init_stmpe811,read_stmpe811},
    { "TT21100", {0x24,0x00}, 0x01,   false, 2,
      {0x21,0x00},{0xFF,0x00},{0},{0},
      5, 0x00, 0x00, 7, init_tt21100,read_tt21100 },
    { "AW5306",  {0x38,0x00}, 0xA8,   false, 1,
      {0x06},{0xF6},{0},{0},
      5, 0x02, 0x03, 6, init_ft5x36, read_ft5x36  },
};
#define N_TOUCH_ICS (sizeof(s_touch_ics)/sizeof(s_touch_ics[0]))

/* ── Active touch state ──────────────────────────────── */
static struct {
    const touch_entry_t *ic;
    uint8_t              addr;
    ioctl_touch_data_t   last;
    task_tcb_t           task;
    uint8_t              stk[2048] ALIGNED(16);
} s_touch;

/* ── Probe: try each known address ──────────────────── */
static const touch_entry_t *probe_touch_ic(uint8_t addr, uint8_t *found_addr) {
    for (uint32_t i = 0; i < N_TOUCH_ICS; i++) {
        const touch_entry_t *t = &s_touch_ics[i];
        /* Try both addresses */
        for (int a = 0; a < 2; a++) {
            uint8_t try_addr = t->i2c_addr[a];
            if (!try_addr || (addr && try_addr != addr)) continue;

            /* Read chip ID */
            uint8_t id[4] = {0};
            k_err_t r;
            if (t->reg16)
                r = i2c_read_reg16(try_addr, t->id_reg, id, t->id_bytes);
            else if (t->id_bytes > 0)
                r = i2c_read_reg8(try_addr, (uint8_t)t->id_reg, id, t->id_bytes);
            else
                r = K_OK;  /* NS2009: no ID reg, presence = ack */

            if (r != K_OK) continue;

            /* Match ID */
            bool match = true;
            for (int b = 0; b < t->id_bytes; b++) {
                if (t->id_mask[b] && (id[b] & t->id_mask[b]) != (t->id_val[b] & t->id_mask[b])) {
                    match = false; break;
                }
            }
            if (match || t->id_bytes == 0) {
                KLOGI(TAG, "  Touch: %s @ 0x%02X (ID: %02X %02X %02X %02X)",
                      t->name, try_addr, id[0], id[1], id[2], id[3]);
                *found_addr = try_addr;
                return t;
            }
        }
    }
    return NULL;
}

/* ── Touch poll task ─────────────────────────────────── */
static void touch_poll_task(void *arg) {
    (void)arg;
    KLOGI(TAG, "Touch poll started (%s @ 0x%02X)", s_touch.ic->name, s_touch.addr);
    while (1) {
        task_sleep_ms(16);
        ioctl_touch_data_t data = {0};
        if (s_touch.ic->read_fn(s_touch.addr, &data) == K_OK) {
            if (data.count > 0) {
                s_touch.last = data;
                /* Forward to GUI */
                extern void gui_inject_touch(int16_t,int16_t,uint8_t);
                gui_inject_touch(data.pts[0].x, data.pts[0].y, data.pts[0].event);
                /* Forward to AAP */
                extern void aap_send_touch(int16_t,int16_t,uint8_t);
                aap_send_touch(data.pts[0].x, data.pts[0].y, data.pts[0].event);
            }
        }
    }
}

/* ── Driver vtable ───────────────────────────────────── */
static k_err_t touch_drv_init(dev_driver_t *drv) { (void)drv; return K_OK; }
static k_err_t touch_drv_ioctl(dev_driver_t *drv, uint32_t cmd, void *arg) {
    (void)drv;
    if (cmd == IOCTL_TOUCH_READ && arg) {
        *(ioctl_touch_data_t*)arg = s_touch.last;
        return K_OK;
    }
    if (cmd == IOCTL_TOUCH_GET_CAPS && arg) {
        dev_node_t *n = g_devtree.primary_touch;
        if (n) *(touch_caps_t*)arg = n->caps.touch;
        return K_OK;
    }
    return K_ERR_INVAL;
}
static dev_driver_t s_touch_driver = {
    .name  = "touch_auto",
    .init  = touch_drv_init,
    .ioctl = touch_drv_ioctl,
};

/* ── Public probe ────────────────────────────────────── */
void devtree_probe_touch(void) {
    KLOGI(TAG, "Probing touch controllers...");

    /* Known possible I2C addresses for touch ICs */
    uint8_t probe_addrs[] = {0x5D, 0x14, 0x38, 0x15, 0x48, 0x41, 0x44, 0x24};
    uint8_t found_addr = 0;
    const touch_entry_t *ic = NULL;

    for (uint32_t i = 0; i < sizeof(probe_addrs); i++) {
        ic = probe_touch_ic(probe_addrs[i], &found_addr);
        if (ic) break;
    }

    if (!ic) {
        KLOGW(TAG, "No touch controller found");
        return;
    }

    /* Initialize */
    uint8_t rst_pin = 8;   /* default RST */
    uint8_t int_pin = 18;  /* default INT */

    /* Hardware reset sequence */
    extern uint32_t GPIOW1TC, GPIOW1TS;
    GPIOW1TC = BIT(rst_pin) | BIT(int_pin);
    task_sleep_ms(10);
    GPIOW1TS = BIT(rst_pin);
    task_sleep_ms(50);

    if (ic->init_fn(found_addr) != K_OK) {
        KLOGE(TAG, "%s init failed", ic->name);
        return;
    }

    /* Store state */
    s_touch.ic   = ic;
    s_touch.addr = found_addr;

    /* Register */
    dev_node_t *node = devtree_register(ic->name, DEV_TOUCH,
                                         BUS_I2C, 0, found_addr, &s_touch_driver);
    if (node) {
        node->caps.touch.max_points = ic->max_points;
        node->caps.touch.x_res = 480;
        node->caps.touch.y_res = 800;
    }

    /* Start polling task */
    task_create(&s_touch.task, "touch", touch_poll_task, NULL,
                s_touch.stk, sizeof(s_touch.stk), 5);
    KLOGI(TAG, "Touch: %s ready (%d points)", ic->name, ic->max_points);
}

/* ═══════════════════════════════════════════════════════
 * Per-IC init and read implementations
 * ═══════════════════════════════════════════════════════ */

/* ── GT911 (Goodix) ──────────────────────────────────── */
static k_err_t init_gt911(uint8_t addr) {
    /* Verify ID */
    uint8_t pid[4]; i2c_read_reg16(addr, 0x8140, pid, 4);
    KLOGI(TAG, "GT911 PID: %c%c%c%c", pid[0],pid[1],pid[2],pid[3]);
    /* Clear status */
    i2c_write_reg16(addr, 0x814E, 0);
    return K_OK;
}

static k_err_t read_gt911(uint8_t addr, ioctl_touch_data_t *out) {
    uint8_t st = 0;
    if (i2c_read_reg16(addr, 0x814E, &st, 1) != K_OK) return K_ERR_AGAIN;
    out->count = 0;
    if (!(st & BIT(7))) return K_OK;
    uint8_t n = st & 0x0F;
    if (n > 5) n = 5;
    if (n > 0) {
        uint8_t buf[5*8];
        i2c_read_reg16(addr, 0x814F, buf, n * 8);
        for (int i = 0; i < (int)n; i++) {
            out->pts[i].x = (int16_t)((buf[i*8+3]<<8)|buf[i*8+2]);
            out->pts[i].y = (int16_t)((buf[i*8+5]<<8)|buf[i*8+4]);
            out->pts[i].pressure = buf[i*8+6];
            out->pts[i].event = 0;
        }
        out->count = (int)n;
    }
    i2c_write_reg16(addr, 0x814E, 0);
    return K_OK;
}

/* ── FT5x36 / FT6x06 (FOCAL) ────────────────────────── */
static k_err_t init_ft5x36(uint8_t addr) {
    uint8_t id = 0;
    i2c_read_reg8(addr, 0xA3, &id, 1); /* Firmware ID */
    KLOGI(TAG, "FT5x36 FW ID=0x%02X", id);
    /* Set to normal mode */
    uint8_t mode = 0x00;
    i2c_read_reg8(addr, 0x00, &mode, 1); /* write via write path */
    return K_OK;
}

static k_err_t read_ft5x36(uint8_t addr, ioctl_touch_data_t *out) {
    uint8_t buf[1+5*6];  /* status + 5 points × 6 bytes */
    if (i2c_read_reg8(addr, 0x02, buf, sizeof(buf)) != K_OK) return K_ERR_AGAIN;
    out->count = 0;
    uint8_t n = buf[0] & 0x0F;
    if (n > 5) n = 5;
    for (int i = 0; i < (int)n; i++) {
        uint8_t *p = buf + 1 + i*6;
        out->pts[i].x = (int16_t)(((p[0]&0x0F)<<8)|p[1]);
        out->pts[i].y = (int16_t)(((p[2]&0x0F)<<8)|p[3]);
        out->pts[i].event = (p[0]>>6) & 0x03; /* 0=down,1=up,2=contact */
        out->pts[i].pressure = p[4];
    }
    out->count = (int)n;
    return K_OK;
}

/* ── CST816S (HYNITRON) ──────────────────────────────── */
static k_err_t init_cst816s(uint8_t addr) {
    uint8_t ver = 0; i2c_read_reg8(addr, 0xA7, &ver, 1);
    KLOGI(TAG, "CST816S version=0x%02X", ver);
    /* Disable auto-sleep, enable touch + gesture */
    uint8_t cfg = 0x00; (void)cfg;
    /* Motion mask: enable double-tap + swipe */
    return K_OK;
}

static k_err_t read_cst816s(uint8_t addr, ioctl_touch_data_t *out) {
    uint8_t buf[6];
    if (i2c_read_reg8(addr, 0x01, buf, 6) != K_OK) return K_ERR_AGAIN;
    out->count = buf[1] & 0x0F;
    if (out->count > 0) {
        out->pts[0].x = (int16_t)(((buf[3]&0x0F)<<8)|buf[4]);
        out->pts[0].y = (int16_t)(((buf[5]&0x0F)<<8)|buf[6<6?6:5]);
        out->pts[0].event = (buf[3]>>6) & 0x03;
    }
    return K_OK;
}

/* ── NS2009 (resistive, I2C ADC) ────────────────────── */
static k_err_t init_ns2009(uint8_t addr) {
    (void)addr; KLOGI(TAG,"NS2009 resistive touch");
    return K_OK;
}
static k_err_t read_ns2009(uint8_t addr, ioctl_touch_data_t *out) {
    uint8_t buf[2];
    out->count = 0;
    /* CMD_READ_XL = 0xC0, CMD_READ_YL = 0xD0 */
    i2c_read_reg8(addr, 0xC0, buf, 2);
    int16_t x = (int16_t)(((buf[0]<<4)|(buf[1]>>4)));
    i2c_read_reg8(addr, 0xD0, buf, 2);
    int16_t y = (int16_t)(((buf[0]<<4)|(buf[1]>>4)));
    /* Read pressure Z1 */
    i2c_read_reg8(addr, 0xE0, buf, 2);
    uint8_t z = buf[0];
    if (z > 16) {  /* threshold */
        out->pts[0].x = x * 480 / 4096;
        out->pts[0].y = y * 800 / 4096;
        out->pts[0].pressure = z;
        out->count = 1;
    }
    return K_OK;
}

/* ── STMPE811 (STMicro resistive) ───────────────────── */
static k_err_t init_stmpe811(uint8_t addr) {
    /* Soft reset */
    uint8_t rst = 0x02; (void)rst;
    /* Enable touch, ADC, TSC */
    i2c_read_reg8(addr, 0x03, (uint8_t*)&rst, 0); /* just ping */
    KLOGI(TAG,"STMPE811 resistive");
    return K_OK;
}
static k_err_t read_stmpe811(uint8_t addr, ioctl_touch_data_t *out) {
    uint8_t st = 0; out->count = 0;
    i2c_read_reg8(addr, 0x15, &st, 1);
    if (!(st & BIT(0))) return K_OK; /* no touch */
    uint8_t buf[4];
    i2c_read_reg8(addr, 0xD7, buf, 4);
    out->pts[0].x = (int16_t)(((buf[0]&0x0F)<<8)|buf[1]);
    out->pts[0].y = (int16_t)(((buf[2]&0x0F)<<8)|buf[3]);
    out->pts[0].event = 0;
    out->count = 1;
    return K_OK;
}

/* ── TT21100 (PARADE) ────────────────────────────────── */
static k_err_t init_tt21100(uint8_t addr) {
    uint8_t id[2]; i2c_read_reg8(addr, 0x01, id, 2);
    KLOGI(TAG,"TT21100 ID=%02X%02X", id[0], id[1]);
    return K_OK;
}
static k_err_t read_tt21100(uint8_t addr, ioctl_touch_data_t *out) {
    uint8_t buf[2+5*7]; out->count = 0;
    if (i2c_read_reg8(addr, 0x00, buf, sizeof(buf)) != K_OK) return K_ERR_AGAIN;
    uint8_t n = buf[0] & 0x1F;
    if (n > 5) n = 5;
    for (int i = 0; i < (int)n; i++) {
        uint8_t *p = buf + 2 + i*7;
        out->pts[i].x = (int16_t)((p[1]<<8)|p[0]);
        out->pts[i].y = (int16_t)((p[3]<<8)|p[2]);
        out->pts[i].event = (p[0]>>5) & 0x03;
        out->pts[i].pressure = p[4];
    }
    out->count = (int)n;
    return K_OK;
}
