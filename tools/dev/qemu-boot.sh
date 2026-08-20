#!/usr/bin/env bash
#
# Boot a built WLED firmware image under Espressif's QEMU fork.
#
# READ tools/dev/README.md FIRST: as of esp-develop-9.2.2-20260417 this does not get
# all the way to the application. It is committed because getting this far took a while
# to work out and the next attempt should not start from nothing.
#
# Usage: qemu-boot.sh [env-name] [seconds]
set -euo pipefail

ENV_NAME=${1:-esp32s3_dmx}
RUN_SECONDS=${2:-45}

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
BUILD="$REPO/.pio/build/$ENV_NAME"
PIO_HOME=${PLATFORMIO_CORE_DIR:-$HOME/.platformio}
WORKDIR=${QEMU_WORKDIR:-$REPO/.pio/qemu}
CA=${CCR_CA_BUNDLE:-/root/.ccr/ca-bundle.crt}
[ -f "$CA" ] && export REQUESTS_CA_BUNDLE="$CA" SSL_CERT_FILE="$CA"

[ -f "$BUILD/firmware.bin" ] || { echo "Build $ENV_NAME first"; exit 1; }
mkdir -p "$WORKDIR"

# --- QEMU itself. The download URL comes from ESP-IDF's tools.json so this does not
# --- go stale every release. api.github.com is out of scope here; raw and release
# --- downloads are reachable.
if [ ! -x "$WORKDIR/qemu/bin/qemu-system-xtensa" ]; then
  echo "== Fetching Espressif QEMU"
  URL=$(curl -sS https://raw.githubusercontent.com/espressif/esp-idf/master/tools/tools.json |
        python3 -c "import json,sys
d = json.load(sys.stdin)
for t in d['tools']:
    if t['name'] == 'qemu-xtensa':
        print(t['versions'][0]['linux-amd64']['url'])
        break")
  curl -sSL -o "$WORKDIR/qemu.tar.xz" "$URL"
  tar xf "$WORKDIR/qemu.tar.xz" -C "$WORKDIR"
fi

# QEMU links against these even with -display none.
ldd "$WORKDIR/qemu/bin/qemu-system-xtensa" 2>/dev/null | grep -q "not found" && {
  echo "== Installing QEMU's shared library dependencies"
  apt-get install -y --no-install-recommends libsdl2-2.0-0 libslirp0
}

# --- Flash image. Offsets match tools/WLED_ESP32_8MB.csv; esptool needs PlatformIO's
# --- penv python, which is where its own dependencies are installed.
echo "== Merging flash image"
"$PIO_HOME/penv/bin/python" "$PIO_HOME/packages/tool-esptoolpy/esptool.py" \
  --chip esp32s3 merge-bin -o "$WORKDIR/flash.bin" \
  --flash-mode dio --flash-freq 80m --flash-size 8MB --pad-to-size 8MB \
  0x0     "$BUILD/bootloader.bin" \
  0x8000  "$BUILD/partitions.bin" \
  0xe000  "$PIO_HOME/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin" \
  0x10000 "$BUILD/firmware.bin"

echo "== Booting for ${RUN_SECONDS}s (serial -> $WORKDIR/serial.log)"
timeout "$RUN_SECONDS" "$WORKDIR/qemu/bin/qemu-system-xtensa" \
  -machine esp32s3 -display none \
  -drive file="$WORKDIR/flash.bin",if=mtd,format=raw \
  -serial "file:$WORKDIR/serial.log" \
  -d unimp,guest_errors -D "$WORKDIR/trace.log" || true

echo "--- serial ---"; cat "$WORKDIR/serial.log" 2>/dev/null
echo "--- unimplemented / guest errors ---"; sort -u "$WORKDIR/trace.log" 2>/dev/null
