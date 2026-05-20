# NanosOS Changelog

## v1.0.0 (2025)

### Core Kernel
- Preemptive priority scheduler (8 levels, O(1) dispatch via CLZ)
- TLSF heap allocator (O(1) alloc/free, < 12.5% fragmentation)
- Slab allocator (4 caches: TCB/Widget/Msg/Small)
- PSRAM allocator (OPI 80MHz, 8MB managed)
- Mutex với priority inheritance
- Counting semaphore (ISR-safe)
- Message queue (lock-free ring buffer)
- Software timers (sorted linked list, 1ms resolution)
- Crash handler: register dump, stack guard check
- Logger: level filter, UART0, ring buffer

### Auto-Detection Framework (NEW v1.0)
- Device tree: 32-slot universal device registry
- LCD probe: 10 panel ICs via SPI RDDID command
  + ST7701S, ILI9341, ILI9488, ST7789, ST7735
  + GC9A01, NT35510, HX8357D, SSD1351, RA8875
- Touch probe: 12 controllers via I2C WHO_AM_I
  + GT911/GT1151, FT5206/5336/6336/6206
  + CST816S/CST820, NS2009, STMPE811, AW5306, TT21100
- Audio probe: 9 codecs via I2C + I2S-only fallback
  + ES8388/ES8374, WM8978/WM8960, AC101, TLV320
  + UDA1380, PCM5102A, MAX98357A
- Sensor probe: 30+ devices (IMU/ENV/RTC/LED/Camera)
- Unified HAL API (hardware-agnostic after probe)

### Display
- RGB565 double-buffered framebuffer (2×750KB PSRAM)
- Dirty-region tracking (16px bands, DMA only changed areas)
- Widget tree: panel, label, button, canvas
- Animation engine: ease-in-out, spring
- 8×16 bitmap ASCII font (96 chars, flash-resident)
- Boot splash với animated progress bar
- 30 FPS minimum

### Audio
- I2S DMA ring buffer (4×1024 samples ping-pong)
- PCM mixer: 4 simultaneous streams, 32-bit accumulator
- Linear resampler: 44.1kHz→48kHz
- Per-stream volume + master volume
- A/V sync timestamps (µs precision)
- Realtime priority (prio=7) → 0 underruns in normal op

### Android Auto
- AOA 2.0 handshake (GET_PROTOCOL, SEND_IDENT, START_ACCESSORY)
- AAP protocol: 8 channels (control, input, video, audio, nav...)
- CRC32 frame validation (PKZIP polynomial)
- Touch event injection (GT911 → AAP CH1)
- Keepalive PING/PONG every 5 seconds
- Auto-reconnect state machine

### USB
- DWC_otg host (Full-Speed 12Mbps, internal PHY)
- 8 host channels (GDMA burst DMA)
- Control + bulk transfers
- USB device enumeration (SET_ADDRESS, GET_DESCRIPTOR)

### Filesystem (NanosFS)
- 4KB block size, 1967 blocks on 8MB partition
- Wear leveling: alloc picks min(erase_count) block
- Journaling: power-safe writes, replay on mount
- LRU block cache: 8 entries × 4KB in PSRAM
- 512 files max, 60-char filenames
- 12 direct blocks + 1 indirect per inode

### Services
- Event loop: deferred callbacks, WDT kick
- Power manager: FULL/BALANCED/SAVER/OFF modes
- Profiler: cycle-accurate (CCOUNT register)
- Wi-Fi stub (Phase 4: OTA, NTP)
- Bluetooth stub (Phase 4: A2DP)

### Tools
- Debug shell: tasks/devs/mem/fs/audio/aap/fps/scan/reset
- Hardware test script (Python, serial protocol)
- Diagnostic tool (esptool + I2C check)
- Partition table generator (pure Python, no ESP-IDF)
- Install scripts: Linux/Mac + Windows

## Planned: v1.1.0
- H.264 software decoder (tiny_h264 port)
- Boot time optimization (<700ms with cached device tree)
- TTF font rasterizer
- OTA update via Wi-Fi
- Bluetooth A2DP full implementation
