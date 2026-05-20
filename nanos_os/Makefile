# NanosOS — Makefile shortcut
# Usage: make build | make flash PORT=/dev/ttyUSB0 | make monitor | make clean

PORT ?= /dev/ttyUSB0
BAUD ?= 921600
BUILD_DIR := build

.PHONY: all build flash monitor clean size analyze

all: build

build:
	@./build.sh build

flash: build
	@./build.sh flash $(PORT)

monitor:
	@./build.sh monitor $(PORT)

all-flash: build flash

clean:
	@./build.sh clean

# Size breakdown
size:
	@xtensa-esp32s3-elf-size -A $(BUILD_DIR)/nanos_os.elf 2>/dev/null | \
	  grep -E "^(\.iram|\.text|\.rodata|\.data|\.bss|Total)" | \
	  awk '{printf "  %-30s %8d KB\n", $$1, $$2/1024}'

# Top 20 largest symbols
symbols:
	@xtensa-esp32s3-elf-nm -S --size-sort --demangle $(BUILD_DIR)/nanos_os.elf 2>/dev/null | \
	  tail -20 | awk '{printf "  %8d B  %s\n", strtonum("0x"$$2), $$4}'

# Disassemble ISR
isr-asm:
	@xtensa-esp32s3-elf-objdump -d $(BUILD_DIR)/nanos_os.elf | \
	  grep -A 30 "<sched_tick_isr>"

# Memory map
memmap:
	@cat $(BUILD_DIR)/nanos_os.map | grep -E "^(0x|LOAD)" | head -40

# Static analysis
analyze:
	@cppcheck --enable=all --suppress=missingInclude --error-exitcode=1 \
	  -I kernel -I mm -I gui -I audio -I usb \
	  kernel/ mm/ gui/ audio/ usb/ fs/ system/ 2>&1 | head -40

# Generate partition table
partitions:
	@python3 ../nanos_flash_package/tools/gen_partition_table.py

# Erase filesystem only (keep app)
erase-fs:
	@python3 -m esptool --chip esp32s3 --port $(PORT) \
	  erase_region 0x200000 0x800000
	@echo "NanosFS erased — will reformat on next boot"

# Erase entire flash
erase-all:
	@python3 -m esptool --chip esp32s3 --port $(PORT) erase_flash

# Hardware test
test:
	@python3 ../nanos_flash_package/tools/hardware_test.py $(PORT)

help:
	@echo "NanosOS Build Targets:"
	@echo "  make build          Build firmware"
	@echo "  make flash          Flash to ESP32-S3"
	@echo "  make monitor        Open serial monitor"
	@echo "  make all-flash      Build + flash"
	@echo "  make clean          Remove build directory"
	@echo "  make size           Section size report"
	@echo "  make symbols        Largest symbols"
	@echo "  make analyze        Static analysis (cppcheck)"
	@echo "  make erase-fs       Erase NanosFS partition"
	@echo "  make erase-all      Erase entire 16MB flash"
	@echo "  make test           Hardware validation test"
	@echo "  PORT=/dev/ttyUSB0   Override serial port"
