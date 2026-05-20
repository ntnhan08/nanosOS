/*
 * NanosOS linker/esp32s3_nanos.ld
 * Custom linker script for ESP32-S3 R8P16
 */
MEMORY {
    IRAM      (rwx) : ORIGIN = 0x40370000, LENGTH = 384K
    DRAM      (rw)  : ORIGIN = 0x3FC88000, LENGTH = 476K
    DMA_POOL  (rw)  : ORIGIN = 0x3FCEF000, LENGTH =   4K
    PSRAM     (rw)  : ORIGIN = 0x3C000000, LENGTH =   8M
    FLASH_TEXT (rx) : ORIGIN = 0x42010000, LENGTH = 1920K
    FLASH_RO  (r)   : ORIGIN = 0x421E0000, LENGTH =  64K
}
ENTRY(_reset_vector)
SECTIONS {
    .iram.vectors : { KEEP(*(.iram.vectors)) } > IRAM
    .iram.text    : {
        . = ALIGN(4);
        *(.iram.text) *(.iram.text.*)
        *scheduler.o(.text*)
        *allocator.o(.text.k_malloc)
        *allocator.o(.text.k_free)
        *framebuffer.o(.text.gui_fill_rect)
        *pipeline.o(.text.mix_fill)
    } > IRAM
    .iram.data    : { . = ALIGN(16); *(.iram.data) *(.iram.data.*) } > IRAM

    _data_lma = LOADADDR(.data);
    .text : {
        . = ALIGN(4); _text_start = .;
        *(.text) *(.text.*) *(.stub) . = ALIGN(4); _text_end = .;
    } > FLASH_TEXT
    .rodata : {
        . = ALIGN(4); *(.rodata) *(.rodata.*) *font_data.o(.rodata*) . = ALIGN(4);
    } > FLASH_RO

    .data : AT(_data_lma) {
        . = ALIGN(16); _data_vma_start = .;
        *(.data) *(.data.*) *(.dram.data) *(.dram.data.*)
        . = ALIGN(4); _data_vma_end = .;
    } > DRAM
    .bss (NOLOAD) : {
        . = ALIGN(16); _bss_start = .;
        *(.bss) *(.bss.*) *(COMMON) . = ALIGN(4); _bss_end = .;
    } > DRAM
    .noinit (NOLOAD) : { . = ALIGN(16); *(.noinit) *(.noinit.*) } > DRAM

    . = ALIGN(16);
    _heap_start = .;
    _heap_end   = ORIGIN(DRAM) + LENGTH(DRAM) - 128;

    .dma_pool (NOLOAD) : { . = ALIGN(4096); *(.dma_pool) } > DMA_POOL

    /DISCARD/ : { *(.xtensa.info) *(.xt.lit) *(.xt.prop) }
}
PROVIDE(_rom_base   = ORIGIN(FLASH_TEXT));
PROVIDE(_psram_base = ORIGIN(PSRAM));
PROVIDE(_psram_size = LENGTH(PSRAM));
PROVIDE(_dram_base  = ORIGIN(DRAM));
PROVIDE(_iram_base  = ORIGIN(IRAM));
ASSERT(_heap_end - _heap_start >= 128K, "Heap too small — check .data/.bss sizes")
