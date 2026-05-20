/* NanosOS — tools/debug_shell.c - UART0 interactive debug console */
#include "../kernel/kernel.h"
#include "../system/devtree/device_tree.h"

extern void hal_uart_putchar(char c);
extern void hal_wdt_kick(void);

static task_tcb_t s_t;
static uint8_t    s_stk[4096] ALIGNED(16);

static void sp(const char *s) { while (*s) hal_uart_putchar(*s++); }

static void sprint_hex(uint32_t v, int digits) {
    char b[12]; b[11] = 0; int i = 10;
    do { b[--i] = "0123456789ABCDEF"[v & 0xF]; v >>= 4; } while (--digits > 0 || v);
    sp(b + i);
}

static void cmd_tasks(void) {
    char b[80];
    snprintf(b, sizeof(b), "  Tasks:%-2u  Switches:%-8u  Tick:%llums\r\n",
             g_kernel.task_count, g_kernel.ctx_switches,
             (unsigned long long)k_time_ms());
    sp(b);
    sp("  ─────────────────────────────────────────────────────\r\n");
    sp("  Pri  Name             State   Stack     CPU\r\n");
    sp("  ─────────────────────────────────────────────────────\r\n");
    static const char *st[] = {"RDY","RUN","BLK","SLP","SUS","DED"};
    for (uint32_t i = 0; i < g_kernel.task_count; i++) {
        task_tcb_t *t = g_kernel.tasks[i];
        if (!t) continue;
        snprintf(b, sizeof(b), "  [%-1u%s] %-16s %-7s %uB\r\n",
                 t->priority, t->priority == 7 ? " RT" : "   ",
                 t->name, st[t->state], t->stack_size);
        sp(b);
    }
}

static void cmd_devs(void) {
    char b[80];
    snprintf(b, sizeof(b), "  Device Tree (%u devices, probe=%ums)\r\n",
             g_devtree.node_count, g_devtree.probe_time_ms);
    sp(b);
    sp("  ─────────────────────────────────────────────────────\r\n");
    sp("  Device         Bus    Addr  Category  Status\r\n");
    sp("  ─────────────────────────────────────────────────────\r\n");
    for (uint32_t i = 0; i < g_devtree.node_count; i++) {
        dev_node_t *n = &g_devtree.nodes[i];
        const char *cat = n->category == DEV_LCD       ? "LCD   " :
                          n->category == DEV_TOUCH      ? "TOUCH " :
                          n->category == DEV_AUDIO_OUT  ? "AUDIO " :
                          n->category == DEV_IMU        ? "IMU   " :
                          n->category == DEV_ENV        ? "ENV   " :
                          n->category == DEV_RTC        ? "RTC   " :
                          n->category == DEV_LED        ? "LED   " : "OTHER ";
        const char *bus = n->bus_type == BUS_SPI  ? "SPI" :
                          n->bus_type == BUS_I2C  ? "I2C" :
                          n->bus_type == BUS_I2S  ? "I2S" : "?  ";
        snprintf(b, sizeof(b), "  %-14s %s%-1d/0x%02X %s %s%s\r\n",
                 n->name, bus, n->bus_num, n->bus_addr, cat,
                 n->online ? "ONLINE" : "OFFLINE",
                 n->primary ? " *" : "  ");
        sp(b);
    }
    sp("\r\n  (* = primary device for category)\r\n");
}

static void cmd_mem(void) {
    mm_stats_print();
    /* Extra: show PSRAM breakdown */
    char b[80];
    snprintf(b, sizeof(b), "  PSRAM: FB=%.1fMB Audio=16KB FSCache=32KB\r\n",
             (480.0f*800*2*2)/(1024*1024));
    sp(b);
}

static void cmd_help(void) {
    sp("\r\n  NanosOS Debug Shell v1.0\r\n");
    sp("  ─────────────────────────────────────────────────────\r\n");
    sp("  tasks   Danh sach tasks dang chay\r\n");
    sp("  devs    Device tree (LCD/Touch/Audio/Sensors)\r\n");
    sp("  mem     RAM va PSRAM usage\r\n");
    sp("  fs      NanosFS statistics\r\n");
    sp("  audio   Audio pipeline stats\r\n");
    sp("  aap     Android Auto stats\r\n");
    sp("  fps     GUI frames per second\r\n");
    sp("  scan    Re-scan I2C bus\r\n");
    sp("  reset   Software reset\r\n");
    sp("  help    Tro giup\r\n");
    sp("  ─────────────────────────────────────────────────────\r\n");
}

static void cmd_scan(void) {
    sp("  Scanning I2C bus...\r\n");
    uint32_t n = devtree_scan_i2c(0);
    char b[48];
    snprintf(b, sizeof(b), "  Found: %u device(s)\r\n", n);
    sp(b);
}

static void shell_task(void *arg) {
    (void)arg;
    char line[80]; int pos = 0;
    sp("\r\n");
    sp("  ╔══════════════════════════════════════════════╗\r\n");
    sp("  ║   NanosOS Debug Shell — type 'help'          ║\r\n");
    sp("  ╚══════════════════════════════════════════════╝\r\n");
    sp("> ");

    while (1) {
        volatile uint32_t *uart_st = (volatile uint32_t *)0x6000001CUL;
        volatile uint32_t *uart_rx = (volatile uint32_t *)0x60000000UL;
        if (!(*uart_st & 0xFF)) { task_sleep_ms(10); continue; }
        char c = (char)(*uart_rx & 0xFF);

        if (c == '\r' || c == '\n') {
            line[pos] = 0; sp("\r\n");
            if      (!strcmp(line, "tasks"))  cmd_tasks();
            else if (!strcmp(line, "devs"))   cmd_devs();
            else if (!strcmp(line, "mem"))    cmd_mem();
            else if (!strcmp(line, "fs"))     nfs_stats_print();
            else if (!strcmp(line, "audio"))  audio_stats_print();
            else if (!strcmp(line, "aap"))    aap_stats_print();
            else if (!strcmp(line, "scan"))   cmd_scan();
            else if (!strcmp(line, "help"))   cmd_help();
            else if (!strcmp(line, "fps")) {
                char b[32]; snprintf(b, 32, "  GUI FPS: %u\r\n", gui_get_fps()); sp(b);
            }
            else if (!strcmp(line, "reset")) {
                sp("  Resetting...\r\n");
                *(volatile uint32_t *)0x60008000 = BIT(31);
            }
            else if (pos > 0) { sp("  Unknown command. Type 'help'\r\n"); }
            pos = 0; sp("> ");
        } else if ((c == 0x7F || c == '\b') && pos > 0) {
            pos--; sp("\b \b");
        } else if (pos < 79) {
            line[pos++] = c; hal_uart_putchar(c);
        }
        hal_wdt_kick();
    }
}

void debug_shell_init(void) {
    task_create(&s_t, "dbg_shell", shell_task, NULL, s_stk, sizeof(s_stk), 1);
    KLOGI("SHELL", "Debug shell ready — UART0 115200");
}
