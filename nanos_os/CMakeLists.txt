cmake_minimum_required(VERSION 3.20)
project(NanosOS VERSION 1.0.0 LANGUAGES C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(ARCH_FLAGS
    -mlongcalls -mtext-section-literals -mfix-esp32-psram-cache-issue
    -ffunction-sections -fdata-sections -fno-rtti -fno-exceptions
    -fstrict-volatile-bitfields)
set(OPT_FLAGS  -O2 -ffast-math -funroll-loops -finline-functions)
set(WARN_FLAGS -Wall -Wextra -Wno-unused-parameter -Werror=implicit-function-declaration)
set(DEFS
    -DNANOS_VERSION_MAJOR=1 -DNANOS_VERSION_MINOR=0 -DNANOS_VERSION_PATCH=0
    -DLCD_WIDTH=480 -DLCD_HEIGHT=800 -DESP_PLATFORM -DCONFIG_SPIRAM_SUPPORT=1)

include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/kernel
    ${CMAKE_SOURCE_DIR}/mm
    ${CMAKE_SOURCE_DIR}/gui
    ${CMAKE_SOURCE_DIR}/audio
    ${CMAKE_SOURCE_DIR}/usb
    ${CMAKE_SOURCE_DIR}/fs
    ${CMAKE_SOURCE_DIR}/services
    ${CMAKE_SOURCE_DIR}/system/devtree
    $ENV{IDF_PATH}/components/soc/esp32s3/include
    $ENV{IDF_PATH}/components/hal/include
    $ENV{IDF_PATH}/components/esp_rom/include
    $ENV{IDF_PATH}/components/xtensa/include)

add_executable(nanos_os.elf
    # Boot
    boot/startup.S
    # Kernel
    kernel/scheduler.c
    kernel/logger.c
    kernel/crash_handler.c
    kernel/isr.c
    # Memory
    mm/allocator.c
    # Device Tree + Auto-Detection
    system/devtree/device_tree.c
    drivers/autodetect/lcd_probe.c
    drivers/autodetect/touch_probe.c
    drivers/autodetect/audio_probe.c
    drivers/autodetect/peripheral_probe.c
    # Drivers (low-level)
    drivers/lcd/lcd_driver.c
    drivers/touch/touch_driver.c
    drivers/flash/flash_driver.c
    drivers/i2s/i2s_driver.c
    # GUI
    gui/framebuffer.c
    gui/font_data.c
    gui/splash.c
    # Audio
    audio/pipeline.c
    # USB + Android Auto
    usb/usb_stack.c
    usb/aap_transport.c
    # Filesystem
    fs/nfs.c
    fs/asset_manager.c
    # Services
    services/event_loop.c
    services/power_manager.c
    # System
    system/hal_init.c
    system/main.c
    # Tools
    tools/debug_shell.c)

target_compile_options(nanos_os.elf PRIVATE ${ARCH_FLAGS} ${OPT_FLAGS} ${WARN_FLAGS} ${DEFS})

set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/linker/esp32s3_nanos.ld)
target_link_options(nanos_os.elf PRIVATE
    -T${LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_BINARY_DIR}/nanos_os.map
    -Wl,--print-memory-usage
    -nostdlib
    -L$ENV{IDF_PATH}/components/esp_rom/esp32s3
    -lesp32s3.rom -lesp32s3.rom.newlib -lgcc -lm)
set_target_properties(nanos_os.elf PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(SERIAL_PORT /dev/ttyUSB0 CACHE STRING "Flash serial port")

add_custom_command(TARGET nanos_os.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:nanos_os.elf>
            ${CMAKE_BINARY_DIR}/bin/nanos_os.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:nanos_os.elf>
            ${CMAKE_BINARY_DIR}/bin/nanos_os.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:nanos_os.elf>
    COMMENT "Generating binary output")

add_custom_target(flash
    COMMAND python3 -m esptool --chip esp32s3 --port ${SERIAL_PORT} --baud 921600
        --before default_reset --after hard_reset
        write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB
        0x008000 ${CMAKE_BINARY_DIR}/bin/partition_table.bin
        0x010000 ${CMAKE_BINARY_DIR}/bin/nanos_os.bin
    DEPENDS nanos_os.elf)

add_custom_target(monitor
    COMMAND python3 -m serial.tools.miniterm --raw --filter=colorize ${SERIAL_PORT} 115200)

add_custom_target(gen_partitions
    COMMAND python3 ${CMAKE_SOURCE_DIR}/../nanos_flash_package/tools/gen_partition_table.py
    COMMENT "Generating partition table binary")

add_custom_target(size_report
    COMMAND ${CMAKE_OBJDUMP} -h $<TARGET_FILE:nanos_os.elf> | grep -E "^\s+[0-9]" |
            awk '{printf "  %-25s %8d bytes\\n",$$2,strtonum("0x"$$3)}'
    DEPENDS nanos_os.elf COMMENT "Section sizes")

message(STATUS "╔══════════════════════════════════════════════╗")
message(STATUS "║   NanosOS v1.0.0 — ESP32-S3 R8P16           ║")
message(STATUS "║   Auto-Detection: 10 LCD · 12 Touch · 9 Audio║")
message(STATUS "╚══════════════════════════════════════════════╝")
message(STATUS "  Build:   ./build.sh build")
message(STATUS "  Flash:   ./build.sh flash /dev/ttyUSB0")
message(STATUS "  Monitor: ./build.sh monitor /dev/ttyUSB0")

# Add new service + app sources
target_sources(nanos_os.elf PRIVATE
    services/profiler.c
    services/wifi_stack.c
    services/bluetooth.c
    apps/android_auto_app.c
    apps/media_dashboard.c
    apps/settings_ui.c
    system/bootloader.c)

include_directories(
    ${CMAKE_SOURCE_DIR}/apps
    ${CMAKE_SOURCE_DIR}/services
    ${CMAKE_SOURCE_DIR}/system/devtree)
