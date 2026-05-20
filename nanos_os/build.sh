#!/usr/bin/env bash
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD="${DIR}/build"
PORT="${2:-/dev/ttyUSB0}"
info()  { echo -e "\033[34m[INFO]\033[0m $*"; }
ok()    { echo -e "\033[32m[ OK ]\033[0m $*"; }
err()   { echo -e "\033[31m[ERR ]\033[0m $*"; exit 1; }
echo ""
echo "╔══════════════════════════════════════════════╗"
echo "║     NanosOS Build System — ESP32-S3 R8P16    ║"
echo "╚══════════════════════════════════════════════╝"
echo ""
case "${1:-build}" in
  build)
    command -v xtensa-esp32s3-elf-gcc >/dev/null 2>&1 || err "Xtensa toolchain not found. Run: . \$IDF_PATH/export.sh"
    info "Configuring..."
    mkdir -p "$BUILD"
    cmake -B "$BUILD" -S "$DIR" \
          -DCMAKE_TOOLCHAIN_FILE="$DIR/cmake/xtensa_esp32s3.cmake" \
          -DCMAKE_BUILD_TYPE=Release -G Ninja -DSERIAL_PORT="$PORT" 2>&1 | tail -5
    info "Building..."
    cmake --build "$BUILD" --parallel "$(nproc)"
    ok "Build complete → $BUILD/bin/nanos_os.bin"
    xtensa-esp32s3-elf-size "$BUILD/nanos_os.elf" ;;
  flash)
    info "Flashing to $PORT..."
    python3 -m esptool --chip esp32s3 --port "$PORT" --baud 921600 \
      write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
      0x008000 "$BUILD/bin/partition_table.bin" \
      0x010000 "$BUILD/bin/nanos_os.bin"
    ok "Flash complete!" ;;
  monitor)
    python3 -m serial.tools.miniterm --raw --filter=colorize "$PORT" 115200 ;;
  clean)
    rm -rf "$BUILD"; ok "Cleaned" ;;
  all)
    "$0" build "$PORT"; "$0" flash "$PORT" ;;
  *)
    echo "Usage: $0 [build|flash|monitor|clean|all] [PORT]" ;;
esac
