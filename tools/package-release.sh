#!/usr/bin/env bash
# Build the firmware and collect everything a GitHub release needs into dist/.
#
# Produces both ways of flashing:
#   * the four separate images, for `pio run -t upload` users and for anyone who
#     wants to reflash only the application
#   * one merged image that covers the whole flash from 0x0, which is what makes
#     a release usable by someone who has never installed PlatformIO -- and what
#     ESP Web Tools needs
#
#   ./tools/package-release.sh            -> dist/..-dev-..
#   ./tools/package-release.sh v1.0.0     -> dist/..-v1.0.0-..
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION=${1:-dev}
ENV=reterminal_e1004
BUILD=.pio/build/$ENV
DIST=dist
NAME=reterminal-immich-frame-$VERSION

# The offsets and flash settings are not folk knowledge -- they come from the
# build environment itself:
#   pio run -t envdump | grep -A6 FLASH_EXTRA_IMAGES
# On the ESP32-S3 the bootloader sits at 0x0, not at 0x1000 as on the original
# ESP32. Getting that wrong produces a board that never boots.
BOOTLOADER_OFFSET=0x0
PARTITIONS_OFFSET=0x8000
BOOTAPP0_OFFSET=0xe000
APP_OFFSET=0x10000
FLASH_MODE=qio
FLASH_FREQ=80m
FLASH_SIZE=8MB

ESPTOOL=${ESPTOOL:-$HOME/.platformio/packages/tool-esptoolpy/esptool.py}
# esptool imports pyserial even for merge_bin, which never opens a serial port,
# and the system python usually does not have it. Whatever python PlatformIO
# itself runs on always does -- find it from pio's shebang when the bundled
# virtualenv is absent, as it is on a Homebrew install.
pick_python() {
  local c
  for c in "${PY_BIN:-}" \
           "$HOME/.platformio/penv/bin/python" \
           "$(head -1 "$(command -v pio)" 2>/dev/null | sed 's|^#!||')" \
           python3; do
    [ -n "$c" ] || continue
    command -v "$c" >/dev/null 2>&1 || continue
    "$c" -c 'import serial' >/dev/null 2>&1 && { echo "$c"; return 0; }
  done
  echo "no python with pyserial found -- 'pip install esptool' and set PY_BIN=..." >&2
  return 1
}
BOOT_APP0=${BOOT_APP0:-$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin}

[ -f "$ESPTOOL" ]   || { echo "esptool.py not found at $ESPTOOL -- set ESPTOOL=..." >&2; exit 1; }
[ -f "$BOOT_APP0" ] || { echo "boot_app0.bin not found at $BOOT_APP0 -- set BOOT_APP0=..." >&2; exit 1; }

echo "==> building $ENV"
pio run -e "$ENV"

rm -rf "$DIST"
mkdir -p "$DIST"
cp "$BUILD/firmware.bin"    "$DIST/$NAME-firmware.bin"
cp "$BUILD/bootloader.bin"  "$DIST/$NAME-bootloader.bin"
cp "$BUILD/partitions.bin"  "$DIST/$NAME-partitions.bin"
cp "$BOOT_APP0"             "$DIST/$NAME-boot_app0.bin"
cp "$BUILD/firmware.elf"    "$DIST/$NAME-firmware.elf"   # keep it: needed to decode backtraces

echo "==> merging"
PY_BIN=$(pick_python)
"$PY_BIN" "$ESPTOOL" --chip esp32s3 merge_bin \
  -o "$DIST/$NAME-merged.bin" \
  --flash_mode "$FLASH_MODE" --flash_freq "$FLASH_FREQ" --flash_size "$FLASH_SIZE" \
  "$BOOTLOADER_OFFSET"  "$BUILD/bootloader.bin" \
  "$PARTITIONS_OFFSET"  "$BUILD/partitions.bin" \
  "$BOOTAPP0_OFFSET"    "$BOOT_APP0" \
  "$APP_OFFSET"         "$BUILD/firmware.bin"

( cd "$DIST" && shasum -a 256 ./*.bin ./*.elf > SHA256SUMS )

cat > "$DIST/FLASHING.md" <<EOF
# Flashing $NAME

## The easy way -- one file, no PlatformIO

Install esptool once (\`pipx install esptool\`, or \`pip install esptool\`), then:

\`\`\`bash
esptool.py --chip esp32s3 --port /dev/ttyUSB0 --baud 460800 \\
    write_flash -z 0x0 $NAME-merged.bin
\`\`\`

On macOS the port looks like \`/dev/cu.usbmodem*\`; on Windows like \`COM5\`.

If the board will not enter the bootloader on its own: hold **Boot**, tap
**Reset**, release **Boot**, and run the command again.

## Individual images

Same result, useful when you only want to replace the application:

| Offset | File |
|---|---|
| $BOOTLOADER_OFFSET | $NAME-bootloader.bin |
| $PARTITIONS_OFFSET | $NAME-partitions.bin |
| $BOOTAPP0_OFFSET | $NAME-boot_app0.bin |
| $APP_OFFSET | $NAME-firmware.bin |

\`\`\`bash
esptool.py --chip esp32s3 --port /dev/ttyUSB0 --baud 460800 \\
    write_flash -z --flash_mode $FLASH_MODE --flash_freq $FLASH_FREQ --flash_size $FLASH_SIZE \\
    $BOOTLOADER_OFFSET $NAME-bootloader.bin \\
    $PARTITIONS_OFFSET $NAME-partitions.bin \\
    $BOOTAPP0_OFFSET $NAME-boot_app0.bin \\
    $APP_OFFSET $NAME-firmware.bin
\`\`\`

The ESP32-S3 bootloader goes at **0x0**, not 0x1000 as on the original ESP32.

## Afterwards

Logs come out on **GPIO43 (TX) / GPIO44 (RX) at 115200**, not USB CDC. The first
boot has no configuration, so the device starts its own Wi-Fi access point --
see "First-time setup" in the README.

\`$NAME-firmware.elf\` is here only so that
\`pio device monitor\` / \`esp32_exception_decoder\` can turn a panic backtrace
into line numbers. You do not flash it.

## Verifying the download

\`\`\`bash
shasum -a 256 -c SHA256SUMS
\`\`\`
EOF

echo
echo "==> $DIST/"
ls -lh "$DIST"
