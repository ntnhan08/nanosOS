/*
 * NanosOS — system/devtree/device_tree.c
 * Device Tree core — registry + probe orchestration
 */
#include "../../kernel/kernel.h"
#include "device_tree.h"

DRAM_ATTR device_tree_t g_devtree;
static const char *TAG = "DEVTREE";

/* ── SPI2 HAL helpers (used by LCD probe) ────────────── */
#define SPI2_BASE  0x60024000UL
#define SPI2_CMD   (*(volatile uint32_t*)(SPI2_BASE+0x00))
#define SPI2_CLK   (*(volatile uint32_t*)(SPI2_BASE+0x18))
#define SPI2_USER  (*(volatile uint32_t*)(SPI2_BASE+0x1C))
#define SPI2_USER1 (*(volatile uint32_t*)(SPI2_BASE+0x20))
#define SPI2_DLEN  (*(volatile uint32_t*)(SPI2_BASE+0x28))
#define SPI2_W0    (*(volatile uint32_t*)(SPI2_BASE+0x98))
#define SPI2_MISC  (*(volatile uint32_t*)(SPI2_BASE+0x34))
#define GPIOW1TS   (*(volatile uint32_t*)0x60004008UL)
#define GPIOW1TC   (*(volatile uint32_t*)0x6000400CUL)
#define GPIO_ENABLE(*(volatile uint32_t*)0x60004020UL)

/* ── I2C0 HAL helpers ────────────────────────────────── */
#define I2C0_BASE  0x60013000UL
#define I2C0_CTR   (*(volatile uint32_t*)(I2C0_BASE+0x004))
#define I2C0_DATA  (*(volatile uint32_t*)(I2C0_BASE+0x01C))
#define I2C0_CLRST (*(volatile uint32_t*)(I2C0_BASE+0x024))
#define I2C0_INTST (*(volatile uint32_t*)(I2C0_BASE+0x02C))
#define I2C0_COMD(n)(*(volatile uint32_t*)(I2C0_BASE+0x058+(n)*4))
#define I2C0_SCLLOW(*(volatile uint32_t*)(I2C0_BASE+0x000))
#define I2C0_SCLHI (*(volatile uint32_t*)(I2C0_BASE+0x038))
#define I2C_TRANS_DONE BIT(4)

/* ── Core init ───────────────────────────────────────── */
void devtree_init(void) {
    memset(&g_devtree, 0, sizeof(g_devtree));
    k_mutex_init(&g_devtree.lock, "devtree");
    KLOGI(TAG, "Device tree initialized (max %d nodes)", MAX_DEVICE_NODES);
}

/* ── Register a device node ──────────────────────────── */
dev_node_t *devtree_register(const char *name, dev_category_t cat,
                              bus_type_t bus, uint8_t bus_num,
                              uint32_t addr, dev_driver_t *drv) {
    k_mutex_lock(&g_devtree.lock, K_FOREVER);
    if (g_devtree.node_count >= MAX_DEVICE_NODES) {
        k_mutex_unlock(&g_devtree.lock);
        KLOGE(TAG, "Device node table full!");
        return NULL;
    }
    dev_node_t *n = &g_devtree.nodes[g_devtree.node_count++];
    n->id       = g_devtree.node_count;
    n->category = cat;
    n->bus_type = bus;
    n->bus_num  = bus_num;
    n->bus_addr = addr;
    n->driver   = drv;
    n->probed   = true;
    n->online   = true;
    strncpy(n->name, name, DEVNODE_NAME_LEN - 1);

    /* Set as primary if first of its category */
    if (cat == DEV_LCD     && !g_devtree.primary_lcd)       { g_devtree.primary_lcd = n; n->primary = true; }
    if (cat == DEV_TOUCH   && !g_devtree.primary_touch)     { g_devtree.primary_touch = n; n->primary = true; }
    if (cat == DEV_AUDIO_OUT && !g_devtree.primary_audio_out){ g_devtree.primary_audio_out = n; n->primary = true; }
    if (cat == DEV_AUDIO_IN && !g_devtree.primary_audio_in) { g_devtree.primary_audio_in = n; n->primary = true; }

    k_mutex_unlock(&g_devtree.lock);
    KLOGI(TAG, "[+] %s on %s%d addr=0x%02X  [%s]",
          name,
          bus == BUS_SPI  ? "SPI"  :
          bus == BUS_I2C  ? "I2C"  :
          bus == BUS_I2S  ? "I2S"  : "?",
          bus_num, addr,
          n->primary ? "PRIMARY" : "secondary");
    return n;
}

