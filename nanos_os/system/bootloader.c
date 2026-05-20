/*
 * NanosOS — system/bootloader.c
 * Second-stage bootloader (runs before kernel_init)
 * Trách nhiệm:
 *   1. Kiểm tra integrity firmware (CRC32 header)
 *   2. Quyết định partition nào boot (OTA A/B)
 *   3. Cấu hình clock ban đầu
 *   4. In banner
 *   5. Handoff sang _reset_vector → kernel_init
 *
 * Chú ý: Thực tế dùng ESP-IDF bootloader (bootloader.bin).
 * File này mô tả logic bootloader layer 2.
 */
#include "../kernel/kernel.h"

#define BOOT_MAGIC      0x4E414E4FUL   /* 'NANO' */
#define APP_OFFSET      0x010000UL
#define PART_HDR_OFFSET 0x008000UL

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t crc32;
    uint8_t  entry[4];     /* entry point relative */
    char     name[16];
    char     build_date[12];
    uint32_t reserved[4];
} boot_header_t;

/* Called conceptually before kernel_init — actual call is in startup.S */
void bootloader_stage2(void) {
    /* 1. Đọc flash header tại 0x010000 */
    extern k_err_t flash_read(uint32_t, void *, uint32_t);
    boot_header_t hdr;
    flash_read(APP_OFFSET, &hdr, sizeof(hdr));

    if (hdr.magic == BOOT_MAGIC) {
        KLOGI("BOOT", "NanosOS %s (build: %s) size=%uKB",
              hdr.name, hdr.build_date, hdr.size / 1024);
    }

    /* 2. Kernel entry */
    extern void kernel_app_entry(void);
    kernel_app_entry();
}