/* ── Find devices ────────────────────────────────────── */
dev_node_t *devtree_find(dev_category_t cat) {
    for (uint32_t i = 0; i < g_devtree.node_count; i++)
        if (g_devtree.nodes[i].category == cat && g_devtree.nodes[i].online)
            return &g_devtree.nodes[i];
    return NULL;
}
dev_node_t *devtree_find_by_name(const char *name) {
    for (uint32_t i = 0; i < g_devtree.node_count; i++)
        if (!strcmp(g_devtree.nodes[i].name, name))
            return &g_devtree.nodes[i];
    return NULL;
}
bool devtree_has(dev_category_t cat) { return devtree_find(cat) != NULL; }

/* ── Print device tree ───────────────────────────────── */
void devtree_print(void) {
    KLOGI(TAG, "═══ Device Tree (%u devices) ═══", g_devtree.node_count);
    KLOGI(TAG, "  %-20s %-8s %-5s %-6s %s",
          "Device", "Bus", "Addr", "Cat", "Status");
    KLOGI(TAG, "  %s", "─────────────────────────────────────────────");
    for (uint32_t i = 0; i < g_devtree.node_count; i++) {
        dev_node_t *n = &g_devtree.nodes[i];
        const char *cat = n->category == DEV_LCD       ? "LCD"   :
                          n->category == DEV_TOUCH      ? "TOUCH" :
                          n->category == DEV_AUDIO_OUT  ? "AUDIO" :
                          n->category == DEV_IMU        ? "IMU"   :
                          n->category == DEV_ENV        ? "ENV"   : "?";
        const char *bus = n->bus_type == BUS_SPI  ? "SPI"  :
                          n->bus_type == BUS_I2C  ? "I2C"  :
                          n->bus_type == BUS_I2S  ? "I2S"  : "?";
        KLOGI(TAG, "  %-20s %-5s%-3d 0x%02X   %-6s %s%s",
              n->name, bus, n->bus_num, n->bus_addr, cat,
              n->online ? "ONLINE" : "OFFLINE",
              n->primary ? " *" : "");
    }
    KLOGI(TAG, "  Probe time: %ums | I2C found: %u | SPI found: %u",
          g_devtree.probe_time_ms,
          g_devtree.i2c_devices_found,
          g_devtree.spi_devices_found);
}

/* ── I2C bus scan ────────────────────────────────────── */
uint32_t devtree_scan_i2c(uint8_t bus_num) {
    (void)bus_num;  /* only I2C0 on this platform */
    /* I2C0 must already be initialized */
    I2C0_SCLLOW = 100; I2C0_SCLHI = 97;  /* 400kHz */
    I2C0_CTR = BIT(5);

    uint32_t found = 0;
    uint8_t found_addrs[128]; uint32_t nf = 0;

    KLOGI(TAG, "I2C0 scan: 0x08..0x77");

    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        /* Send START + address byte + STOP, check ACK */
        I2C0_COMD(0) = 0x00000000;          /* START */
        I2C0_DATA    = (uint32_t)((addr << 1) | 0); /* write */
        I2C0_COMD(1) = 0x00000101;          /* WRITE 1 byte, check ACK */
        I2C0_COMD(2) = 0x00000400;          /* STOP */
        I2C0_CLRST  = 0xFFFF;
        I2C0_CTR |= BIT(5);                 /* START transfer */

        /* Wait up to 5ms */
        uint32_t t = 5000;
        while (!(I2C0_INTST & I2C_TRANS_DONE) && --t) {
            __asm__ volatile("nop;nop;nop;nop");
        }
        I2C0_CLRST = I2C_TRANS_DONE;

        /* ACK check: if no NACK error, device responded */
        bool acked = (I2C0_INTST & BIT(9)) == 0;  /* NACK_INT not set */
        if (t > 0 && acked) {
            found_addrs[nf++] = addr;
            found++;
        }
        task_sleep_ms(1);
    }

    /* Log found devices */
    if (found) {
        KLOGI(TAG, "I2C0: %u device(s) found:", found);
        for (uint32_t i = 0; i < nf; i++)
            KLOGI(TAG, "  0x%02X", found_addrs[i]);
    } else {
        KLOGI(TAG, "I2C0: no devices");
    }

    g_devtree.i2c_devices_found += found;
    return found;
}

/* ── Master probe ────────────────────────────────────── */
void devtree_probe_all(void) {
    uint64_t t0 = k_time_ms();
    KLOGI(TAG, "Auto-probing all buses...");

    devtree_scan_i2c(0);
    devtree_probe_lcd();
    devtree_probe_touch();
    devtree_probe_audio();
    devtree_probe_sensors();

    g_devtree.probe_time_ms = (uint32_t)(k_time_ms() - t0);
    devtree_print();
    KLOGI(TAG, "Probe complete in %ums — %u devices online",
          g_devtree.probe_time_ms, g_devtree.node_count);
}
